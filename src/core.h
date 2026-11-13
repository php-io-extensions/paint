/*
 * The painting itself, in plain C: no Zend types here. A surface is 32-bit
 * pixels, four bytes each in one of four orders, colours straight (not
 * premultiplied), top row first. A shape becomes coverage, sample line by
 * sample line, and the coverage is blended in at a colour or a gradient.
 *
 * Coordinates are doubles in pixels with whole numbers on pixel edges.
 */
#ifndef PAINT_CORE_H
#define PAINT_CORE_H

#include <stddef.h>
#include <stdint.h>

#define PAINT_PATH       1
#define PAINT_ELLIPSE    2
#define PAINT_RING       3
#define PAINT_ROUND_RECT 4
#define PAINT_ROUND_RING 5
#define PAINT_SHADOW     6
#define PAINT_POLYLINE   7

#define PAINT_NON_ZERO 0
#define PAINT_EVEN_ODD 1

#define PAINT_SOLID  0
#define PAINT_LINEAR 1
#define PAINT_RADIAL 2

#define PAINT_PAD     0
#define PAINT_REPEAT  1
#define PAINT_REFLECT 2

#define PAINT_ORDER_RGBA 0
#define PAINT_ORDER_BGRA 1
#define PAINT_ORDER_ARGB 2
#define PAINT_ORDER_ABGR 3

#define PAINT_MAX_SIDE   32767
#define PAINT_SAMPLES    16
#define PAINT_RAMP_BYTES 1024
#define PAINT_LIMIT      1073741824.0

typedef struct {
	uint8_t *px;         /* the first byte of the top row */
	int width, height;
	ptrdiff_t stride;    /* bytes from one row to the next */
	uint8_t r, g, b, a;  /* where each channel is in a pixel's four bytes */
} paint_surface;

/* Half open: columns x0..x1-1, rows y0..y1-1, inside the surface. */
typedef struct {
	int x0, y0, x1, y1;
} paint_clip;

typedef struct {
	int kind;             /* PAINT_SOLID, PAINT_LINEAR or PAINT_RADIAL */
	uint32_t rgba;        /* 0xRRGGBBAA, for PAINT_SOLID */
	const uint8_t *ramp;  /* 256 RGBA colours, for a gradient */
	int spread;
	double m[6];          /* pixel centre to gradient space: u = a*x + c*y + e, v = b*x + d*y + f */
} paint_paint;

/* What a shape touched; any is 0 when it touched nothing. */
typedef struct {
	int x0, y0, x1, y1, any;
} paint_box;

void paint_order(paint_surface *s, int order);

/* Every pixel of the rect set to the colour, not blended. */
void paint_clear(const paint_surface *s, uint32_t rgba, paint_clip rect);

/* Each answers 0, or -1 when memory ran out (nothing more is painted). */
int paint_path(const paint_surface *s, paint_clip clip, const double *const *contours, const int *points, int count, int rule, int antialias, const paint_paint *paint, paint_box *box);
int paint_ellipse(const paint_surface *s, paint_clip clip, double cx, double cy, double rx, double ry, int antialias, const paint_paint *paint, paint_box *box);
int paint_ring(const paint_surface *s, paint_clip clip, double cx, double cy, double rx, double ry, double stroke, int antialias, const paint_paint *paint, paint_box *box);
int paint_round_rect(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, const double r[4], int antialias, const paint_paint *paint, paint_box *box);
int paint_round_ring(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, const double r[4], double inset, int antialias, const paint_paint *paint, paint_box *box);
int paint_shadow(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, double radius, double blur, int antialias, const paint_paint *paint, paint_box *box);
int paint_polyline(const paint_surface *s, paint_clip clip, const double *points, int count, int closed, const paint_paint *paint, paint_box *box);

#endif
