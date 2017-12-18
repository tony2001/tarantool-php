#include <zend.h>
#include <zend_API.h>
#include <zend_exceptions.h>

#include "php_tarantool.h"
#include "tarantool_msgpack.h"
#include "third_party/msgpuck.h"

#ifndef    HASH_KEY_NON_EXISTENT
#define    HASH_KEY_NON_EXISTENT HASH_KEY_NON_EXISTANT
#endif  /* HASH_KEY_NON_EXISTENT */

/* UTILITES */

int smart_string_ensure(smart_string *str, size_t len) {
	if (SSTR_AWA(str) > SSTR_LEN(str) + len)
		return 0;
	size_t needed = str->a * 2;
	if (SSTR_LEN(str) + len > needed)
		needed = SSTR_LEN(str) + len;
	register size_t __n1;
	smart_string_alloc4(str, needed, 0, __n1);
	return 0;
}

void smart_string_nullify(smart_string *str) {
	memset(SSTR_BEG(str), 0, SSTR_AWA(str));
}

/* PACKING ROUTINES */

void php_mp_pack_nil(smart_string *str) {
	size_t needed = mp_sizeof_nil();
	smart_string_ensure(str, needed);
	mp_encode_nil(SSTR_POS(str));
	SSTR_LEN(str) += needed;
}

void php_mp_pack_long_pos(smart_string *str, long val) {
	size_t needed = mp_sizeof_uint(val);
	smart_string_ensure(str, needed);
	mp_encode_uint(SSTR_POS(str), val);
	SSTR_LEN(str) += needed;
}

void php_mp_pack_long_neg(smart_string *str, long val) {
	size_t needed = mp_sizeof_int(val);
	smart_string_ensure(str, needed);
	mp_encode_int(SSTR_POS(str), val);
	SSTR_LEN(str) += needed;
}

void php_mp_pack_long(smart_string *str, long val) {
	if (val >= 0)
		php_mp_pack_long_pos(str, val);
	else
		php_mp_pack_long_neg(str, val);
}

void php_mp_pack_double(smart_string *str, double val) {
	size_t needed = mp_sizeof_double(val);
	smart_string_ensure(str, needed);
	mp_encode_double(SSTR_POS(str), val);
	SSTR_LEN(str) += needed;
}

void php_mp_pack_bool(smart_string *str, unsigned char val) {
	size_t needed = mp_sizeof_bool(val);
	smart_string_ensure(str, needed);
	mp_encode_bool(SSTR_POS(str), val);
	SSTR_LEN(str) += needed;
}

void php_mp_pack_string(smart_string *str, char *c, size_t len) {
	size_t needed = mp_sizeof_str(len);
	smart_string_ensure(str, needed);
	mp_encode_str(SSTR_POS(str), c, len);
	SSTR_LEN(str) += needed;
}

void php_mp_pack_hash(smart_string *str, size_t len) {
	size_t needed = mp_sizeof_map(len);
	smart_string_ensure(str, needed);
	mp_encode_map(SSTR_POS(str), len);
	SSTR_LEN(str) += needed;
}

void php_mp_pack_array(smart_string *str, size_t len) {
	size_t needed = mp_sizeof_array(len);
	smart_string_ensure(str, needed);
	mp_encode_array(SSTR_POS(str), len);
	SSTR_LEN(str) += needed;
}

int php_mp_is_hash(zval *val) {
	HashTable *ht = Z_ARRVAL_P(val);
	int count = zend_hash_num_elements(ht);
	if (count != ht->nNextFreeElement) {
		return 1;
	} else {
		HashPosition pos = {0};
		zend_hash_internal_pointer_reset_ex(ht, &pos);
		int i = 0;
		for (; i < count; ++i) {
			if (zend_hash_get_current_key_type_ex(ht, &pos) != \
					HASH_KEY_IS_LONG)
				return 1;
			zend_hash_move_forward_ex(ht, &pos);
		}
	}
	return 0;
}

void php_mp_pack_array_recursively(smart_string *str, zval *val) {
	HashTable *ht = Z_ARRVAL_P(val);
	size_t n = zend_hash_num_elements(ht);

	zval *data;

	php_mp_pack_array(str, n);
	size_t key_index = 0;
	for (; key_index < n; ++key_index) {
		data = zend_hash_index_find(ht, key_index);
		if (!data || data == val ||
				(Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)) && Z_ARRVAL_P(data)->u.v.nApplyCount > 1)) {
			php_mp_pack_nil(str);
		} else {
			if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)))
				Z_ARRVAL_P(data)->u.v.nApplyCount++;
			php_mp_pack(str, data);
			if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)))
				Z_ARRVAL_P(data)->u.v.nApplyCount--;
		}
	}
}

void php_mp_pack_hash_recursively(smart_string *str, zval *val) {
	HashTable *ht = Z_ARRVAL_P(val);
	size_t n = zend_hash_num_elements(ht);

	zend_string *key;
	int key_type;
	ulong key_index;
	zval *data;
	HashPosition pos;

	php_mp_pack_hash(str, n);
	zend_hash_internal_pointer_reset_ex(ht, &pos);
	for (;; zend_hash_move_forward_ex(ht, &pos)) {
		key_type = zend_hash_get_current_key_ex(ht, &key, &key_index, &pos);
		if (key_type == HASH_KEY_NON_EXISTENT)
			break;
		switch (key_type) {
		case HASH_KEY_IS_LONG:
			php_mp_pack_long(str, key_index);
			break;
		case HASH_KEY_IS_STRING:
			php_mp_pack_string(str, ZSTR_VAL(key), ZSTR_LEN(key));
			break;
		default:
			/* TODO: THROW EXCEPTION */
			php_mp_pack_string(str, "", strlen(""));
			break;
		}
		data = zend_hash_get_current_data_ex(ht, &pos);
		if (!data || data == val ||
				(Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)) && Z_ARRVAL_P(data)->u.v.nApplyCount > 1)) {
			php_mp_pack_nil(str);
		} else {
			if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)))
				Z_ARRVAL_P(data)->u.v.nApplyCount++;
			php_mp_pack(str, data);
			if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)))
				Z_ARRVAL_P(data)->u.v.nApplyCount--;
		}
	}
}

void php_mp_pack(smart_string *str, zval *val) {
	switch(Z_TYPE_P(val)) {
	case IS_NULL:
		php_mp_pack_nil(str);
		break;
	case IS_LONG:
		php_mp_pack_long(str, Z_LVAL_P(val));
		break;
	case IS_DOUBLE:
		php_mp_pack_double(str, (double )Z_DVAL_P(val));
		break;
	case IS_TRUE:
		php_mp_pack_bool(str, 1);
		break;
	case IS_FALSE:
		php_mp_pack_bool(str, 0);
		break;
	case IS_ARRAY:
		if (php_mp_is_hash(val))
			php_mp_pack_hash_recursively(str, val);
		else
			php_mp_pack_array_recursively(str, val);
		break;
	case IS_STRING:
		php_mp_pack_string(str, Z_STRVAL_P(val), Z_STRLEN_P(val));
		break;
	default:
		/* TODO: THROW EXCEPTION */
		php_mp_pack_nil(str);
		break;
	}
}

/* UNPACKING ROUTINES */

ptrdiff_t php_mp_unpack_nil(zval *oval, char **str) {
	size_t needed = mp_sizeof_nil();
	mp_decode_nil((const char **)str);
	ZVAL_NULL(oval);
	str += 1;
	return needed;
}

ptrdiff_t php_mp_unpack_uint(zval *oval, char **str) {
	unsigned long val = mp_decode_uint((const char **)str);
	ZVAL_LONG(oval, val);
	return mp_sizeof_uint(val);
}

ptrdiff_t php_mp_unpack_int(zval *oval, char **str) {
	long val = mp_decode_int((const char **)str);
	ZVAL_LONG(oval, val);
	return mp_sizeof_int(val);
}

ptrdiff_t php_mp_unpack_str(zval *oval, char **str) {
	uint32_t len = 0;
	const char *out = mp_decode_str((const char **)str, &len);
	ZVAL_STRINGL(oval, out, len);
	return mp_sizeof_str(len);
}

ptrdiff_t php_mp_unpack_bin(zval *oval, char **str) {
	uint32_t len = 0;
	const char *out = mp_decode_bin((const char **)str, &len);
	char *out_alloc = emalloc(len * sizeof(char));
	memcpy(out_alloc, out, len);
	ZVAL_STRINGL(oval, out_alloc, len);
	efree(out_alloc);
	return mp_sizeof_bin(len);
}

ptrdiff_t php_mp_unpack_bool(zval *oval, char **str) {
	if (mp_decode_bool((const char **)str)) {
		ZVAL_TRUE(oval);
	} else {
		ZVAL_FALSE(oval);
	}
	return mp_sizeof_bool(str);
}

ptrdiff_t php_mp_unpack_float(zval *oval, char **str) {
	float val = mp_decode_float((const char **)str);
	ZVAL_DOUBLE(oval, (double )val);
	return mp_sizeof_float(val);
}

ptrdiff_t php_mp_unpack_double(zval *oval, char **str) {
	double val = mp_decode_double((const char **)str);
	ZVAL_DOUBLE(oval, (double )val);
	return mp_sizeof_double(val);
}

static const char *op_to_string(zend_uchar type) {
	switch(type) {
	case(IS_NULL):
		return "NULL";
	case(IS_LONG):
		return "LONG";
	case(IS_DOUBLE):
		return "DOUBLE";
	case(IS_TRUE):
		return "TRUE";
	case(IS_FALSE):
		return "FALSE";
	case(IS_ARRAY):
		return "ARRAY";
	case(IS_OBJECT):
		return "OBJECT";
	case(IS_STRING):
		return "STRING";
	case(IS_RESOURCE):
		return "RESOURCE";
	case(IS_CONSTANT):
		return "CONSTANT";
#ifdef IS_CONSTANT_ARRAY
	case(IS_CONSTANT_ARRAY):
		return "CONSTANT_ARRAY";
#endif
#ifdef IS_CALLABLE
	case(IS_CALLABLE):
		return "CALLABLE";
#endif
	default:
		return "UNKNOWN";
	}
}

ptrdiff_t php_mp_unpack_map(zval *oval, char **str) {
	TSRMLS_FETCH();
	size_t len = mp_decode_map((const char **)str);
	array_init_size(oval, len);
	while (len-- > 0) {
		zval key, value;
		ZVAL_UNDEF(&key);
		ZVAL_UNDEF(&value);
		if (php_mp_unpack(&key, str) == FAILURE) {
			goto error;
		}
		if (php_mp_unpack(&value, str) == FAILURE) {
			goto error;
		}
		switch (Z_TYPE(key)) {
		case IS_LONG:
			add_index_zval(oval, Z_LVAL(key), &value);
			break;
		case IS_STRING:
			add_assoc_zval(oval, Z_STRVAL(key), &value);
			break;
		case IS_DOUBLE:
			/* convert to INT/STRING for future uses */
			/* FALLTHROUGH */
		default: {
//			char k[256];
//			char *op = op_to_string(Z_TYPE(key));
//			printf("oplen: %d\n", strlen(op));
//			printf("op: %s\n", op);
//			snprintf(k, sizeof(k), "Bad key type for PHP Array: '%s'", op);
//			THROW_EXC(k);
			THROW_EXC("Bad key type for PHP Array");
			goto error;
			break;
		}
		}
		zval_ptr_dtor(&key);
		continue;
error:
		zval_ptr_dtor(&key);
		zval_ptr_dtor(&value);
		return FAILURE;
	}
	return SUCCESS;
}

ptrdiff_t php_mp_unpack_array(zval *oval, char **str) {
	size_t len = mp_decode_array((const char **)str);
	array_init_size(oval, len);
	while (len-- > 0) {
		zval value;
		ZVAL_UNDEF(&value);
		if (php_mp_unpack(&value, str) == FAILURE) {
			zval_ptr_dtor(&value);
			return FAILURE;
		}
		add_next_index_zval(oval, &value);
	}
	return SUCCESS;
}

ssize_t php_mp_unpack(zval *oval, char **str) {
	size_t needed = 0;
	switch (mp_typeof(**str)) {
	case MP_NIL:
		return php_mp_unpack_nil(oval, str);
	case MP_UINT:
		return php_mp_unpack_uint(oval, str);
	case MP_INT:
		return php_mp_unpack_int(oval, str);
	case MP_STR:
		return php_mp_unpack_str(oval, str);
	case MP_BIN:
		return php_mp_unpack_bin(oval, str);
	case MP_ARRAY:
		return php_mp_unpack_array(oval, str);
	case MP_MAP:
		return php_mp_unpack_map(oval, str);
	case MP_BOOL:
		return php_mp_unpack_bool(oval, str);
	case MP_FLOAT:
		return php_mp_unpack_float(oval, str);
	case MP_DOUBLE:
		return php_mp_unpack_double(oval, str);
	case MP_EXT:
		break;
	}
	return FAILURE;
}

/* SIZEOF ROUTINES */
size_t php_mp_sizeof_nil() {
	return mp_sizeof_nil();
}

size_t php_mp_sizeof_long_pos(long val) {
	return mp_sizeof_uint(val);
}

size_t php_mp_sizeof_long_neg(long val) {
	return mp_sizeof_int(val);
}

size_t php_mp_sizeof_long(long val) {
	if (val >= 0)
		return php_mp_sizeof_long_pos(val);
	return php_mp_sizeof_long_neg(val);
}

size_t php_mp_sizeof_double(double val) {
	return mp_sizeof_double(val);
}

size_t php_mp_sizeof_bool(unsigned char val) {
	return mp_sizeof_bool(val);
}

size_t php_mp_sizeof_string(size_t len) {
	return mp_sizeof_str(len);
}

size_t php_mp_sizeof_hash(size_t len) {
	return mp_sizeof_map(len);
}

size_t php_mp_sizeof_array(size_t len) {
	return mp_sizeof_array(len);
}

size_t php_mp_sizeof_array_recursively(zval *val) {
	TSRMLS_FETCH();
	HashTable *ht = HASH_OF(val);
	size_t n = zend_hash_num_elements(ht);
	size_t needed = php_mp_sizeof_array(n);
	size_t key_index = 0;
	zval *data;

	for (; key_index < n; ++key_index) {
		data = zend_hash_index_find(ht, key_index);
		if (!data || data == val ||
				(Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)) && Z_ARRVAL_P(data)->u.v.nApplyCount > 1)) {
			needed += php_mp_sizeof_nil();
		} else {
			if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)))
				Z_ARRVAL_P(data)->u.v.nApplyCount++;
			needed += php_mp_sizeof(data);
			if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)))
				Z_ARRVAL_P(data)->u.v.nApplyCount--;
		}
	}
	return needed;
}

size_t php_mp_sizeof_hash_recursively(zval *val) {
	TSRMLS_FETCH();
	HashTable *ht = HASH_OF(val);
	size_t n = zend_hash_num_elements(ht);
	size_t needed = php_mp_sizeof_hash(n);

	zend_string *key;
	int key_type;
	ulong key_index;
	zval *data;
	HashPosition pos;

	zend_hash_internal_pointer_reset_ex(ht, &pos);
	for (;; zend_hash_move_forward_ex(ht, &pos)) {
		key_type = zend_hash_get_current_key_ex(ht, &key, &key_index, &pos);
		if (key_type == HASH_KEY_NON_EXISTENT)
			break;
		switch (key_type) {
		case HASH_KEY_IS_LONG:
			needed += php_mp_sizeof_long(key_index);
			break;
		case HASH_KEY_IS_STRING:
			needed += php_mp_sizeof_string(ZSTR_LEN(key));
			break;
		default:
			/* TODO: THROW EXCEPTION */
			needed += php_mp_sizeof_string(strlen(""));
			break;
		}
		data = zend_hash_get_current_data_ex(ht, &pos);
		if (!data || data == val ||
				(Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)) && Z_ARRVAL_P(data)->u.v.nApplyCount > 1)) {
			needed += php_mp_sizeof_nil();
		} else {
			if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)))
				Z_ARRVAL_P(data)->u.v.nApplyCount++;
			needed += php_mp_sizeof(data);
			if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data)))
				Z_ARRVAL_P(data)->u.v.nApplyCount--;
		}
	}
	return needed;
}


size_t php_mp_sizeof(zval *val) {
	switch(Z_TYPE_P(val)) {
	case IS_NULL:
		return php_mp_sizeof_nil();
		break;
	case IS_LONG:
		return php_mp_sizeof_long(Z_LVAL_P(val));
		break;
	case IS_DOUBLE:
		return php_mp_sizeof_double((double )Z_DVAL_P(val));
		break;
	case IS_FALSE:
		return php_mp_sizeof_bool(0);
		break;
	case IS_TRUE:
		return php_mp_sizeof_bool(1);
		break;
	case IS_ARRAY:
		if (php_mp_is_hash(val))
			return php_mp_sizeof_hash_recursively(val);
		return php_mp_sizeof_array_recursively(val);
		break;
	case IS_STRING:
		return php_mp_sizeof_string(Z_STRLEN_P(val));
		break;
	default:
		/* TODO: THROW EXCEPTION */
		return php_mp_sizeof_nil();
		break;
	}
}

/* OTHER */

size_t php_mp_check(const char *str, size_t str_size) {
	return mp_check(&str, str + str_size);
}

void php_mp_pack_package_size(smart_string *str, size_t val) {
	size_t needed = 5;
	smart_string_ensure(str, needed);
	*(SSTR_POS(str)) = 0xce;
	*(uint32_t *)(SSTR_POS(str) + 1) = mp_bswap_u32(val);
	SSTR_LEN(str) += needed;
}

size_t php_mp_unpack_package_size(char* str) {
	return mp_decode_uint((const char **)&str);
}
