PHP_ARG_ENABLE(bprof, whether to enable bprof support,
[ --enable-bprof      Enable bprof support])

if test "$PHP_BPROF" != "no"; then

  AC_MSG_CHECKING([for PCRE includes])

  if test -f $phpincludedir/ext/pcre/php_pcre.h; then
    AC_DEFINE([HAVE_PCRE], 1, [have pcre headers])
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
  fi

  PHP_NEW_EXTENSION(bprof, bprof.c, $ext_shared)
fi
