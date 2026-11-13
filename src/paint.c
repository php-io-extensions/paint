/*
 * paint: shapes painted straight into pixels in C. PaintSurface fills paths,
 * ellipses, rounded boxes, shadows and thin lines, in a colour or a gradient.
 */

#include "runtime.h"
#include "ext/standard/info.h"
#include "../stubs/paint_arginfo.h"

PHP_MINIT_FUNCTION(paint)
{
	(void) type;

	register_paint_symbols(module_number);

	phppaint_register_PaintSurface();

	return SUCCESS;
}

PHP_RINIT_FUNCTION(paint)
{
	(void) type;
	(void) module_number;
#if defined(COMPILE_DL_PAINT) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return SUCCESS;
}

PHP_MINFO_FUNCTION(paint)
{
	(void) zend_module;

	php_info_print_table_start();
	php_info_print_table_row(2, "paint support", "enabled");
	php_info_print_table_row(2, "Version", PHP_PAINT_VERSION);
	php_info_print_table_end();
}

zend_module_entry paint_module_entry = {
	STANDARD_MODULE_HEADER,
	"paint",
	NULL,
	PHP_MINIT(paint),
	NULL,
	PHP_RINIT(paint),
	NULL,
	PHP_MINFO(paint),
	PHP_PAINT_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_PAINT
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(paint)
#endif
