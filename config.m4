PHP_ARG_ENABLE(bprof, whether to enable bprof support,
[ --enable-bprof      Enable bprof support])

if test "$PHP_BPROF" != "no"; then
  PHP_NEW_EXTENSION(bprof, php_bprof.c, $ext_shared)
fi
