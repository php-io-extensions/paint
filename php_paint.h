#ifndef PHP_PAINT_H
#define PHP_PAINT_H

extern zend_module_entry paint_module_entry;
#define phpext_paint_ptr &paint_module_entry

#define PHP_PAINT_VERSION "0.11.0"

#if defined(ZTS) && defined(COMPILE_DL_PAINT)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif
