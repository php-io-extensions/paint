/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 77692cde1a0e31ac9123eda40ef8b95948c9c711 */

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_PaintSurface___construct, 0, 0, 2)
	ZEND_ARG_TYPE_INFO(0, width, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, height, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, order, IS_LONG, 0, "PAINT_ORDER_RGBA")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_PaintSurface_over, 0, 6, PaintSurface, 0)
	ZEND_ARG_TYPE_INFO(0, address, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, width, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, height, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, stride, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, order, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, owner, IS_OBJECT, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_PaintSurface_gradient, 0, 5, IS_ARRAY, 1)
	ZEND_ARG_TYPE_INFO(0, kind, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, spread, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, geometry, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, stops, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, matrix, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_PaintSurface_width, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_PaintSurface_height arginfo_class_PaintSurface_width

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_PaintSurface_bytes, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_PaintSurface_clear, 0, 5, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, rgba, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, x, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, y, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, width, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, height, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_PaintSurface_fill, 0, 6, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, ops, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, paints, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, x, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, y, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, width, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, height, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, antialias, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_METHOD(PaintSurface, __construct);
ZEND_METHOD(PaintSurface, over);
ZEND_METHOD(PaintSurface, gradient);
ZEND_METHOD(PaintSurface, width);
ZEND_METHOD(PaintSurface, height);
ZEND_METHOD(PaintSurface, bytes);
ZEND_METHOD(PaintSurface, clear);
ZEND_METHOD(PaintSurface, fill);

static const zend_function_entry class_PaintSurface_methods[] = {
	ZEND_ME(PaintSurface, __construct, arginfo_class_PaintSurface___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(PaintSurface, over, arginfo_class_PaintSurface_over, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(PaintSurface, gradient, arginfo_class_PaintSurface_gradient, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(PaintSurface, width, arginfo_class_PaintSurface_width, ZEND_ACC_PUBLIC)
	ZEND_ME(PaintSurface, height, arginfo_class_PaintSurface_height, ZEND_ACC_PUBLIC)
	ZEND_ME(PaintSurface, bytes, arginfo_class_PaintSurface_bytes, ZEND_ACC_PUBLIC)
	ZEND_ME(PaintSurface, clear, arginfo_class_PaintSurface_clear, ZEND_ACC_PUBLIC)
	ZEND_ME(PaintSurface, fill, arginfo_class_PaintSurface_fill, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_PaintSurface(void)
{
	zend_class_entry ce, *class_entry;

	INIT_CLASS_ENTRY(ce, "PaintSurface", class_PaintSurface_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL|ZEND_ACC_NOT_SERIALIZABLE);

	return class_entry;
}
