PHP_ARG_ENABLE([paint],
  [whether to enable paint support],
  [AS_HELP_STRING([--enable-paint], [Enable the paint shape painting extension])],
  [no])

if test "$PHP_PAINT" != "no"; then
  dnl -ffp-contract=off: no fused multiply-add, so every platform rounds each step the same way.
  PHP_NEW_EXTENSION([paint],
    [src/paint.c src/core.c src/PaintSurface.c],
    [$ext_shared],, [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1 -ffp-contract=off])
  PHP_ADD_BUILD_DIR([$ext_builddir/src])
fi
