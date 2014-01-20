dnl config.m4 for extension tarantool
PHP_ARG_ENABLE(tarantool, for tarantool support,
[  --enable-tarantool	Enable tarantool support])

if test "$PHP_TARANTOOL" != "no"; then
   PHP_NEW_EXTENSION(tarantool, tarantool.c, $ext_shared)
fi
