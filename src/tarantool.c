#include <time.h>
#include <stdio.h>
#include <limits.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include "php_tarantool.h"

#include "tarantool_msgpack.h"
#include "tarantool_proto.h"
#include "tarantool_schema.h"
#include "tarantool_tp.h"

static int le_ptarantool;

int __tarantool_authenticate(tarantool_object *obj);

double now_gettimeofday(void)
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec * 1e9 + t.tv_usec * 1e3;
}

void smart_string_nullify(smart_string *str);

ZEND_DECLARE_MODULE_GLOBALS(tarantool)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define TARANTOOL_PARSE_PARAMS(ID, FORMAT, ...) zval *ID;		\
	if (zend_parse_method_parameters(ZEND_NUM_ARGS(),	\
				getThis(), "O" FORMAT,			\
				&ID, tarantool_class_ptr,		\
				__VA_ARGS__) == FAILURE)		\
		RETURN_FALSE;

static inline tarantool_object *php_tarantool_object(zend_object *obj) {
	return (tarantool_object *)((char*)(obj) - XtOffsetOf(tarantool_object, zo));
}

#define TARANTOOL_FETCH_OBJECT(NAME)                           \
	tarantool_object *NAME = php_tarantool_object(Z_OBJ_P(getThis()))


#define TARANTOOL_CONNECT_ON_DEMAND(CON, ID)				\
	if (!CON->stream)						\
		if (__tarantool_connect(CON, ID) == FAILURE)	\
			RETURN_FALSE;					\
	if (CON->stream && php_stream_eof(CON->stream) != 0)		\
		if (__tarantool_reconnect(CON, ID) == FAILURE)\
			RETURN_FALSE;

#define TARANTOOL_RETURN_DATA(HT, HEAD, BODY)				\
	HashTable *ht = HASH_OF(HT);				\
	zval *answer;						\
	answer = zend_hash_index_find(ht, TNT_DATA);			\
	if (!answer) {	\
		THROW_EXC("No field DATA in body");			\
		zval_ptr_dtor(HEAD);					\
		zval_ptr_dtor(BODY);					\
		RETURN_FALSE;						\
	}								\
	RETVAL_ZVAL(answer, 1, 0);					\
	zval_ptr_dtor(HEAD);						\
	zval_ptr_dtor(BODY);						\
	return;

#define RLCI(NAME)							\
	REGISTER_LONG_CONSTANT("TARANTOOL_ITER_" # NAME,		\
		ITERATOR_ ## NAME, CONST_CS | CONST_PERSISTENT)

zend_object_handlers tarantool_obj_handlers;

zend_function_entry tarantool_module_functions[] = {
	{NULL, NULL, NULL}
};

zend_module_entry tarantool_module_entry = {
	STANDARD_MODULE_HEADER,
	"tarantool16",
	tarantool_module_functions,
	PHP_MINIT(tarantool),
	PHP_MSHUTDOWN(tarantool),
	PHP_RINIT(tarantool),
	NULL,
	PHP_MINFO(tarantool),
	"1.0",
	STANDARD_MODULE_PROPERTIES
};

PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("tarantool.persistent16", "1", PHP_INI_ALL, OnUpdateBool, persistent16, zend_tarantool_globals, tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.timeout", "10.0", PHP_INI_ALL, OnUpdateReal, timeout, zend_tarantool_globals, tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.request_timeout", "10.0", PHP_INI_ALL, OnUpdateReal, request_timeout, zend_tarantool_globals, tarantool_globals)
PHP_INI_END()

#ifdef COMPILE_DL_TARANTOOL
ZEND_GET_MODULE(tarantool)
#endif

void double_to_tv(double tm, struct timeval *tv)
{
	tv->tv_sec = floor(tm);
	tv->tv_usec = floor((tm - floor(tm)) * pow(10, 6));
}

void double_to_ts(double tm, struct timespec *ts)
{
	ts->tv_sec = floor(tm);
	ts->tv_nsec = floor((tm - floor(tm)) * pow(10, 9));
}

static int tarantool_stream_send(tarantool_object *obj)  /* {{{ */
{
	if (php_stream_write(obj->stream, SSTR_BEG(&obj->value), SSTR_LEN(&obj->value)) != SSTR_LEN(&obj->value) || php_stream_flush(obj->stream)) {
		return FAILURE;
	}
	SSTR_LEN(&obj->value) = 0;
	smart_string_nullify(&obj->value);
	return SUCCESS;
}
/* }}} */

static size_t tarantool_stream_read(tarantool_object *obj, char *buf, size_t size) /* {{{ */
{
	size_t total_size = 0;
	size_t read_size = 0;

	while (total_size < size) {
		read_size = php_stream_read(obj->stream, buf + total_size, size - total_size);
		if (read_size <= 0) {
			break;
		}
		total_size += read_size;
	}
	return total_size;
}
/* }}} */

static void tarantool_stream_close(tarantool_object *obj) /* {{{ */
{
	if (obj->stream) {
		if (obj->persistent) {
			zend_hash_str_del(&EG(persistent_list), obj->hashkey2, strlen(obj->hashkey2));
		} else {
			php_stream_close(obj->stream);
		}
	}
	obj->stream = NULL;
}
/* }}} */

int __tarantool_connect(tarantool_object *obj, zval *id) /* {{{ */
{
	char err[512];
	struct timeval tv = {0};
	int errcode = 0, options, flags;
	char  *addr = NULL;
	zend_string *errstr = NULL;
	size_t addr_len = 0;
	zend_resource prsrc;

	addr_len = spprintf(&addr, 0, "%s:%d", obj->host, obj->port);
	options = REPORT_ERRORS;

	if (obj->persistent) {
		options |= STREAM_OPEN_PERSISTENT;

		switch(php_stream_from_persistent_id(obj->hashkey1, &obj->stream)) {
			case PHP_STREAM_PERSISTENT_SUCCESS:
				if (PHP_STREAM_OPTION_RETURN_OK == php_stream_set_option(obj->stream, PHP_STREAM_OPTION_CHECK_LIVENESS, 0, NULL)) {
					return SUCCESS;
				}
				/* dead - kill it */
				php_stream_pclose(obj->stream);
				obj->stream = NULL;

			case PHP_STREAM_PERSISTENT_FAILURE:
			default:
				/* failed; get a new one */
				;
		}
	}

	double_to_tv(TARANTOOL_G(timeout), &tv);
	obj->stream = php_stream_xport_create(addr, addr_len, options, STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT, obj->hashkey1, &tv, NULL, &errstr, &errcode);
	efree(addr);

	if (errcode || !obj->stream) {
		snprintf(err, sizeof(err), "Failed to connect to %s:%d [%d]: %s", obj->host, obj->port, errcode, ZSTR_VAL(errstr));
		zend_string_release(errstr);
		THROW_EXC(err);
		return FAILURE;
	}

	/* Set READ_TIMEOUT */
	double_to_tv(TARANTOOL_G(request_timeout), &tv);
	if (tv.tv_sec != 0 || tv.tv_usec != 0) {
		php_stream_set_option(obj->stream, PHP_STREAM_OPTION_READ_TIMEOUT, 0, &tv);
	}

	/* Set TCP_NODELAY */
	int socketd = ((php_netstream_data_t* )obj->stream->abstract)->socket;
	flags = 1;
	if (setsockopt(socketd, IPPROTO_TCP, TCP_NODELAY, (char *) &flags, sizeof(int))) {
		snprintf(err, sizeof(err), "Failed setsockopt [%d]: %s", errno, strerror(errno));
		zend_string_release(errstr);
		tarantool_stream_close(obj);
		THROW_EXC(err);
		return FAILURE;
	}

	if (tarantool_stream_read(obj, obj->greeting, GREETING_SIZE) == -1) {
		snprintf(err, sizeof(err), "Failed to read server greeting");
		zend_string_release(errstr);
		tarantool_stream_close(obj);
		THROW_EXC(err);
		return FAILURE;
	}

	if (obj->persistent) {
		prsrc.type = le_ptarantool;
		prsrc.ptr  = obj->stream;

		zend_hash_str_update_mem(&EG(persistent_list), obj->hashkey2, strlen(obj->hashkey2), (void *) &prsrc, sizeof(prsrc));
	}

	obj->salt = obj->greeting + SALT_PREFIX_SIZE;

	if (obj->login != NULL && obj->passwd != NULL) {
		tarantool_schema_flush(obj->schema);
		return __tarantool_authenticate(obj);
	}
	return SUCCESS;
}
/* }}} */

int __tarantool_reconnect(tarantool_object *obj, zval *id)
{
	tarantool_stream_close(obj);
	return __tarantool_connect(obj, id);
}

static void tarantool_free(zend_object *zobj) /* {{{ */
{
	tarantool_object *obj = php_tarantool_object(zobj);

	if (!obj) {
		return;
	}

	if (!obj->persistent) {
		tarantool_stream_close(obj);
	} else {
		efree(obj->hashkey1);
		efree(obj->hashkey2);
	}

	if (obj->greeting) {
		efree(obj->greeting);
	}
	if (obj->schema) {
		tarantool_schema_delete(obj->schema);
		efree(obj->schema);
	}
	efree(obj->host);
	smart_string_free(&obj->value);

	if (obj->login)  efree(obj->login);
	if (obj->passwd) efree(obj->passwd);
	if (obj->tps)    tarantool_tp_free(obj->tps);
	zend_object_std_dtor(&obj->zo);
}
/* }}} */

static zend_object *tarantool_create(zend_class_entry *entry) /* {{{ */
{
	tarantool_object *obj = NULL;

	obj = (tarantool_object *)ecalloc(sizeof(tarantool_object), 1);
	zend_object_std_init(&obj->zo, entry);
	obj->zo.handlers = &tarantool_obj_handlers;

	return &obj->zo;
}
/* }}} */

static int64_t tarantool_step_recv_ex(tarantool_object *obj, unsigned long sync, zval *header, zval *body, long *sync_recv) /* {{{ */
{
	char pack_len[5] = {0, 0, 0, 0, 0};
	ZVAL_UNDEF(header);
	ZVAL_UNDEF(body);
	if (tarantool_stream_read(obj, pack_len, 5) != 5) {
		THROW_EXC("Can't read query from server");
		goto error;
	}
	if (php_mp_check(pack_len, 5)) {
		THROW_EXC("Failed verifying msgpack");
		goto error;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(&obj->value, body_size);
	if (tarantool_stream_read(obj, SSTR_POS(&obj->value),
				  body_size) != body_size) {
		THROW_EXC("Can't read query from server");
		goto error;
	}
	SSTR_LEN(&obj->value) += body_size;

	char *pos = SSTR_BEG(&obj->value);
	if (php_mp_check(pos, body_size)) {
		THROW_EXC("Failed verifying msgpack");
		goto error;
	}
	if (php_mp_unpack(header, &pos) == FAILURE ||
	    Z_TYPE_P(header) != IS_ARRAY) {
		goto error;
	}
	if (php_mp_check(pos, body_size)) {
		THROW_EXC("Failed verifying msgpack");
		goto error;
	}
	if (php_mp_unpack(body, &pos) == FAILURE) {
		goto error;
	}

	HashTable *hash = HASH_OF(header);
	zval *val;

	val = zend_hash_index_find(hash, TNT_SYNC);
	if (val) {
		if (sync_recv != NULL) {
				*sync_recv = Z_LVAL_P(val);
		}
	}
	val = zend_hash_index_find(hash, TNT_CODE);
	if (val) {
		if (Z_LVAL_P(val) == TNT_OK) {
			SSTR_LEN(&obj->value) = 0;
			smart_string_nullify(&obj->value);
			return SUCCESS;
		}
		HashTable *hash = HASH_OF(body);
		zval *errstr;
		long errcode = Z_LVAL_P(val) & ((1 << 15) - 1 );

		errstr = zend_hash_index_find(hash, TNT_ERROR);
		if (!errstr) {
			ZVAL_STRING(errstr, "empty");
		}
		THROW_EXC("Query error %d: %s", errcode, Z_STRVAL_P(errstr),
				Z_STRLEN_P(errstr));
		goto error;
	}
	THROW_EXC("Failed to retrieve answer code");
error:
	obj->stream = NULL;
	if (header) zval_ptr_dtor(header);
	if (body) zval_ptr_dtor(body);
	SSTR_LEN(&obj->value) = 0;
	smart_string_nullify(&obj->value);
	return FAILURE;
}
/* }}} */

static int64_t tarantool_step_recv(tarantool_object *obj, unsigned long sync, zval *header, zval *body)
{
	return tarantool_step_recv_ex(obj, sync, header, body, NULL);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_call, 0, 0, 1)
	ZEND_ARG_INFO(0, proc)
	ZEND_ARG_INFO(0, tuple)
	ZEND_ARG_INFO(1, sync)
ZEND_END_ARG_INFO()


const zend_function_entry tarantool_class_methods[] = { /* {{{ */
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, close, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, ping, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, replace, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, arginfo_call, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, eval, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, delete, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, update, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, upsert, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, evaluate, eval, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, flushSchema, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, disconnect, close, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, getSync, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};
/* }}} */

/* ####################### HELPERS ####################### */

void pack_key(zval *args, char select, zval *arr) /* {{{ */
{
	if (args && Z_TYPE_P(args) == IS_ARRAY) {
		ZVAL_DUP(arr, args);
		return;
	}

	if (select && (!args || Z_TYPE_P(args) == IS_NULL)) {
		array_init(arr);
		return;
	}

	array_init(arr);
	Z_ADDREF_P(args);
	add_next_index_zval(arr, args);
}
/* }}} */

int tarantool_update_verify_op(zval *op, long position, zval *arr) /* {{{ */
{
	if (Z_TYPE_P(op) != IS_ARRAY || !php_mp_is_hash(op)) {
		THROW_EXC("Op must be MAP at pos %d", position);
		return 0;
	}
	HashTable *ht = HASH_OF(op);
	size_t n = zend_hash_num_elements(ht);
	zval *opstr, *oppos;

	array_init(arr);

	opstr = zend_hash_str_find(ht, "op", strlen("op"));
	if (!opstr || Z_TYPE_P(opstr) != IS_STRING ||
			Z_STRLEN_P(opstr) != 1) {
		THROW_EXC("Field OP must be provided and must be STRING with "
				"length=1 at position %d", position);
		return 0;
	}
	oppos = zend_hash_str_find(ht, "field", strlen("field"));
	if (!oppos || Z_TYPE_P(oppos) != IS_LONG) {
		THROW_EXC("Field FIELD must be provided and must be LONG at "
				"position %d", position);
		return 0;

	}
	zval *oparg, *splice_len, *splice_val;
	switch(Z_STRVAL_P(opstr)[0]) {
	case ':':
		if (n != 5) {
			THROW_EXC("Five fields must be provided for splice"
					" at position %d", position);
			return 0;
		}
		oparg = zend_hash_str_find(ht, "offset", strlen("offset"));
		if (!oparg || Z_TYPE_P(oparg) != IS_LONG) {
			THROW_EXC("Field OFFSET must be provided and must be LONG for "
					"splice at position %d", position);
			return 0;
		}
		splice_len = zend_hash_str_find(ht, "length", strlen("length"));
		if (!oparg || Z_TYPE_P(splice_len) != IS_LONG) {
			THROW_EXC("Field LENGTH must be provided and must be LONG for "
					"splice at position %d", position);
			return 0;
		}
		splice_val = zend_hash_str_find(ht, "list", strlen("list"));
		if (!oparg || Z_TYPE_P(splice_val) != IS_STRING) {
			THROW_EXC("Field LIST must be provided and must be STRING for "
					"splice at position %d", position);
			return 0;
		}
		add_next_index_stringl(arr, Z_STRVAL_P(opstr), 1);
		add_next_index_long(arr, Z_LVAL_P(oppos));
		add_next_index_long(arr, Z_LVAL_P(oparg));
		add_next_index_long(arr, Z_LVAL_P(splice_len));
		add_next_index_stringl(arr, Z_STRVAL_P(splice_val),
				Z_STRLEN_P(splice_val));
		break;
	case '+':
	case '-':
	case '&':
	case '|':
	case '^':
	case '#':
		if (n != 3) {
			THROW_EXC("Three fields must be provided for '%s' at "
					"position %d", Z_STRVAL_P(opstr), position);
			return 0;
		}
		oparg = zend_hash_str_find(ht, "arg", strlen("arg"));
		if (!oparg || Z_TYPE_P(oparg) != IS_LONG) {
			THROW_EXC("Field ARG must be provided and must be LONG for "
					"'%s' at position %d", Z_STRVAL_P(opstr), position);
			return 0;
		}
		add_next_index_stringl(arr, Z_STRVAL_P(opstr), 1);
		add_next_index_long(arr, Z_LVAL_P(oppos));
		add_next_index_long(arr, Z_LVAL_P(oparg));
		break;
	case '=':
	case '!':
		if (n != 3) {
			THROW_EXC("Three fields must be provided for '%s' at "
					"position %d", Z_STRVAL_P(opstr), position);
			return 0;
		}
		oparg = zend_hash_str_find(ht, "arg", strlen("arg"));
		if (!oparg || !PHP_MP_SERIALIZABLE_P(oparg)) {
			THROW_EXC("Field ARG must be provided and must be SERIALIZABLE for "
					"'%s' at position %d", Z_STRVAL_P(opstr), position);
			return 0;
		}
		add_next_index_stringl(arr, Z_STRVAL_P(opstr), 1);
		add_next_index_long(arr, Z_LVAL_P(oppos));
		//SEPARATE_ZVAL_TO_MAKE_IS_REF(oparg);
		Z_ADDREF_P(oparg);
		add_next_index_zval(arr, oparg);
		break;
	default:
		THROW_EXC("Unknown operation '%s' at position %d",
				Z_STRVAL_P(opstr), position);
		return 0;

	}
	return 1;
}
/* }}} */

int tarantool_update_verify_args(zval *args, zval *arr) /* {{{ */
{
	if (Z_TYPE_P(args) != IS_ARRAY || php_mp_is_hash(args)) {
		THROW_EXC("Provided value for update OPS must be Array");
		return 0;
	}
	HashTable *ht = HASH_OF(args);
	size_t n = zend_hash_num_elements(ht);

	array_init(arr);
	size_t key_index = 0;
	for(; key_index < n; ++key_index) {
		zval *op = zend_hash_index_find(ht, key_index);
		if (!op) {
			THROW_EXC("Internal Array Error");
			goto cleanup;
		}
		zval op_arr;
		if (!tarantool_update_verify_op(op, key_index, &op_arr))
			goto cleanup;
		if (add_next_index_zval(arr, &op_arr) == FAILURE) {
			THROW_EXC("Internal Array Error");
			goto cleanup;
		}
	}
	return 1;
cleanup:
	zval_ptr_dtor(arr);
	return 0;
}
/* }}} */

int get_spaceno_by_name(tarantool_object *obj, zval *id, zval *name) /* {{{ */
{
	if (Z_TYPE_P(name) == IS_LONG) return Z_LVAL_P(name);
	if (Z_TYPE_P(name) != IS_STRING) {
		THROW_EXC("Space ID must be String or Long");
		return FAILURE;
	}
	int32_t space_no = tarantool_schema_get_sid_by_string(obj->schema,
			Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (space_no != FAILURE) return space_no;

	tarantool_tp_update(obj->tps);
	tp_select(obj->tps, SPACE_SPACE, INDEX_SPACE_NAME, 0, 4096);
	tp_key(obj->tps, 1);
	tp_encode_str(obj->tps, Z_STRVAL_P(name), Z_STRLEN_P(name));
	tp_reqid(obj->tps, TARANTOOL_G(sync_counter)++);

	SSTR_LEN(&obj->value) = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj) == FAILURE)
		return FAILURE;

	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5) != 5) {
		THROW_EXC("Can't read query from server");
		return FAILURE;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(&obj->value, body_size);
	if (tarantool_stream_read(obj, SSTR_BEG(&obj->value), body_size) != body_size) {
		THROW_EXC("Can't read query from server");
		return FAILURE;
	}

	struct tnt_response resp; memset(&resp, 0, sizeof(struct tnt_response));
	if (php_tp_response(&resp, SSTR_BEG(&obj->value), body_size) == -1) {
		THROW_EXC("Failed to parse query");
		return FAILURE;
	}

	if (resp.error) {
		THROW_EXC("Query error %d: %.*s", resp.code, resp.error_len, resp.error);
		return FAILURE;
	}

	if (tarantool_schema_add_spaces(obj->schema, resp.data, resp.data_len)) {
		THROW_EXC("Failed parsing schema (space) or memory issues");
		return FAILURE;
	}
	space_no = tarantool_schema_get_sid_by_string(obj->schema,
			Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (space_no == FAILURE)
		THROW_EXC("No space '%s' defined", Z_STRVAL_P(name));
	return space_no;
}
/* }}} */

int get_indexno_by_name(tarantool_object *obj, zval *id, int space_no, zval *name) /* {{{ */
{
	if (Z_TYPE_P(name) == IS_LONG)
		return Z_LVAL_P(name);
	if (Z_TYPE_P(name) != IS_STRING) {
		THROW_EXC("Index ID must be String or Long");
		return FAILURE;
	}
	int32_t index_no = tarantool_schema_get_iid_by_string(obj->schema,
			space_no, Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (index_no != FAILURE) return index_no;

	tarantool_tp_update(obj->tps);
	tp_select(obj->tps, SPACE_INDEX, INDEX_INDEX_NAME, 0, 4096);
	tp_key(obj->tps, 2);
	tp_encode_uint(obj->tps, space_no);
	tp_encode_str(obj->tps, Z_STRVAL_P(name), Z_STRLEN_P(name));
	tp_reqid(obj->tps, TARANTOOL_G(sync_counter)++);

	SSTR_LEN(&obj->value) = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj) == FAILURE)
		return FAILURE;

	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5) != 5) {
		THROW_EXC("Can't read query from server");
		return FAILURE;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(&obj->value, body_size);
	if (tarantool_stream_read(obj, SSTR_BEG(&obj->value), body_size) != body_size) {
		THROW_EXC("Can't read query from server");
		return FAILURE;
	}

	struct tnt_response resp; memset(&resp, 0, sizeof(struct tnt_response));
	if (php_tp_response(&resp, SSTR_BEG(&obj->value), body_size) == -1) {
		THROW_EXC("Failed to parse query");
		return FAILURE;
	}

	if (resp.error) {
		THROW_EXC("Query error %d: %.*s", resp.code, resp.error_len, resp.error);
		return FAILURE;
	}

	if (tarantool_schema_add_indexes(obj->schema, resp.data, resp.data_len)) {
		THROW_EXC("Failed parsing schema (index) or memory issues");
		return FAILURE;
	}
	index_no = tarantool_schema_get_iid_by_string(obj->schema,
			space_no, Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (index_no == FAILURE)
		THROW_EXC("No index '%s' defined", Z_STRVAL_P(name));
	return index_no;
}
/* }}} */

/* ####################### METHODS ####################### */

zend_class_entry *tarantool_class_ptr;

PHP_RINIT_FUNCTION(tarantool) {
	return SUCCESS;
}

static void php_tarantool_init_globals(zend_tarantool_globals *tarantool_globals) {
	tarantool_globals->sync_counter    = 0;
	tarantool_globals->timeout         = 10.0;
	tarantool_globals->request_timeout = 10.0;
	tarantool_globals->persistent16    = 1;
}

static void tarantool_pconnect_dtor(zend_resource *rsrc)
{
	php_stream *stream = (php_stream *)rsrc->ptr;
	php_stream_pclose(stream);
}

PHP_MINIT_FUNCTION(tarantool) /* {{{ */
{
	le_ptarantool = zend_register_list_destructors_ex(NULL, tarantool_pconnect_dtor, "persistent tarantool connection", module_number);
	ZEND_INIT_MODULE_GLOBALS(tarantool, php_tarantool_init_globals, NULL);
	REGISTER_INI_ENTRIES();

	/* Register constants */
	RLCI(EQ);
	RLCI(REQ);
	RLCI(ALL);
	RLCI(LT);
	RLCI(LE);
	RLCI(GE);
	RLCI(GT);
	RLCI(BITSET_ALL_SET);
	RLCI(BITSET_ANY_SET);
	RLCI(BITSET_ALL_NOT_SET);
	RLCI(OVERLAPS);
	RLCI(NEIGHBOR);

	/* Init class entries */
	zend_class_entry tarantool_class;
	INIT_CLASS_ENTRY(tarantool_class, "Tarantool16", tarantool_class_methods);
	tarantool_class.create_object = tarantool_create;
	tarantool_class_ptr = zend_register_internal_class(&tarantool_class);
	memcpy(&tarantool_obj_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	tarantool_obj_handlers.offset = XtOffsetOf(tarantool_object, zo);
	tarantool_obj_handlers.free_obj = tarantool_free;

	return SUCCESS;
}
/* }}} */

PHP_MSHUTDOWN_FUNCTION(tarantool) {
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_MINFO_FUNCTION(tarantool) /* {{{ */
{
	php_info_print_table_start();
	php_info_print_table_header(2, "Tarantool support", "enabled");
	php_info_print_table_row(2, "Extension version", PHP_TARANTOOL_VERSION);
	php_info_print_table_end();
	DISPLAY_INI_ENTRIES();
}
/* }}} */

PHP_METHOD(tarantool_class, __construct) /* {{{ */
{
	char *host = NULL; size_t host_len = 0;
	long port = 0;

	TARANTOOL_PARSE_PARAMS(id, "|sl", &host, &host_len, &port);
	TARANTOOL_FETCH_OBJECT(obj);

	/*
	 * validate parameters
	 */

	if (host == NULL) host = "localhost";
	if (port < 0 || port >= 65536) {
		THROW_EXC("Invalid primary port value: %li", port);
		RETURN_FALSE;
	}
	if (port == 0) port = 3301;

	/* initialzie object structure */
	obj->host = estrdup(host);
	obj->port = port;
	if (TARANTOOL_G(persistent16)) {
		spprintf(&obj->hashkey1, 0, "tarantool:%s:%d", host, port);
		spprintf(&obj->hashkey2, 0, "ptarantool:%s:%d", host, port);
	} else {
		obj->hashkey1 = NULL;
		obj->hashkey2 = NULL;
	}
	obj->persistent = TARANTOOL_G(persistent16);
	obj->auth = 0;
	obj->greeting = (char *)ecalloc(sizeof(char), GREETING_SIZE);
	obj->salt = NULL;
	obj->login = NULL;
	obj->passwd = NULL;
	obj->schema = tarantool_schema_new();
	smart_string_ensure(&obj->value, GREETING_SIZE);
	obj->tps = tarantool_tp_new(&obj->value);
	return;
}
/* }}} */

PHP_METHOD(tarantool_class, connect) /* {{{ */
{
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj);
	if (obj->stream && obj->stream->mode) RETURN_TRUE;
	if (__tarantool_connect(obj, id) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}
/* }}} */

int __tarantool_authenticate(tarantool_object *obj) /* {{{ */
{
	TSRMLS_FETCH();

	tarantool_tp_update(obj->tps);
	int batch_count = 3;
	size_t passwd_len = (obj->passwd ? strlen(obj->passwd) : 0);
	tp_auth(obj->tps, obj->salt, obj->login, strlen(obj->login),
		obj->passwd, passwd_len);
	uint32_t auth_sync = TARANTOOL_G(sync_counter)++;
	tp_reqid(obj->tps, auth_sync);
	tp_select(obj->tps, SPACE_SPACE, 0, 0, 4096);
	tp_key(obj->tps, 0);
	uint32_t space_sync = TARANTOOL_G(sync_counter)++;
	tp_reqid(obj->tps, space_sync);
	tp_select(obj->tps, SPACE_INDEX, 0, 0, 4096);
	tp_key(obj->tps, 0);
	uint32_t index_sync = TARANTOOL_G(sync_counter)++;
	tp_reqid(obj->tps, index_sync);
	SSTR_LEN(&obj->value) = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj) == FAILURE)
		return FAILURE;

	int status = SUCCESS;

	while (batch_count-- > 0) {
		char pack_len[5] = {0, 0, 0, 0, 0};
		if (tarantool_stream_read(obj, pack_len, 5) != 5) {
			THROW_EXC("Can't read query from server");
			return FAILURE;
		}
		size_t body_size = php_mp_unpack_package_size(pack_len);
		smart_string_ensure(&obj->value, body_size);
		if (tarantool_stream_read(obj, SSTR_BEG(&obj->value), body_size) != body_size) {
			THROW_EXC("Can't read query from server");
			return FAILURE;
		}
		if (status == FAILURE) continue;
		struct tnt_response resp;
		memset(&resp, 0, sizeof(struct tnt_response));
		if (php_tp_response(&resp, SSTR_BEG(&obj->value), body_size) == -1) {
			THROW_EXC("Failed to parse query");
			status = FAILURE;
		}

		if (resp.error) {
			THROW_EXC("Query error %d: %.*s", resp.code,
				  resp.error_len, resp.error);
			status = FAILURE;
		}
		if (resp.sync == space_sync) {
			if (tarantool_schema_add_spaces(obj->schema, resp.data,
						        resp.data_len) &&
					status != FAILURE) {
				THROW_EXC("Failed parsing schema (space) or "
					  "memory issues");
				status = FAILURE;
			}
		} else if (resp.sync == index_sync) {
			if (tarantool_schema_add_indexes(obj->schema, resp.data,
							 resp.data_len) &&
					status != FAILURE) {
				THROW_EXC("Failed parsing schema (index) or "
					  "memory issues");
				status = FAILURE;
			}
		} else if (resp.sync == auth_sync && resp.error) {
			THROW_EXC("Query error %d: %.*s", resp.code,
				  resp.error_len, resp.error);
			status = FAILURE;
		}
	}

	return status;
}
/* }}} */

PHP_METHOD(tarantool_class, authenticate) /* {{{ */
{
	char *login; size_t login_len;
	char *passwd; size_t passwd_len;

	TARANTOOL_PARSE_PARAMS(id, "ss", &login, &login_len,
			&passwd, &passwd_len);
	TARANTOOL_FETCH_OBJECT(obj);
	obj->login = estrdup(login);
	obj->passwd = estrdup(passwd);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	__tarantool_authenticate(obj);
	RETURN_NULL();
}
/* }}} */

PHP_METHOD(tarantool_class, flush_schema) /* {{{ */
{
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj);

	tarantool_schema_flush(obj->schema);
	RETURN_TRUE;
}
/* }}} */

PHP_METHOD(tarantool_class, close) /* {{{ */
{
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj);

	tarantool_stream_close(obj);
	tarantool_schema_delete(obj->schema);
	obj->schema = NULL;

	RETURN_TRUE;
}
/* }}} */

PHP_METHOD(tarantool_class, ping) /* {{{ */
{
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_ping(&obj->value, sync);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	zval_ptr_dtor(&header);
	zval_ptr_dtor(&body);
	RETURN_TRUE;
}
/* }}} */

PHP_METHOD(tarantool_class, select) /* {{{ */
{
	zval *space = NULL, *index = NULL;
	zval *key = NULL, key_new;
	zval *zlimit = NULL;
	long limit = LONG_MAX-1, offset = 0, iterator = 0;

	TARANTOOL_PARSE_PARAMS(id, "z|zzzll", &space, &key,
			&index, &zlimit, &offset, &iterator);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	if (zlimit != NULL && Z_TYPE_P(zlimit) != IS_NULL && Z_TYPE_P(zlimit) != IS_LONG) {
		THROW_EXC("wrong type of 'limit' - expected long/null, got '%s'",
				zend_zval_type_name(zlimit));
		RETURN_FALSE;
	} else if (zlimit != NULL && Z_TYPE_P(zlimit) == IS_LONG) {
		limit = Z_LVAL_P(zlimit);
	}

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE) RETURN_FALSE;
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, id, space_no, index);
		if (index_no == FAILURE) RETURN_FALSE;
	}
	pack_key(key, 1, &key_new);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_select(&obj->value, sync, space_no, index_no,
			limit, offset, iterator, &key_new);
	zval_ptr_dtor(&key_new);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
/* }}} */

PHP_METHOD(tarantool_class, insert) /* {{{ */
{
	zval *space, *tuple;

	TARANTOOL_PARSE_PARAMS(id, "za", &space, &tuple);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_insert_or_replace(&obj->value, sync, space_no,
			tuple, TNT_INSERT);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
/* }}} */

PHP_METHOD(tarantool_class, replace) /* {{{ */
{
	zval *space, *tuple;

	TARANTOOL_PARSE_PARAMS(id, "za", &space, &tuple);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_insert_or_replace(&obj->value, sync, space_no,
			tuple, TNT_REPLACE);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
/* }}} */

PHP_METHOD(tarantool_class, delete) /* {{{ */
{
	zval *space = NULL, *key = NULL, *index = NULL;
	zval key_new;

	TARANTOOL_PARSE_PARAMS(id, "zz|z", &space, &key, &index);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE) RETURN_FALSE;
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, id, space_no, index);
		if (index_no == FAILURE) RETURN_FALSE;
	}

	pack_key(key, 0, &key_new);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_delete(&obj->value, sync, space_no, index_no, key);
	zval_ptr_dtor(&key_new);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
/* }}} */

PHP_METHOD(tarantool_class, call) /* {{{ */
{
	char *proc; size_t proc_len;
	zval *tuple = NULL, tuple_new, *zsync = NULL;

	TARANTOOL_PARSE_PARAMS(id, "s|zz/", &proc, &proc_len, &tuple, &zsync);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	pack_key(tuple, 1, &tuple_new);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_call(&obj->value, sync, proc, proc_len, &tuple_new);
	zval_ptr_dtor(&tuple_new);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (zsync) {
		zval_dtor(zsync);
		ZVAL_LONG(zsync, 0);
		if (tarantool_step_recv_ex(obj, sync, &header, &body, &Z_LVAL_P(zsync)) == FAILURE)
			RETURN_FALSE;
	} else {
		if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
			RETURN_FALSE;
	}

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
/* }}} */

PHP_METHOD(tarantool_class, eval) /* {{{ */
{
	char *proc; size_t proc_len;
	zval *tuple = NULL, tuple_new;

	TARANTOOL_PARSE_PARAMS(id, "s|z", &proc, &proc_len, &tuple);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	pack_key(tuple, 1, &tuple_new);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_eval(&obj->value, sync, proc, proc_len, &tuple_new);
	zval_ptr_dtor(&tuple_new);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
/* }}} */

PHP_METHOD(tarantool_class, update) /* {{{ */
{
	zval *space = NULL, *key = NULL, *index = NULL, *args = NULL;
	zval key_new, v_args;

	TARANTOOL_PARSE_PARAMS(id, "zza|z", &space, &key, &args, &index);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE) RETURN_FALSE;
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, id, space_no, index);
		if (index_no == FAILURE) RETURN_FALSE;
	}

	if (!tarantool_update_verify_args(args, &v_args));
	pack_key(key, 0, &key_new);
	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_update(&obj->value, sync, space_no, index_no, &key_new, &v_args);
	zval_ptr_dtor(&key_new);
	zval_ptr_dtor(&v_args);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
/* }}} */

PHP_METHOD(tarantool_class, upsert) /* {{{ */
{
	zval *space = NULL, *tuple = NULL, *args = NULL;
	zval v_args;

	TARANTOOL_PARSE_PARAMS(id, "zaa", &space, &tuple, &args);
	TARANTOOL_FETCH_OBJECT(obj);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE) RETURN_FALSE;

	if (!tarantool_update_verify_args(args, &v_args)) {
		RETURN_FALSE;
	}
	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_upsert(&obj->value, sync, space_no, tuple, &v_args);
	zval_ptr_dtor(&v_args);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
/* }}} */

PHP_METHOD(tarantool_class, getSync)
{
	RETURN_LONG(TARANTOOL_G(sync_counter));
}

