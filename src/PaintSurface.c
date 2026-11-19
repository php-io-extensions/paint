/*
 * PaintSurface: the pixels, and the ops read out of PHP arrays into the plain
 * C painter in core.c.
 */
#include "runtime.h"
#include "zend_exceptions.h"
#include "../stubs/PaintSurface_arginfo.h"

zend_class_entry *phppaint_ce_PaintSurface;

static zend_object_handlers phppaint_surface_handlers;

static zend_object *phppaint_surface_create(zend_class_entry *ce)
{
	phppaint_surface *obj = zend_object_alloc(sizeof(phppaint_surface), ce);

	zend_object_std_init(&obj->std, ce);
	object_properties_init(&obj->std, ce);
	ZVAL_UNDEF(&obj->owner);

	return &obj->std;
}

static void phppaint_surface_free(zend_object *object)
{
	phppaint_surface *obj = phppaint_surface_from(object);

	if (obj->pixels != NULL) {
		zend_string_release(obj->pixels);
	}
	zval_ptr_dtor(&obj->owner);
	zend_object_std_dtor(object);
}

void phppaint_register_PaintSurface(void)
{
	phppaint_ce_PaintSurface = register_class_PaintSurface();
	phppaint_ce_PaintSurface->create_object = phppaint_surface_create;

	memcpy(&phppaint_surface_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	phppaint_surface_handlers.offset = XtOffsetOf(phppaint_surface, std);
	phppaint_surface_handlers.free_obj = phppaint_surface_free;
	phppaint_surface_handlers.clone_obj = NULL;
	phppaint_surface_handlers.compare = zend_objects_not_comparable;
	phppaint_ce_PaintSurface->default_object_handlers = &phppaint_surface_handlers;
}

/* The surface of $this; NULL, with an Error thrown, when it was never made. */
static paint_surface *phppaint_this(zval *this)
{
	phppaint_surface *obj = phppaint_surface_from(Z_OBJ_P(this));

	if (obj->surface.px == NULL) {
		zend_throw_error(NULL, "PaintSurface has not been constructed");
		return NULL;
	}

	return &obj->surface;
}

static bool phppaint_sides(zend_long width, zend_long height, zend_long order)
{
	if (width < 1 || width > PAINT_MAX_SIDE || height < 1 || height > PAINT_MAX_SIDE) {
		zend_value_error("A surface is 1..%d pixels each side, got " ZEND_LONG_FMT " x " ZEND_LONG_FMT, PAINT_MAX_SIDE, width, height);
		return false;
	}
	if (order < PAINT_ORDER_RGBA || order > PAINT_ORDER_ABGR) {
		zend_value_error("The byte order is one of PAINT_ORDER_RGBA, PAINT_ORDER_BGRA, PAINT_ORDER_ARGB, PAINT_ORDER_ABGR");
		return false;
	}

	return true;
}

/* The rect cut to the surface. */
static paint_clip phppaint_clip(const paint_surface *s, zend_long x, zend_long y, zend_long width, zend_long height)
{
	paint_clip clip;
	zend_long x1 = width > 0 ? x + width : x, y1 = height > 0 ? y + height : y;

	clip.x0 = (int) (x < 0 ? 0 : (x > s->width ? s->width : x));
	clip.y0 = (int) (y < 0 ? 0 : (y > s->height ? s->height : y));
	clip.x1 = (int) (x1 < clip.x0 ? clip.x0 : (x1 > s->width ? s->width : x1));
	clip.y1 = (int) (y1 < clip.y0 ? clip.y0 : (y1 > s->height ? s->height : y1));

	return clip;
}

PHP_METHOD(PaintSurface, __construct)
{
	zend_long width, height, order = PAINT_ORDER_RGBA;
	phppaint_surface *obj = phppaint_surface_from(Z_OBJ_P(ZEND_THIS));

	ZEND_PARSE_PARAMETERS_START(2, 3)
		Z_PARAM_LONG(width)
		Z_PARAM_LONG(height)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(order)
	ZEND_PARSE_PARAMETERS_END();

	if (obj->surface.px != NULL) {
		zend_throw_error(NULL, "PaintSurface is already constructed");
		RETURN_THROWS();
	}
	if (! phppaint_sides(width, height, order)) {
		RETURN_THROWS();
	}

	obj->pixels = zend_string_alloc((size_t) width * (size_t) height * 4, 0);
	memset(ZSTR_VAL(obj->pixels), 0, ZSTR_LEN(obj->pixels) + 1);
	obj->surface.px = (uint8_t *) ZSTR_VAL(obj->pixels);
	obj->surface.width = (int) width;
	obj->surface.height = (int) height;
	obj->surface.stride = (ptrdiff_t) width * 4;
	paint_order(&obj->surface, (int) order);
}

PHP_METHOD(PaintSurface, over)
{
	zend_long address, width, height, stride, order;
	zval *owner;
	phppaint_surface *obj;

	ZEND_PARSE_PARAMETERS_START(6, 6)
		Z_PARAM_LONG(address)
		Z_PARAM_LONG(width)
		Z_PARAM_LONG(height)
		Z_PARAM_LONG(stride)
		Z_PARAM_LONG(order)
		Z_PARAM_OBJECT(owner)
	ZEND_PARSE_PARAMETERS_END();

	if (address == 0) {
		zend_argument_value_error(1, "must not be 0");
		RETURN_THROWS();
	}
	if (! phppaint_sides(width, height, order)) {
		RETURN_THROWS();
	}
	if (stride < width * 4) {
		zend_argument_value_error(4, "must be at least the width times 4");
		RETURN_THROWS();
	}

	object_init_ex(return_value, phppaint_ce_PaintSurface);
	obj = phppaint_surface_from(Z_OBJ_P(return_value));
	obj->surface.px = (uint8_t *) (uintptr_t) address;
	obj->surface.width = (int) width;
	obj->surface.height = (int) height;
	obj->surface.stride = (ptrdiff_t) stride;
	paint_order(&obj->surface, (int) order);
	ZVAL_COPY(&obj->owner, owner);
}

PHP_METHOD(PaintSurface, width)
{
	paint_surface *s;

	ZEND_PARSE_PARAMETERS_NONE();
	if ((s = phppaint_this(ZEND_THIS)) == NULL) {
		RETURN_THROWS();
	}
	RETURN_LONG(s->width);
}

PHP_METHOD(PaintSurface, height)
{
	paint_surface *s;

	ZEND_PARSE_PARAMETERS_NONE();
	if ((s = phppaint_this(ZEND_THIS)) == NULL) {
		RETURN_THROWS();
	}
	RETURN_LONG(s->height);
}

PHP_METHOD(PaintSurface, bytes)
{
	paint_surface *s;
	zend_string *out;
	size_t row;

	ZEND_PARSE_PARAMETERS_NONE();
	if ((s = phppaint_this(ZEND_THIS)) == NULL) {
		RETURN_THROWS();
	}

	row = (size_t) s->width * 4;
	out = zend_string_alloc(row * (size_t) s->height, 0);
	for (int y = 0; y < s->height; y++) {
		memcpy(ZSTR_VAL(out) + row * (size_t) y, s->px + (ptrdiff_t) y * s->stride, row);
	}
	ZSTR_VAL(out)[ZSTR_LEN(out)] = '\0';
	RETURN_NEW_STR(out);
}

PHP_METHOD(PaintSurface, clear)
{
	zend_long rgba, x, y, width, height;
	paint_surface *s;

	ZEND_PARSE_PARAMETERS_START(5, 5)
		Z_PARAM_LONG(rgba)
		Z_PARAM_LONG(x)
		Z_PARAM_LONG(y)
		Z_PARAM_LONG(width)
		Z_PARAM_LONG(height)
	ZEND_PARSE_PARAMETERS_END();

	if ((s = phppaint_this(ZEND_THIS)) == NULL) {
		RETURN_THROWS();
	}
	if (rgba < 0 || rgba > 0xFFFFFFFFLL) {
		zend_argument_value_error(1, "must be a colour 0xRRGGBBAA");
		RETURN_THROWS();
	}
	paint_clear(s, (uint32_t) rgba, phppaint_clip(s, x, y, width, height));
}

/* ---- reading ops ---- */

/* A finite number within the limit; false, with ValueError thrown, otherwise. */
static bool phppaint_number(zval *value, double *out, uint32_t op, const char *what)
{
	if (value != NULL) {
		ZVAL_DEREF(value);
	}
	if (value != NULL && Z_TYPE_P(value) == IS_LONG) {
		*out = (double) Z_LVAL_P(value);
	} else if (value != NULL && Z_TYPE_P(value) == IS_DOUBLE) {
		*out = Z_DVAL_P(value);
	} else {
		zend_value_error("Op %u: %s is not a number", op, what);
		return false;
	}
	if (! zend_finite(*out) || *out > PAINT_LIMIT || *out < -PAINT_LIMIT) {
		zend_value_error("Op %u: %s is not finite, or is past the limit", op, what);
		return false;
	}

	return true;
}

/* The numbers at places 1..count of an op. */
static bool phppaint_numbers(HashTable *op, uint32_t count, double *out, uint32_t index)
{
	static const char *const place[] = {"", "its first number", "its second number", "its third number", "its fourth number", "its fifth number", "its sixth number"};

	for (uint32_t i = 1; i <= count; i++) {
		if (! phppaint_number(zend_hash_index_find(op, i), &out[i - 1], index, place[i])) {
			return false;
		}
	}

	return true;
}

/* A list of numbers as doubles the caller frees; NULL, with ValueError thrown, when it is not one. */
static double *phppaint_flat(zval *value, uint32_t *count, uint32_t index, const char *what)
{
	HashTable *list;
	double *out;
	uint32_t n = 0;
	zval *entry;

	if (value != NULL) {
		ZVAL_DEREF(value);
	}
	if (value == NULL || Z_TYPE_P(value) != IS_ARRAY || ! zend_array_is_list(Z_ARRVAL_P(value))) {
		zend_value_error("Op %u: %s is not a list of numbers", index, what);
		return NULL;
	}
	list = Z_ARRVAL_P(value);
	out = safe_emalloc(zend_hash_num_elements(list) + 1, sizeof(double), 0);
	ZEND_HASH_FOREACH_VAL(list, entry) {
		if (! phppaint_number(entry, &out[n], index, what)) {
			efree(out);
			return NULL;
		}
		n++;
	} ZEND_HASH_FOREACH_END();
	*count = n;

	return out;
}

/* [tl, tr, br, bl] */
static bool phppaint_radii(zval *value, double r[4], uint32_t index)
{
	uint32_t n;
	double *flat = phppaint_flat(value, &n, index, "the radii");

	if (flat == NULL) {
		return false;
	}
	if (n != 4) {
		efree(flat);
		zend_value_error("Op %u: the radii are a list of four numbers: [tl, tr, br, bl]", index);
		return false;
	}
	memcpy(r, flat, 4 * sizeof(double));
	efree(flat);

	return true;
}

/* A colour, or a gradient [kind, spread, ramp, a, b, c, d, e, f]. */
static bool phppaint_paint(zval *value, paint_paint *paint, uint32_t index)
{
	HashTable *list;
	zval *kind, *spread, *ramp;

	if (value != NULL) {
		ZVAL_DEREF(value);
	}
	if (value != NULL && Z_TYPE_P(value) == IS_LONG) {
		if (Z_LVAL_P(value) < 0 || Z_LVAL_P(value) > 0xFFFFFFFFLL) {
			zend_value_error("Paint %u is not a colour 0xRRGGBBAA", index);
			return false;
		}
		paint->kind = PAINT_SOLID;
		paint->rgba = (uint32_t) Z_LVAL_P(value);
		return true;
	}
	if (value == NULL || Z_TYPE_P(value) != IS_ARRAY || ! zend_array_is_list(Z_ARRVAL_P(value)) || zend_hash_num_elements(Z_ARRVAL_P(value)) != 9) {
		zend_value_error("Paint %u is a colour, or a gradient [kind, spread, ramp, a, b, c, d, e, f]", index);
		return false;
	}
	list = Z_ARRVAL_P(value);
	kind = zend_hash_index_find(list, 0);
	spread = zend_hash_index_find(list, 1);
	ramp = zend_hash_index_find(list, 2);
	ZVAL_DEREF(kind);
	ZVAL_DEREF(spread);
	ZVAL_DEREF(ramp);

	if (Z_TYPE_P(kind) != IS_LONG || (Z_LVAL_P(kind) != PAINT_LINEAR && Z_LVAL_P(kind) != PAINT_RADIAL)) {
		zend_value_error("Paint %u: a gradient's kind is PAINT_LINEAR or PAINT_RADIAL", index);
		return false;
	}
	if (Z_TYPE_P(spread) != IS_LONG || Z_LVAL_P(spread) < PAINT_PAD || Z_LVAL_P(spread) > PAINT_REFLECT) {
		zend_value_error("Paint %u: a gradient's spread is PAINT_PAD, PAINT_REPEAT or PAINT_REFLECT", index);
		return false;
	}
	if (Z_TYPE_P(ramp) != IS_STRING || Z_STRLEN_P(ramp) != PAINT_RAMP_BYTES) {
		zend_value_error("Paint %u: a gradient's ramp is %d bytes", index, PAINT_RAMP_BYTES);
		return false;
	}
	for (uint32_t i = 0; i < 6; i++) {
		zval *n = zend_hash_index_find(list, 3 + i);

		ZVAL_DEREF(n);
		if (Z_TYPE_P(n) == IS_LONG) {
			paint->m[i] = (double) Z_LVAL_P(n);
		} else if (Z_TYPE_P(n) == IS_DOUBLE && zend_finite(Z_DVAL_P(n))) {
			paint->m[i] = Z_DVAL_P(n);
		} else {
			zend_value_error("Paint %u: a gradient ends with six finite numbers", index);
			return false;
		}
	}
	paint->kind = (int) Z_LVAL_P(kind);
	paint->spread = (int) Z_LVAL_P(spread);
	paint->ramp = (const uint8_t *) Z_STRVAL_P(ramp);

	return true;
}

/* A path's contours read and painted. */
static int phppaint_path(const paint_surface *s, paint_clip clip, HashTable *op, uint32_t index, bool antialias, const paint_paint *paint, paint_box *box)
{
	zval *contours = zend_hash_index_find(op, 1), *rule = zend_hash_index_find(op, 2), *contour;
	double **flat;
	int *points;
	uint32_t count = 0, total;
	int result;

	if (contours != NULL) {
		ZVAL_DEREF(contours);
	}
	if (rule != NULL) {
		ZVAL_DEREF(rule);
	}
	if (contours == NULL || Z_TYPE_P(contours) != IS_ARRAY || ! zend_array_is_list(Z_ARRVAL_P(contours))) {
		zend_value_error("Op %u: a path's contours are a list of lists of numbers", index);
		return -2;
	}
	if (rule == NULL || Z_TYPE_P(rule) != IS_LONG || (Z_LVAL_P(rule) != PAINT_NON_ZERO && Z_LVAL_P(rule) != PAINT_EVEN_ODD)) {
		zend_value_error("Op %u: a path's rule is PAINT_NON_ZERO or PAINT_EVEN_ODD", index);
		return -2;
	}

	total = zend_hash_num_elements(Z_ARRVAL_P(contours));
	flat = safe_emalloc(total + 1, sizeof(double *), 0);
	points = safe_emalloc(total + 1, sizeof(int), 0);
	result = 0;
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(contours), contour) {
		uint32_t n;

		flat[count] = phppaint_flat(contour, &n, index, "a contour");
		if (flat[count] == NULL) {
			result = -2;
			break;
		}
		if (n % 2 != 0) {
			efree(flat[count]);
			zend_value_error("Op %u: a contour is x0, y0, x1, y1, ...: an even count of numbers", index);
			result = -2;
			break;
		}
		points[count++] = (int) (n / 2);
	} ZEND_HASH_FOREACH_END();

	if (result == 0) {
		result = paint_path(s, clip, (const double *const *) flat, points, (int) count, (int) Z_LVAL_P(rule), antialias, paint, box);
	}
	for (uint32_t i = 0; i < count; i++) {
		efree(flat[i]);
	}
	efree(flat);
	efree(points);

	return result;
}

/* One op painted: 0, -1 when memory ran out, -2 with ValueError thrown. */
static int phppaint_op(const paint_surface *s, paint_clip clip, zval *value, uint32_t index, bool antialias, const paint_paint *paint, paint_box *box)
{
	HashTable *op;
	zval *kind;
	double n[6], r[4];

	ZVAL_DEREF(value);
	if (Z_TYPE_P(value) != IS_ARRAY || ! zend_array_is_list(Z_ARRVAL_P(value)) || (kind = zend_hash_index_find(Z_ARRVAL_P(value), 0)) == NULL || Z_TYPE_P(kind) != IS_LONG) {
		zend_value_error("Op %u is not a list that starts with its kind", index);
		return -2;
	}
	op = Z_ARRVAL_P(value);

	switch (Z_LVAL_P(kind)) {
		case PAINT_PATH:
			return phppaint_path(s, clip, op, index, antialias, paint, box);

		case PAINT_ELLIPSE:
			if (! phppaint_numbers(op, 4, n, index)) {
				return -2;
			}
			return paint_ellipse(s, clip, n[0], n[1], n[2], n[3], antialias, paint, box);

		case PAINT_RING:
			if (! phppaint_numbers(op, 5, n, index)) {
				return -2;
			}
			return paint_ring(s, clip, n[0], n[1], n[2], n[3], n[4], antialias, paint, box);

		case PAINT_ROUND_RECT:
			if (! phppaint_numbers(op, 4, n, index) || ! phppaint_radii(zend_hash_index_find(op, 5), r, index)) {
				return -2;
			}
			return paint_round_rect(s, clip, n[0], n[1], n[2], n[3], r, antialias, paint, box);

		case PAINT_ROUND_RING:
			if (! phppaint_numbers(op, 4, n, index) || ! phppaint_radii(zend_hash_index_find(op, 5), r, index)
				|| ! phppaint_number(zend_hash_index_find(op, 6), &n[4], index, "the inset")) {
				return -2;
			}
			return paint_round_ring(s, clip, n[0], n[1], n[2], n[3], r, n[4], antialias, paint, box);

		case PAINT_SHADOW:
			if (! phppaint_numbers(op, 6, n, index)) {
				return -2;
			}
			return paint_shadow(s, clip, n[0], n[1], n[2], n[3], n[4], n[5], antialias, paint, box);

		case PAINT_POLYLINE: {
			uint32_t count;
			double *flat = phppaint_flat(zend_hash_index_find(op, 1), &count, index, "the points");
			zval *closed = zend_hash_index_find(op, 2);
			int result;

			if (flat == NULL) {
				return -2;
			}
			if (count % 2 != 0) {
				efree(flat);
				zend_value_error("Op %u: the points are x0, y0, x1, y1, ...: an even count of numbers", index);
				return -2;
			}
			result = paint_polyline(s, clip, flat, (int) (count / 2), closed != NULL && zend_is_true(closed), paint, box);
			efree(flat);

			return result;
		}
	}

	zend_value_error("Op %u has a kind that is not one of the PAINT_ shapes", index);

	return -2;
}

/* count finite numbers of a list into out; false when it is not that. */
static bool phppaint_finite_list(zval *value, uint32_t count, double *out)
{
	zval *n;
	uint32_t i = 0;

	ZVAL_DEREF(value);
	if (Z_TYPE_P(value) != IS_ARRAY || !zend_array_is_list(Z_ARRVAL_P(value)) || zend_hash_num_elements(Z_ARRVAL_P(value)) != count) {
		return false;
	}
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), n) {
		ZVAL_DEREF(n);
		if (Z_TYPE_P(n) == IS_LONG) {
			out[i] = (double) Z_LVAL_P(n);
		} else if (Z_TYPE_P(n) == IS_DOUBLE && zend_finite(Z_DVAL_P(n))) {
			out[i] = Z_DVAL_P(n);
		} else {
			return false;
		}
		i++;
	} ZEND_HASH_FOREACH_END();
	return true;
}

PHP_METHOD(PaintSurface, gradient)
{
	zend_long kind, spread;
	zval *geometry, *stops, *matrix, *stop, list;
	double g[4], forward[6], m[6], *offsets;
	uint32_t *colours, count, i = 0;
	zend_string *ramp;

	ZEND_PARSE_PARAMETERS_START(5, 5)
		Z_PARAM_LONG(kind)
		Z_PARAM_LONG(spread)
		Z_PARAM_ZVAL(geometry)
		Z_PARAM_ZVAL(stops)
		Z_PARAM_ZVAL(matrix)
	ZEND_PARSE_PARAMETERS_END();

	if (kind != PAINT_LINEAR && kind != PAINT_RADIAL) {
		zend_argument_value_error(1, "must be PAINT_LINEAR or PAINT_RADIAL");
		RETURN_THROWS();
	}
	if (spread < PAINT_PAD || spread > PAINT_REFLECT) {
		zend_argument_value_error(2, "must be PAINT_PAD, PAINT_REPEAT or PAINT_REFLECT");
		RETURN_THROWS();
	}
	if (!phppaint_finite_list(geometry, kind == PAINT_LINEAR ? 4 : 3, g)) {
		zend_argument_value_error(3, kind == PAINT_LINEAR ? "must be [x0, y0, x1, y1], finite" : "must be [cx, cy, r], finite");
		RETURN_THROWS();
	}
	ZVAL_DEREF(stops);
	if (Z_TYPE_P(stops) != IS_ARRAY || !zend_array_is_list(Z_ARRVAL_P(stops)) || (count = zend_hash_num_elements(Z_ARRVAL_P(stops))) == 0) {
		zend_argument_value_error(4, "must be a list of stops [offset, 0xRRGGBBAA], at least one");
		RETURN_THROWS();
	}
	if (!phppaint_finite_list(matrix, 6, forward)) {
		zend_argument_value_error(5, "must be [a, b, c, d, e, f], finite");
		RETURN_THROWS();
	}
	offsets = safe_emalloc(count, sizeof(double), 0);
	colours = safe_emalloc(count, sizeof(uint32_t), 0);
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(stops), stop) {
		zval *offset, *colour;

		ZVAL_DEREF(stop);
		if (Z_TYPE_P(stop) != IS_ARRAY || !zend_array_is_list(Z_ARRVAL_P(stop)) || zend_hash_num_elements(Z_ARRVAL_P(stop)) != 2
			|| (offset = zend_hash_index_find(Z_ARRVAL_P(stop), 0)) == NULL || (colour = zend_hash_index_find(Z_ARRVAL_P(stop), 1)) == NULL) {
			goto bad_stop;
		}
		ZVAL_DEREF(offset);
		ZVAL_DEREF(colour);
		if (Z_TYPE_P(offset) == IS_LONG) {
			offsets[i] = (double) Z_LVAL_P(offset);
		} else if (Z_TYPE_P(offset) == IS_DOUBLE) {
			offsets[i] = Z_DVAL_P(offset);
		} else {
			goto bad_stop;
		}
		if (!(offsets[i] >= 0.0 && offsets[i] <= 1.0) || (i > 0 && offsets[i] < offsets[i - 1])
			|| Z_TYPE_P(colour) != IS_LONG || Z_LVAL_P(colour) < 0 || Z_LVAL_P(colour) > 0xFFFFFFFFLL) {
			goto bad_stop;
		}
		colours[i] = (uint32_t) Z_LVAL_P(colour);
		i++;
	} ZEND_HASH_FOREACH_END();

	if (!paint_gradient_matrix((int) kind, g, forward, m)) {
		efree(offsets);
		efree(colours);
		RETURN_NULL();
	}
	ramp = zend_string_alloc(PAINT_RAMP_BYTES, false);
	paint_ramp(offsets, colours, (int) count, (uint8_t *) ZSTR_VAL(ramp));
	ZSTR_VAL(ramp)[PAINT_RAMP_BYTES] = '\0';
	efree(offsets);
	efree(colours);

	array_init_size(&list, 9);
	add_next_index_long(&list, kind);
	add_next_index_long(&list, spread);
	add_next_index_str(&list, ramp);
	for (int k = 0; k < 6; k++) {
		add_next_index_double(&list, m[k]);
	}
	RETURN_COPY_VALUE(&list);

bad_stop:
	efree(offsets);
	efree(colours);
	zend_argument_value_error(4, "must be a list of stops [offset 0..1 not falling, 0xRRGGBBAA]");
	RETURN_THROWS();
}

PHP_METHOD(PaintSurface, fill)
{
	HashTable *ops, *paints;
	zend_long x, y, width, height;
	bool antialias = true;
	paint_surface *s;
	paint_clip clip;
	zval *op;
	uint32_t index = 0;

	ZEND_PARSE_PARAMETERS_START(6, 7)
		Z_PARAM_ARRAY_HT(ops)
		Z_PARAM_ARRAY_HT(paints)
		Z_PARAM_LONG(x)
		Z_PARAM_LONG(y)
		Z_PARAM_LONG(width)
		Z_PARAM_LONG(height)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(antialias)
	ZEND_PARSE_PARAMETERS_END();

	if ((s = phppaint_this(ZEND_THIS)) == NULL) {
		RETURN_THROWS();
	}
	if (! zend_array_is_list(ops) || ! zend_array_is_list(paints) || zend_hash_num_elements(ops) != zend_hash_num_elements(paints)) {
		zend_value_error("The ops and the paints are lists of the same length");
		RETURN_THROWS();
	}
	clip = phppaint_clip(s, x, y, width, height);

	array_init_size(return_value, zend_hash_num_elements(ops));
	ZEND_HASH_FOREACH_VAL(ops, op) {
		paint_paint paint = {0};
		paint_box box = {0};
		int result;

		if (! phppaint_paint(zend_hash_index_find(paints, index), &paint, index)) {
			RETURN_THROWS();
		}
		result = phppaint_op(s, clip, op, index, antialias, &paint, &box);
		if (result == -1) {
			zend_throw_error(NULL, "Out of memory painting op %u", index);
		}
		if (result != 0) {
			RETURN_THROWS();
		}

		if (box.any) {
			zval rect;

			array_init_size(&rect, 4);
			add_next_index_long(&rect, box.x0);
			add_next_index_long(&rect, box.y0);
			add_next_index_long(&rect, box.x1 - box.x0);
			add_next_index_long(&rect, box.y1 - box.y0);
			add_next_index_zval(return_value, &rect);
		} else {
			add_next_index_null(return_value);
		}
		index++;
	} ZEND_HASH_FOREACH_END();
}
