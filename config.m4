PHP_ARG_ENABLE(dumpfatal, whether to enable dumpfatal support,
[  --enable-dumpfatal           Enable dumpfatal support])

if test "$PHP_DUMPFATAL" != "no"; then
  PHP_NEW_EXTENSION(dumpfatal, dumpfatal.c, $ext_shared)
fi
