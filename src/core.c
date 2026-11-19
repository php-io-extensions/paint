/*
 * Shapes into pixels.
 *
 * Anti-aliased, a pixel's coverage is the area of it inside the shape, by exact
 * area: nothing is sampled. A shape is given as its boundary, straight pieces
 * and arcs of ellipses, each with the winding it adds to everything on its
 * right. A piece that passes through a pixel adds, for the part of the row it
 * spans,
 *
 *     height - moment   to that pixel         moment = integral of (x(y) - pixel's left side) dy
 *     moment            to the pixel after
 *
 * so the running sum of a row, left to right, is each pixel's covered area
 * times its winding. For a straight piece the moment is height x the mean of
 * x; for an arc x = cx +- rx sqrt(1 - u^2), u = (y - cy) / ry, it is the area
 * under a circle, rx ry (u sqrt(1 - u^2) + asin u) / 2. The cost follows the
 * length of the boundary in pixels, not the area of the shape. A path may wind
 * any number of times and cross itself, so each row of it is settled under its
 * fill rule first (see path_settle).
 *
 * With hard edges a pixel is in when its centre is: for the line through a
 * row's centres the shape says where the line is inside it, as intervals of x.
 */
#include "core.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- pixels ---- */

void paint_order(paint_surface *s, int order)
{
	static const uint8_t at[4][4] = {{0, 1, 2, 3}, {2, 1, 0, 3}, {1, 2, 3, 0}, {3, 2, 1, 0}};

	s->r = at[order][0];
	s->g = at[order][1];
	s->b = at[order][2];
	s->a = at[order][3];
}

static inline uint8_t *pixel_at(const paint_surface *s, int x, int y)
{
	return s->px + (ptrdiff_t) y * s->stride + (ptrdiff_t) x * 4;
}

static inline void put(const paint_surface *s, uint8_t *p, int r, int g, int b, int a)
{
	p[s->r] = (uint8_t) r;
	p[s->g] = (uint8_t) g;
	p[s->b] = (uint8_t) b;
	p[s->a] = (uint8_t) a;
}

/* One pixel under a colour at alpha a (0..255): the colour's share is a/255, the pixel's the rest. */
static inline void blend(const paint_surface *s, uint8_t *p, int r, int g, int b, int a)
{
	if (a == 0) {
		return;
	}
	if (a == 255) {
		put(s, p, r, g, b, 255);
		return;
	}
	put(s, p,
		(r * a + p[s->r] * (255 - a) + 127) / 255,
		(g * a + p[s->g] * (255 - a) + 127) / 255,
		(b * a + p[s->b] * (255 - a) + 127) / 255,
		(255 * a + p[s->a] * (255 - a) + 127) / 255);
}

static void grow(paint_box *box, int row, int x, int length)
{
	if (! box->any) {
		box->x0 = x;
		box->x1 = x + length;
		box->y0 = row;
		box->y1 = row + 1;
		box->any = 1;
		return;
	}
	if (x < box->x0) box->x0 = x;
	if (x + length > box->x1) box->x1 = x + length;
	if (row < box->y0) box->y0 = row;
	if (row + 1 > box->y1) box->y1 = row + 1;
}

/* A run of pixels at one coverage (1..255), painted. */
static void run(const paint_surface *s, const paint_paint *paint, int row, int x, int length, int cover, paint_box *box)
{
	uint8_t *p = pixel_at(s, x, row);

	grow(box, row, x, length);

	if (paint->kind == PAINT_SOLID) {
		int c[4];
		int a = ((int) (paint->rgba & 0xFF) * cover + 127) / 255;

		if (a == 0) {
			return;
		}
		/* the colour by its place in a pixel's bytes; at full alpha the pixel's alpha becomes 255 */
		c[s->r] = (int) (paint->rgba >> 24);
		c[s->g] = (int) ((paint->rgba >> 16) & 0xFF);
		c[s->b] = (int) ((paint->rgba >> 8) & 0xFF);
		c[s->a] = 255;

		if (a == 255) {
			for (int i = 0; i < length; i++, p += 4) {
				p[0] = (uint8_t) c[0];
				p[1] = (uint8_t) c[1];
				p[2] = (uint8_t) c[2];
				p[3] = (uint8_t) c[3];
			}
			return;
		}

		/*
		 * (colour * a + pixel * (255 - a) + 127) / 255 for each byte, two bytes a multiply: bytes 0 and 2
		 * in one word's 16-bit halves, bytes 1 and 3 in another's. A sum y is at most 65152, and for
		 * y < 65535, y / 255 is (y + 1 + (y >> 8)) >> 8, so each half is divided without leaving its 16 bits.
		 */
		const uint32_t keep = (uint32_t) (255 - a);
		const uint32_t even = (uint32_t) (c[0] * a + 127) | ((uint32_t) (c[2] * a + 127) << 16);
		const uint32_t odd = (uint32_t) (c[1] * a + 127) | ((uint32_t) (c[3] * a + 127) << 16);

		for (int i = 0; i < length; i++, p += 4) {
			uint32_t y = ((uint32_t) p[0] | ((uint32_t) p[2] << 16)) * keep + even;
			uint32_t z = ((uint32_t) p[1] | ((uint32_t) p[3] << 16)) * keep + odd;

			y = (y + 0x00010001u + ((y >> 8) & 0x00FF00FFu)) >> 8;
			z = (z + 0x00010001u + ((z >> 8) & 0x00FF00FFu)) >> 8;
			p[0] = (uint8_t) y;
			p[2] = (uint8_t) (y >> 16);
			p[1] = (uint8_t) z;
			p[3] = (uint8_t) (z >> 16);
		}
		return;
	}

	/* A gradient: each pixel centre taken to gradient space, where t picks the colour off the ramp. */
	const double *m = paint->m;
	const double py = row + 0.5;

	/* each pixel's place worked out on its own, so where a span is split does not move a bit */
	for (int i = 0; i < length; i++, p += 4) {
		const double px = (double) (x + i) + 0.5;
		const double u = m[0] * px + m[2] * py + m[4];
		const double v = m[1] * px + m[3] * py + m[5];
		double t = paint->kind == PAINT_RADIAL ? hypot(u, v) : u;

		if (paint->spread == PAINT_PAD) {
			t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
		} else if (paint->spread == PAINT_REPEAT) {
			t = t - floor(t);
		} else {
			double w = t - 2.0 * floor(t / 2.0);
			t = w > 1.0 ? 2.0 - w : w;
		}
		if (! (t >= 0.0)) {
			t = 0.0;    /* not a number */
		}

		const uint8_t *c = paint->ramp + ((int) floor(t * 255 + 0.5)) * 4;
		int a = c[3];

		if (cover != 255) {
			a = (a * cover + 127) / 255;
		}
		blend(s, p, c[0], c[1], c[2], a);
	}
}

void paint_clear(const paint_surface *s, uint32_t rgba, paint_clip rect)
{
	for (int y = rect.y0; y < rect.y1; y++) {
		uint8_t *p = pixel_at(s, rect.x0, y);

		for (int x = rect.x0; x < rect.x1; x++, p += 4) {
			put(s, p, (int) (rgba >> 24), (int) ((rgba >> 16) & 0xFF), (int) ((rgba >> 8) & 0xFF), (int) (rgba & 0xFF));
		}
	}
}

/* ---- hard edges: the line through each row's centres ---- */

/* Where line y is inside the shape: *out is count pairs [from, to), in increasing x. Lines are asked in increasing y. */
typedef int (*across_fn)(void *shape, double y, const double **out);

static double clampd(double v, double low, double high)
{
	return v < low ? low : (v > high ? high : v);
}

/* The rows of the shape's extent, a pixel more each side, cut to the clip: a pixel is in when its centre is. */
static int scan(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, across_fn across, void *shape, const paint_paint *paint, paint_box *box)
{
	int left = (int) clampd(floor(x0) - 1, clip.x0, clip.x1);
	int right = (int) clampd(ceil(x1) + 1, clip.x0, clip.x1);
	int top = (int) clampd(floor(y0) - 1, clip.y0, clip.y1);
	int bottom = (int) clampd(ceil(y1) + 1, clip.y0, clip.y1);
	const double *spans;

	if (right <= left || bottom <= top) {
		return 0;
	}

	for (int row = top; row < bottom; row++) {
		int start = -1, end = -1;
		int n = across(shape, row + 0.5, &spans);

		for (int i = 0; i < n; i++) {
			int first = (int) clampd(ceil(spans[2 * i] - 0.5), left, right);
			int stop = (int) clampd(ceil(spans[2 * i + 1] - 0.5), left, right);

			if (stop <= first) {
				continue;
			}
			if (first == end) {
				end = stop;
				continue;
			}
			if (end > start) {
				run(s, paint, row, start, end - start, 255, box);
			}
			start = first;
			end = stop;
		}
		if (end > start) {
			run(s, paint, row, start, end - start, 255, box);
		}
	}

	return 0;
}

/* ---- anti-aliased: exact area ---- */

/*
 * One row's sums. What a piece adds left of the clip is left of every pixel in it, so it goes to the clip's
 * first column; what it adds at or past the clip's right side is never read.
 */
typedef struct {
	const paint_surface *s;
	const paint_paint *paint;
	paint_box *box;
	int left, top, right, bottom;  /* the clip */
	int base;                      /* gains[i] is column base + i */
	double *gains;                 /* what the running sum gains at each column */
	int low, high;                 /* the columns touched in this row; high < low when none */
} area_sum;

/* Room for the columns of x0..x1, a pixel more each side so rounding in a piece's x stays inside, cut to the clip. */
static int area_open(area_sum *a, const paint_surface *s, paint_clip clip, double x0, double x1, const paint_paint *paint, paint_box *box)
{
	int base = (int) clampd(floor(x0) - 1, clip.x0, clip.x1);
	int end = (int) clampd(floor(x1) + 2, clip.x0, clip.x1);

	a->s = s;
	a->paint = paint;
	a->box = box;
	a->left = clip.x0;
	a->top = clip.y0;
	a->right = clip.x1;
	a->bottom = clip.y1;
	a->base = base;
	a->low = INT_MAX;
	a->high = INT_MIN;
	a->gains = calloc((size_t) (end - base + 1), sizeof(double));

	return a->gains == NULL ? -1 : 0;
}

static inline void gain(area_sum *a, int column, double v)
{
	a->gains[column - a->base] += v;
	if (column < a->low) {
		a->low = column;
	}
	if (column > a->high) {
		a->high = column;
	}
}

/* The row's coverage, the size of its running sum held to 1, painted as runs; then the sums are cleared. */
static void area_row(area_sum *a, int row)
{
	if (a->high < a->low) {
		return;
	}

	int last = a->high < a->right - 1 ? a->high : a->right - 1;
	double sum = 0.0;
	int start = 0, value = 0;

	for (int column = a->low; column <= last; column++) {
		sum += a->gains[column - a->base];

		double c = sum < 0.0 ? -sum : sum;

		if (c > 1.0) {
			c = 1.0;
		}

		int v = (int) (c * 255.0 + 0.5);

		if (v != value) {
			if (value > 0 && column > start) {
				run(a->s, a->paint, row, start, column - start, value, a->box);
			}
			start = column;
			value = v;
		}
	}
	if (value > 0 && a->right > start) {
		run(a->s, a->paint, row, start, a->right - start, value, a->box);
	}
	memset(a->gains + (a->low - a->base), 0, (size_t) (a->high - a->low + 1) * sizeof(double));
	a->low = INT_MAX;
	a->high = INT_MIN;
}

/* A straight piece inside one row: h high, from x = xa at its top to x = xb at its bottom, adding winding w on its right. */
static void area_span(area_sum *a, double xa, double xb, double h, double w)
{
	if (h <= 0.0) {
		return;
	}

	int left = a->left, right = a->right;
	double xl, xr;

	if (xa <= xb) {
		xl = xa;
		xr = xb;
	} else {
		xl = xb;
		xr = xa;
	}
	if (xl >= right) {
		return;
	}
	if (xr <= left) {
		gain(a, left, w * h);
		return;
	}

	int il = (int) floor(xl), ir = (int) floor(xr);

	if (il == ir) {
		/* one pixel: the moment is the height times the mean distance from the pixel's left side */
		double m = h * ((xa + xb) / 2 - il);

		gain(a, il, w * (h - m));
		gain(a, il + 1, w * m);
		return;
	}

	/* several pixels: walked by x, each taking the height the piece spends in it */
	double rise = h / (xr - xl);
	int i = il;

	if (il < left) {
		gain(a, left, w * (left - xl) * rise);
		i = left;
	}

	int stop = ir < right - 1 ? ir : right - 1;

	for (; i <= stop; i++) {
		double xs = xl > i ? xl : (double) i;
		double xe = xr < i + 1 ? xr : (double) (i + 1);
		double hi = (xe - xs) * rise;
		double m = hi * ((xs + xe) / 2 - i);

		gain(a, i, w * (hi - m));
		gain(a, i + 1, w * m);
	}
}

/* The part of an arc inside one row where x runs one way, from height ya down to yb. side: -1 the left side, 1 the right. */
static void area_arc_piece(area_sum *a, double cx, double cy, double rx, double ry, double side, double w, double ya, double yb)
{
	double h = yb - ya;
	double ua = (ya - cy) / ry, ub = (yb - cy) / ry;

	ua = ua < -1.0 ? -1.0 : (ua > 1.0 ? 1.0 : ua);
	ub = ub < -1.0 ? -1.0 : (ub > 1.0 ? 1.0 : ub);

	double sa = sqrt(1.0 - ua * ua), sb = sqrt(1.0 - ub * ub);
	double xa = cx + side * rx * sa, xb = cx + side * rx * sb;
	int left = a->left, right = a->right;
	double xl, xr;

	if (xa <= xb) {
		xl = xa;
		xr = xb;
	} else {
		xl = xb;
		xr = xa;
	}
	if (xl >= right) {
		return;
	}
	if (xr <= left) {
		gain(a, left, w * h);
		return;
	}

	/* the area under the circle from the centre line to u: (u sqrt(1 - u^2) + asin u) / 2 */
	double pa = 0.5 * (ua * sa + asin(ua)), pb = 0.5 * (ub * sb + asin(ub));
	double area = side * rx * ry;
	int il = (int) floor(xl), ir = (int) floor(xr);

	if (il == ir) {
		double m = (cx - il) * h + area * (pb - pa);

		gain(a, il, w * (h - m));
		gain(a, il + 1, w * m);
		return;
	}

	/* several pixels: cut where the arc crosses a pixel's side, the height of each crossing solved from x */
	double half = (ya + yb) / 2 < cy ? -1.0 : 1.0;
	double y1, p1, y_end, p_end;

	/* the end at the smaller x first */
	if (xa <= xb) {
		y1 = ya;
		p1 = pa;
		y_end = yb;
		p_end = pb;
	} else {
		y1 = yb;
		p1 = pb;
		y_end = ya;
		p_end = pa;
	}

	int i = il;
	int first = il + 1 > left ? il + 1 : left;
	int stop = ir < right ? ir : right;

	for (int k = first; k <= stop + 1; k++) {
		double y2, p2;

		if (k <= stop) {
			double t = (k - cx) / rx;
			double q = 1.0 - t * t;
			double u = half * (q > 0.0 ? sqrt(q) : 0.0);

			y2 = cy + ry * u;
			p2 = 0.5 * (u * fabs(t) + asin(u));
		} else {
			y2 = y_end;
			p2 = p_end;
		}

		double hi = fabs(y2 - y1);

		if (i < left) {
			gain(a, left, w * hi);
		} else if (i < right) {
			double m = (cx - i) * hi + area * (y2 > y1 ? p2 - p1 : p1 - p2);

			gain(a, i, w * (hi - m));
			gain(a, i + 1, w * m);
		}
		y1 = y2;
		p1 = p2;
		i = k;
	}
}

/* A piece of a shape's boundary: a straight edge or one side of an ellipse, between two heights, cut to the clip. */
typedef struct {
	int arc, on;
	double cx, cy, rx, ry, side;   /* an arc */
	double x0, y0, slope;          /* an edge: x at y0, its top */
	double w, from, to;
	double reach_left, reach_right; /* the columns it can touch lie between these */
} area_piece;

/* One side of an ellipse between heights y_from and y_to, adding winding w on its right. */
static area_piece area_arc(paint_clip clip, double cx, double cy, double rx, double ry, double side, double w, double y_from, double y_to)
{
	area_piece p = {.arc = 1, .cx = cx, .cy = cy, .rx = rx, .ry = ry, .side = side, .w = w, .reach_left = cx - rx, .reach_right = cx + rx};

	if (rx <= 0.0 || ry <= 0.0) {
		return p;
	}

	double from = y_from > cy - ry ? y_from : cy - ry;
	double to = y_to < cy + ry ? y_to : cy + ry;

	p.from = from > (double) clip.y0 ? from : (double) clip.y0;
	p.to = to < (double) clip.y1 ? to : (double) clip.y1;
	p.on = p.from < p.to;

	return p;
}

/* A straight edge from (x0, y0) to (x1, y1): going down it adds 1 to the winding on its right, going up it takes 1 away. */
static area_piece area_edge(paint_clip clip, double x0, double y0, double x1, double y1)
{
	area_piece p = {.arc = 0, .w = 1.0, .reach_left = x0 < x1 ? x0 : x1, .reach_right = x0 < x1 ? x1 : x0};

	if (y0 == y1) {
		return p;
	}
	if (y0 > y1) {
		double t = x0;

		x0 = x1;
		x1 = t;
		t = y0;
		y0 = y1;
		y1 = t;
		p.w = -1.0;
	}
	p.x0 = x0;
	p.y0 = y0;
	p.slope = (x1 - x0) / (y1 - y0);
	p.from = y0 > (double) clip.y0 ? y0 : (double) clip.y0;
	p.to = y1 < (double) clip.y1 ? y1 : (double) clip.y1;
	p.on = p.from < p.to;

	return p;
}

/* What the piece adds in one row. */
static void area_piece_row(area_sum *a, const area_piece *p, int row)
{
	if (! p->on) {
		return;
	}

	double ya = p->from > (double) row ? p->from : (double) row;
	double yb = p->to < (double) (row + 1) ? p->to : (double) (row + 1);

	if (! p->arc) {
		area_span(a, p->x0 + (ya - p->y0) * p->slope, p->x0 + (yb - p->y0) * p->slope, yb - ya, p->w);
		return;
	}
	if (yb <= ya) {
		return;
	}
	/* x runs one way above the centre and the other way below it */
	if (ya < p->cy && p->cy < yb) {
		area_arc_piece(a, p->cx, p->cy, p->rx, p->ry, p->side, p->w, ya, p->cy);
		area_arc_piece(a, p->cx, p->cy, p->rx, p->ry, p->side, p->w, p->cy, yb);
	} else {
		area_arc_piece(a, p->cx, p->cy, p->rx, p->ry, p->side, p->w, ya, yb);
	}
}

/* Sums with room for every column the pieces that are on can touch. */
static int area_open_for(area_sum *a, const paint_surface *s, paint_clip clip, const area_piece *pieces, int count, const paint_paint *paint, paint_box *box)
{
	double left = INFINITY, right = -INFINITY;

	for (int i = 0; i < count; i++) {
		if (pieces[i].on) {
			if (pieces[i].reach_left < left) left = pieces[i].reach_left;
			if (pieces[i].reach_right > right) right = pieces[i].reach_right;
		}
	}
	if (! (left <= right)) {
		left = right = clip.x0;
	}

	return area_open(a, s, clip, left, right, paint, box);
}

/* The pieces, row by row over y0..y1 and a pixel more each side, each row's pieces in the order given, then painted. */
static void area_pieces(area_sum *a, const area_piece *pieces, int count, double y0, double y1)
{
	int top = (int) clampd(floor(y0) - 1, a->top, a->bottom);
	int bottom = (int) clampd(ceil(y1) + 1, a->top, a->bottom);

	for (int row = top; row < bottom; row++) {
		for (int i = 0; i < count; i++) {
			area_piece_row(a, &pieces[i], row);
		}
		area_row(a, row);
	}
}

/* ---- path ---- */

typedef struct {
	double ymin, ymax, x0, y0, slope;
	int direction, order;
} path_edge;

typedef struct {
	double x;
	int order, direction;
} path_crossing;

typedef struct {
	path_edge *edges;
	int count, next;
	int *active;
	int actives;
	path_crossing *crossings;
	double *spans;
	int even_odd;
} path_shape;

static int by_top(const void *a, const void *b)
{
	const path_edge *e = a, *f = b;

	if (e->ymin != f->ymin) {
		return e->ymin < f->ymin ? -1 : 1;
	}

	return e->order - f->order;
}

static int by_x(const void *a, const void *b)
{
	const path_crossing *c = a, *d = b;

	if (c->x != d->x) {
		return c->x < d->x ? -1 : 1;
	}

	return c->order - d->order;
}

static int path_across(void *shape, double y, const double **out)
{
	path_shape *p = shape;
	int n = 0, kept = 0;

	/* Sample lines arrive in increasing y, so an edge joins once and leaves once. */
	while (p->next < p->count && p->edges[p->next].ymin <= y) {
		p->active[p->actives++] = p->next++;
	}
	for (int i = 0; i < p->actives; i++) {
		if (p->edges[p->active[i]].ymax > y) {
			p->active[kept++] = p->active[i];
		}
	}
	p->actives = kept;

	for (int i = 0; i < kept; i++) {
		const path_edge *e = &p->edges[p->active[i]];

		p->crossings[i].x = e->x0 + (y - e->y0) * e->slope;
		p->crossings[i].order = e->order;
		p->crossings[i].direction = e->direction;
	}
	qsort(p->crossings, (size_t) kept, sizeof(path_crossing), by_x);

	int winding = 0;
	double start = 0.0;

	for (int i = 0; i < kept; i++) {
		int was = p->even_odd ? (winding & 1) : (winding != 0);

		winding += p->even_odd ? 1 : p->crossings[i].direction;

		int is = p->even_odd ? (winding & 1) : (winding != 0);

		if (! was && is) {
			start = p->crossings[i].x;
		} else if (was && ! is && p->crossings[i].x > start) {
			p->spans[2 * n] = start;
			p->spans[2 * n + 1] = p->crossings[i].x;
			n++;
		}
	}
	*out = p->spans;

	return n;
}

/*
 * Anti-aliased, a path is settled one row at a time. The row is cut at the heights where an edge starts or ends
 * inside it; between two cuts, the edges that span the stretch are sorted across and the winding counted from the
 * left, and only the edges where inside turns to outside or back are integrated, as +1 and -1. Where two of them
 * cross inside the stretch, it is cut at the crossings first, so between two cuts no edge crosses another and the
 * area is exact for both fill rules.
 */

typedef struct {
	double ymin, ymax, x, slope;    /* x at ymin, its top */
	int w, order;                   /* 1 going down, -1 going up; its place in the path */
} band_edge;

typedef struct {
	double xa, xb, key;             /* x at the stretch's top and bottom; their sum, what they are sorted by */
	int w, place;
} band_cross;

typedef struct {
	area_sum *a;
	band_edge *edges;
	int *active, actives;
	band_cross *across[2];          /* one for a stretch, one for the pieces it is cut into */
	double *crossings;
	size_t room;
	int even_odd, failed;
} band_path;

static int band_by_top(const void *a, const void *b)
{
	const band_edge *e = a, *f = b;

	if (e->ymin != f->ymin) {
		return e->ymin < f->ymin ? -1 : 1;
	}

	return e->order - f->order;
}

static int band_by_key(const void *a, const void *b)
{
	const band_cross *c = a, *d = b;

	if (c->key != d->key) {
		return c->key < d->key ? -1 : 1;
	}

	return c->place - d->place;
}

static int by_value(const void *a, const void *b)
{
	double x = *(const double *) a, y = *(const double *) b;

	return x < y ? -1 : (x > y ? 1 : 0);
}

/* The edges between heights from and to of one row, none starting or ending between. */
static void path_settle(band_path *p, double from, double to, int may_cross, int depth)
{
	band_cross *across = p->across[depth];
	int n = 0;

	for (int i = 0; i < p->actives; i++) {
		const band_edge *e = &p->edges[p->active[i]];

		if (e->ymin <= from && e->ymax >= to) {
			across[n].xa = e->x + (from - e->ymin) * e->slope;
			across[n].xb = e->x + (to - e->ymin) * e->slope;
			across[n].key = across[n].xa + across[n].xb;
			across[n].w = e->w;
			across[n].place = n;
			n++;
		}
	}
	if (n == 0) {
		return;
	}
	qsort(across, (size_t) n, sizeof(band_cross), band_by_key);

	int i = 1;

	for (; may_cross && i < n; i++) {
		if (across[i].xa < across[i - 1].xa || across[i].xb < across[i - 1].xb) {
			break;
		}
	}
	if (may_cross && i < n) {
		/* two edges cross inside the stretch when one is left of the other at one end and right of it at the other */
		size_t found = 0;

		for (int j = 0; j < n; j++) {
			for (int k = j + 1; k < n; k++) {
				double top = across[k].xa - across[j].xa;
				double bottom = across[k].xb - across[j].xb;

				if ((top > 0.0 && bottom < 0.0) || (top < 0.0 && bottom > 0.0)) {
					if (found + 1 >= p->room) {
						size_t room = p->room ? p->room * 2 : 64;
						double *grown = realloc(p->crossings, room * sizeof(double));

						if (grown == NULL) {
							p->failed = 1;
							return;
						}
						p->crossings = grown;
						p->room = room;
					}
					p->crossings[found++] = from + (to - from) * top / (top - bottom);
				}
			}
		}
		if (found > 0) {
			qsort(p->crossings, found, sizeof(double), by_value);
			p->crossings[found++] = to;

			double start = from;

			for (size_t c = 0; c < found; c++) {
				if (p->crossings[c] > start) {
					path_settle(p, start, p->crossings[c], 0, 1);
					start = p->crossings[c];
				}
			}

			return;
		}
	}

	double h = to - from;
	int winding = 0, inside = 0;

	for (int c = 0; c < n; c++) {
		winding += across[c].w;

		int now = p->even_odd ? (winding & 1) == 1 : winding != 0;

		if (now != inside) {
			area_span(p->a, across[c].xa, across[c].xb, h, now ? 1.0 : -1.0);
			inside = now;
		}
	}
}

static int path_area(const paint_surface *s, paint_clip clip, const double *const *contours, const int *points, int count, size_t total, int rule, const paint_paint *paint, paint_box *box)
{
	area_sum a = {0};
	band_path p = {0};
	double *cuts = NULL;
	double left = INFINITY, right = -INFINITY;
	int edges = 0, result = -1;

	p.edges = malloc(total * sizeof(band_edge));
	p.active = malloc(total * sizeof(int));
	p.across[0] = malloc(total * sizeof(band_cross));
	p.across[1] = malloc(total * sizeof(band_cross));
	cuts = malloc((total * 2 + 1) * sizeof(double));
	if (p.edges == NULL || p.active == NULL || p.across[0] == NULL || p.across[1] == NULL || cuts == NULL) {
		goto done;
	}

	for (int c = 0; c < count; c++) {
		const double *f = contours[c];
		int n = points[c];

		for (int i = 0; i < n; i++) {
			int j = i + 1 == n ? 0 : i + 1;
			double x0 = f[2 * i], y0 = f[2 * i + 1], x1 = f[2 * j], y1 = f[2 * j + 1];

			if (x0 < left) left = x0;
			if (x0 > right) right = x0;
			if (y0 == y1) {
				continue;
			}

			band_edge e = y0 < y1
				? (band_edge) {y0, y1, x0, (x1 - x0) / (y1 - y0), 1, 0}
				: (band_edge) {y1, y0, x1, (x0 - x1) / (y0 - y1), -1, 0};

			if (e.ymax > clip.y0 && e.ymin < clip.y1) {
				e.order = edges;
				p.edges[edges++] = e;
			}
		}
	}
	if (edges == 0) {
		result = 0;
		goto done;
	}
	qsort(p.edges, (size_t) edges, sizeof(band_edge), band_by_top);
	if (area_open(&a, s, clip, left, right, paint, box) != 0) {
		goto done;
	}
	p.a = &a;
	p.even_odd = rule == PAINT_EVEN_ODD;

	int next = 0;
	int lowest = (int) floor(p.edges[0].ymin);

	for (int row = lowest > clip.y0 ? lowest : clip.y0; row < clip.y1; row++) {
		double lo = (double) row, hi = (double) (row + 1);
		int cut = 0, ended = 0;

		while (next < edges && p.edges[next].ymin < hi) {
			p.active[p.actives++] = next++;
		}
		/* the heights inside the row where an edge starts or ends; an edge that ended above it leaves the list */
		for (int i = 0; i < p.actives; i++) {
			const band_edge *e = &p.edges[p.active[i]];

			if (e->ymax <= lo) {
				ended = 1;
				continue;
			}
			if (e->ymin > lo) {
				cuts[cut++] = e->ymin;
			}
			if (e->ymax < hi) {
				cuts[cut++] = e->ymax;
			}
		}
		if (ended) {
			int kept = 0;

			for (int i = 0; i < p.actives; i++) {
				if (p.edges[p.active[i]].ymax > lo) {
					p.active[kept++] = p.active[i];
				}
			}
			p.actives = kept;
		}
		if (p.actives == 0) {
			if (next >= edges) {
				break;
			}
			continue;
		}

		if (cut == 0 && p.actives == 2) {
			const band_edge *e = &p.edges[p.active[0]], *f = &p.edges[p.active[1]];

			if (e->w != f->w) {
				/*
				 * two edges the whole row high, one each way, that do not cross in it: between them the winding
				 * is 1 or -1 whichever is left, and the row takes the size of its sum
				 */
				double at = e->x + (lo - e->ymin) * e->slope, ab = e->x + (hi - e->ymin) * e->slope;
				double bt = f->x + (lo - f->ymin) * f->slope, bb = f->x + (hi - f->ymin) * f->slope;

				if ((at - bt) * (ab - bb) >= 0.0) {
					area_span(&a, at, ab, 1.0, (double) e->w);
					area_span(&a, bt, bb, 1.0, (double) f->w);
					area_row(&a, row);
					continue;
				}
			}
		}

		qsort(cuts, (size_t) cut, sizeof(double), by_value);
		cuts[cut++] = hi;

		double from = lo;

		for (int i = 0; i < cut; i++) {
			if (cuts[i] > from) {
				path_settle(&p, from, cuts[i], 1, 0);
				if (p.failed) {
					goto done;
				}
				from = cuts[i];
			}
		}
		area_row(&a, row);
	}
	result = 0;

done:
	free(a.gains);
	free(p.edges);
	free(p.active);
	free(p.across[0]);
	free(p.across[1]);
	free(p.crossings);
	free(cuts);

	return result;
}

int paint_path(const paint_surface *s, paint_clip clip, const double *const *contours, const int *points, int count, int rule, int antialias, const paint_paint *paint, paint_box *box)
{
	path_shape p = {0};
	double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
	size_t total = 0;
	int result = -1;

	for (int c = 0; c < count; c++) {
		total += (size_t) points[c];
	}
	if (total == 0) {
		return 0;
	}
	if (antialias) {
		return path_area(s, clip, contours, points, count, total, rule, paint, box);
	}

	p.edges = malloc(total * sizeof(path_edge));
	p.active = malloc(total * sizeof(int));
	p.crossings = malloc(total * sizeof(path_crossing));
	p.spans = malloc(total * 2 * sizeof(double));
	if (p.edges == NULL || p.active == NULL || p.crossings == NULL || p.spans == NULL) {
		goto done;
	}

	for (int c = 0; c < count; c++) {
		const double *f = contours[c];
		int n = points[c];

		for (int i = 0; i < n; i++) {
			int j = (i + 1) % n;
			double x0 = f[2 * i], y0 = f[2 * i + 1], x1 = f[2 * j], y1 = f[2 * j + 1];

			if (x0 < left) left = x0;
			if (x0 > right) right = x0;
			if (y0 < top) top = y0;
			if (y0 > bottom) bottom = y0;
			if (y0 == y1) {
				continue;
			}

			path_edge *e = &p.edges[p.count];

			e->ymin = y0 < y1 ? y0 : y1;
			e->ymax = y0 < y1 ? y1 : y0;
			e->x0 = x0;
			e->y0 = y0;
			e->slope = (x1 - x0) / (y1 - y0);
			e->direction = y1 > y0 ? 1 : -1;
			e->order = p.count++;
		}
	}
	if (p.count == 0) {
		result = 0;
		goto done;
	}
	qsort(p.edges, (size_t) p.count, sizeof(path_edge), by_top);
	p.even_odd = rule == PAINT_EVEN_ODD;

	result = scan(s, clip, left, top, right, bottom, path_across, &p, paint, box);

done:
	free(p.edges);
	free(p.active);
	free(p.crossings);
	free(p.spans);

	return result;
}

/* ---- ellipse and ring ---- */

typedef struct {
	double cx, cy, rx, ry, half;
	double spans[4];
} round_shape;

/* [cx - h, cx + h) with h = rx * sqrt(1 - t²), t = (y - cy) / ry. */
static int ellipse_at(double cx, double cy, double rx, double ry, double y, double *out)
{
	double t = (y - cy) / ry;
	double q = 1.0 - t * t;

	if (q <= 0) {
		return 0;
	}

	double h = rx * sqrt(q);

	out[0] = cx - h;
	out[1] = cx + h;

	return 1;
}

static int ellipse_across(void *shape, double y, const double **out)
{
	round_shape *e = shape;

	*out = e->spans;

	return ellipse_at(e->cx, e->cy, e->rx, e->ry, y, e->spans);
}

static int ring_across(void *shape, double y, const double **out)
{
	round_shape *e = shape;
	double inner[2];

	*out = e->spans;
	if (! ellipse_at(e->cx, e->cy, e->rx + e->half, e->ry + e->half, y, e->spans)) {
		return 0;
	}
	if (e->rx - e->half <= 0 || e->ry - e->half <= 0 || ! ellipse_at(e->cx, e->cy, e->rx - e->half, e->ry - e->half, y, inner)) {
		return 1;
	}
	e->spans[3] = e->spans[1];
	e->spans[1] = inner[0];
	e->spans[2] = inner[1];

	return 2;
}

/*
 * Anti-aliased: the ellipse half a stroke larger, its left side winding what is right of it in and its right side
 * winding it out again; less, where there is one, the ellipse half the stroke smaller, wound the other way.
 */
static int ellipse_area(const paint_surface *s, paint_clip clip, double cx, double cy, double rx, double ry, double half, const paint_paint *paint, paint_box *box)
{
	area_sum a;
	area_piece pieces[4];
	int count = 2;
	double outer_x = rx + half, outer_y = ry + half;

	pieces[0] = area_arc(clip, cx, cy, outer_x, outer_y, -1.0, 1.0, cy - ry - half, cy + ry + half);
	pieces[1] = area_arc(clip, cx, cy, outer_x, outer_y, 1.0, -1.0, cy - ry - half, cy + ry + half);
	if (half > 0 && rx - half > 0 && ry - half > 0) {
		pieces[2] = area_arc(clip, cx, cy, rx - half, ry - half, -1.0, -1.0, cy - ry + half, cy + ry - half);
		pieces[3] = area_arc(clip, cx, cy, rx - half, ry - half, 1.0, 1.0, cy - ry + half, cy + ry - half);
		count = 4;
	}
	if (area_open_for(&a, s, clip, pieces, count, paint, box) != 0) {
		return -1;
	}
	area_pieces(&a, pieces, count, cy - outer_y, cy + outer_y);
	free(a.gains);

	return 0;
}

int paint_ellipse(const paint_surface *s, paint_clip clip, double cx, double cy, double rx, double ry, int antialias, const paint_paint *paint, paint_box *box)
{
	round_shape e = {cx, cy, rx, ry, 0.0, {0}};

	if (rx <= 0 || ry <= 0) {
		return 0;
	}
	if (antialias) {
		return ellipse_area(s, clip, cx, cy, rx, ry, 0.0, paint, box);
	}

	return scan(s, clip, cx - rx, cy - ry, cx + rx, cy + ry, ellipse_across, &e, paint, box);
}

int paint_ring(const paint_surface *s, paint_clip clip, double cx, double cy, double rx, double ry, double stroke, int antialias, const paint_paint *paint, paint_box *box)
{
	round_shape e = {cx, cy, rx, ry, stroke / 2, {0}};

	if (rx <= 0 || ry <= 0 || stroke <= 0) {
		return 0;
	}
	if (antialias) {
		return ellipse_area(s, clip, cx, cy, rx, ry, e.half, paint, box);
	}

	return scan(s, clip, cx - (rx + e.half), cy - (ry + e.half), cx + (rx + e.half), cy + (ry + e.half), ring_across, &e, paint, box);
}

/* ---- rounded box and its ring ---- */

typedef struct {
	double x0, y0, x1, y1, r[4];
	double ix0, iy0, ix1, iy1, ir[4];
	int hollow;
	double spans[4];
} box_shape;

/* One end on a quarter circle of radius r centred (cx, cy). */
static double corner(double cx, double cy, double r, double y, double side)
{
	double t = (y - cy) / r;
	double q = 1.0 - t * t;

	if (q < 0) {
		q = 0.0;
	}

	double h = r * sqrt(q);

	return side < 0 ? cx - h : cx + h;
}

/* [left, right): above a corner's band the end is cut by that corner's quarter circle. r is tl, tr, br, bl. */
static int box_at(double x0, double y0, double x1, double y1, const double *r, double y, double *out)
{
	if (y < y0 || y >= y1) {
		return 0;
	}

	double left = x0, right = x1;

	if (y < y0 + r[0]) {
		left = corner(x0 + r[0], y0 + r[0], r[0], y, -1.0);
	} else if (y > y1 - r[3]) {
		left = corner(x0 + r[3], y1 - r[3], r[3], y, -1.0);
	}
	if (y < y0 + r[1]) {
		right = corner(x1 - r[1], y0 + r[1], r[1], y, 1.0);
	} else if (y > y1 - r[2]) {
		right = corner(x1 - r[2], y1 - r[2], r[2], y, 1.0);
	}
	if (! (right > left)) {
		return 0;
	}
	out[0] = left;
	out[1] = right;

	return 1;
}

static int box_across(void *shape, double y, const double **out)
{
	box_shape *b = shape;
	double inner[2];

	*out = b->spans;
	if (! box_at(b->x0, b->y0, b->x1, b->y1, b->r, y, b->spans)) {
		return 0;
	}
	if (! b->hollow || ! box_at(b->ix0, b->iy0, b->ix1, b->iy1, b->ir, y, inner)) {
		return 1;
	}
	b->spans[3] = b->spans[1];
	b->spans[1] = inner[0];
	b->spans[2] = inner[1];

	return 2;
}

/*
 * Anti-aliased: the two sides of a rounded box, winding w inside it. Down a side: the quarter circle of the top
 * corner, the straight part, the quarter circle of the bottom corner. r is tl, tr, br, bl. Adds up to six pieces.
 */
static int box_sides(paint_clip clip, area_piece *pieces, double x0, double y0, double x1, double y1, const double *r, double w)
{
	const double sides[2][5] = {{x0, r[0], r[3], -1.0, w}, {x1, r[1], r[2], 1.0, -w}};
	int count = 0;

	for (int i = 0; i < 2; i++) {
		double x = sides[i][0], top = sides[i][1], bottom = sides[i][2], side = sides[i][3], winding = sides[i][4];
		/* where the top corner ends and the bottom one starts */
		double upper = y0 + top < y1 ? y0 + top : y1;
		double lower = y1 - bottom > upper ? y1 - bottom : upper;

		if (top > 0) {
			pieces[count++] = area_arc(clip, x - side * top, y0 + top, top, top, side, winding, y0, upper);
		}
		if (lower > upper) {
			/* a straight edge winds 1 going down and -1 going up */
			pieces[count++] = winding > 0 ? area_edge(clip, x, upper, x, lower) : area_edge(clip, x, lower, x, upper);
		}
		if (bottom > 0 && y1 > lower) {
			pieces[count++] = area_arc(clip, x - side * bottom, y1 - bottom, bottom, bottom, side, winding, lower, y1);
		}
	}

	return count;
}

/* The box, less the inner one wound the other way when there is one. */
static int box_area(const paint_surface *s, paint_clip clip, const box_shape *b, const paint_paint *paint, paint_box *box)
{
	area_sum a;
	area_piece pieces[12];
	int count;

	count = box_sides(clip, pieces, b->x0, b->y0, b->x1, b->y1, b->r, 1.0);
	if (b->hollow) {
		count += box_sides(clip, pieces + count, b->ix0, b->iy0, b->ix1, b->iy1, b->ir, -1.0);
	}
	if (area_open_for(&a, s, clip, pieces, count, paint, box) != 0) {
		return -1;
	}
	area_pieces(&a, pieces, count, b->y0, b->y1);
	free(a.gains);

	return 0;
}

int paint_round_rect(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, const double r[4], int antialias, const paint_paint *paint, paint_box *box)
{
	box_shape b = {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1, .r = {r[0], r[1], r[2], r[3]}};

	if (x1 <= x0 || y1 <= y0) {
		return 0;
	}
	if (antialias) {
		return box_area(s, clip, &b, paint, box);
	}

	return scan(s, clip, x0, y0, x1, y1, box_across, &b, paint, box);
}

int paint_round_ring(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, const double r[4], double inset, int antialias, const paint_paint *paint, paint_box *box)
{
	box_shape b = {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1, .r = {r[0], r[1], r[2], r[3]}};

	if (x1 <= x0 || y1 <= y0 || inset <= 0) {
		return 0;
	}
	/* the inner box and its radii, max(0, r - inset) */
	b.ix0 = x0 + inset;
	b.iy0 = y0 + inset;
	b.ix1 = x1 - inset;
	b.iy1 = y1 - inset;
	for (int i = 0; i < 4; i++) {
		b.ir[i] = r[i] - inset > 0.0 ? r[i] - inset : 0.0;
	}
	b.hollow = b.ix1 > b.ix0 && b.iy1 > b.iy0;
	if (antialias) {
		return box_area(s, clip, &b, paint, box);
	}

	return scan(s, clip, x0, y0, x1, y1, box_across, &b, paint, box);
}

/* ---- shadow ---- */

/* Signed distance from (dx, dy), measured from the box centre, to the box of half sizes hx, hy rounded by r. */
static double round_distance(double dx, double dy, double hx, double hy, double r)
{
	double qx = fabs(dx) - hx + r, qy = fabs(dy) - hy + r;
	double ox = qx > 0 ? qx : 0.0, oy = qy > 0 ? qy : 0.0;
	double in = qx > qy ? qx : qy;

	return (in < 0 ? in : 0.0) + sqrt(ox * ox + oy * oy) - r;
}

/* Coverage at distance d: 1 - smoothstep(-blur, blur, d); with no blur, in when d < 0. */
static int shade(double d, double blur, int hard)
{
	double c;

	if (blur > 0) {
		double t = clampd((d + blur) / (2 * blur), 0.0, 1.0);

		c = 1.0 - t * t * (3 - 2 * t);
	} else {
		c = d < 0 ? 1.0 : 0.0;
	}

	int v = (int) floor(c * 255 + 0.5);

	return hard ? (v >= 128 ? 255 : 0) : v;
}

int paint_shadow(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, double radius, double blur, int antialias, const paint_paint *paint, paint_box *box)
{
	if (x1 <= x0 || y1 <= y0) {
		return 0;
	}

	int left = (int) clampd(floor(x0 - blur), clip.x0, clip.x1);
	int right = (int) clampd(ceil(x1 + blur), clip.x0, clip.x1);
	int top = (int) clampd(floor(y0 - blur), clip.y0, clip.y1);
	int bottom = (int) clampd(ceil(y1 + blur), clip.y0, clip.y1);

	if (right <= left || bottom <= top) {
		return 0;
	}

	double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hx = (x1 - x0) / 2, hy = (y1 - y0) / 2;
	/* columns this far inside both sides see a distance that depends on y alone: one run */
	int band = (int) ceil(radius + blur) + 2;
	int middle_from = (int) ceil(x0) + band, middle_to = (int) floor(x1) - band;

	for (int row = top; row < bottom; row++) {
		double py = row + 0.5;
		int start = left, value = -1;

		for (int col = left; col < right; col++) {
			int middle = col == middle_from && middle_to > middle_from;
			int v = shade(round_distance((middle ? cx : col + 0.5) - cx, py - cy, hx, hy, radius), blur, ! antialias);

			if (v != value) {
				if (value > 0) {
					run(s, paint, row, start, col - start, value, box);
				}
				start = col;
				value = v;
			}
			if (middle) {
				col = (middle_to < right ? middle_to : right) - 1;
			}
		}
		if (value > 0) {
			run(s, paint, row, start, right - start, value, box);
		}
	}

	return 0;
}

/* ---- one-pixel lines ---- */

typedef struct {
	int y, x;
} line_pixel;

typedef struct {
	line_pixel *pixels;
	size_t count, room;
} line_pixels;

static int by_place(const void *a, const void *b)
{
	const line_pixel *p = a, *q = b;

	return p->y != q->y ? (p->y < q->y ? -1 : 1) : (p->x < q->x ? -1 : (p->x > q->x ? 1 : 0));
}

/* The pixels of one line inside the clip: along the longer axis from its lower end, step i moves floor((2*minor*i + major) / (2*major)) across. */
static int segment(line_pixels *out, paint_clip clip, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
	int steep = llabs(y1 - y0) > llabs(x1 - x0);
	int64_t a0 = steep ? y0 : x0, b0 = steep ? x0 : y0, a1 = steep ? y1 : x1, b1 = steep ? x1 : y1;

	if (a0 > a1) {
		int64_t t = a0; a0 = a1; a1 = t;
		t = b0; b0 = b1; b1 = t;
	}

	int64_t low = steep ? clip.y0 : clip.x0, high = steep ? clip.y1 : clip.x1;
	int64_t minor_low = steep ? clip.x0 : clip.y0, minor_high = steep ? clip.x1 : clip.y1;
	int64_t major = a1 - a0, minor = llabs(b1 - b0), sign = b1 >= b0 ? 1 : -1;

	for (int64_t a = a0 > low ? a0 : low, last = a1 < high - 1 ? a1 : high - 1; a <= last; a++) {
		int64_t b = b0 + sign * (major == 0 ? 0 : (2 * minor * (a - a0) + major) / (2 * major));

		if (b < minor_low || b >= minor_high) {
			continue;
		}
		if (out->count == out->room) {
			size_t room = out->room ? out->room * 2 : 256;
			line_pixel *grown = realloc(out->pixels, room * sizeof(line_pixel));

			if (grown == NULL) {
				return -1;
			}
			out->pixels = grown;
			out->room = room;
		}
		out->pixels[out->count].y = (int) (steep ? a : b);
		out->pixels[out->count].x = (int) (steep ? b : a);
		out->count++;
	}

	return 0;
}

int paint_polyline(const paint_surface *s, paint_clip clip, const double *points, int count, int closed, const paint_paint *paint, paint_box *box)
{
	line_pixels px = {0};
	int result = 0;

	if (count == 0) {
		return 0;
	}
#define AT(i, k) ((int64_t) floor(points[2 * (i) + (k)]))
	if (count == 1) {
		result = segment(&px, clip, AT(0, 0), AT(0, 1), AT(0, 0), AT(0, 1));
	}
	for (int i = 0; result == 0 && i < count - 1; i++) {
		result = segment(&px, clip, AT(i, 0), AT(i, 1), AT(i + 1, 0), AT(i + 1, 1));
	}
	if (result == 0 && closed && count > 1) {
		result = segment(&px, clip, AT(count - 1, 0), AT(count - 1, 1), AT(0, 0), AT(0, 1));
	}
#undef AT

	if (result == 0 && px.count > 0) {
		qsort(px.pixels, px.count, sizeof(line_pixel), by_place);

		/* each row's pixels as runs of neighbours, a pixel two lines share painted once */
		size_t i = 0;

		while (i < px.count) {
			int y = px.pixels[i].y, start = px.pixels[i].x, end = start + 1;

			for (i++; i < px.count && px.pixels[i].y == y && px.pixels[i].x <= end; i++) {
				if (px.pixels[i].x == end) {
					end++;
				}
			}
			run(s, paint, y, start, end - start, 255, box);
		}
	}
	free(px.pixels);

	return result;
}


/* ----------------------------------------------------------------- gradients */

void paint_ramp(const double *offsets, const uint32_t *colours, int count, uint8_t ramp[PAINT_RAMP_BYTES])
{
	int k = 0, last = count - 1;

	for (int i = 0; i < 256; i++) {
		double x = (double) i / 255.0;
		uint8_t *out = ramp + 4 * i;

		while (k < last && offsets[k + 1] <= x) {
			k++;
		}
		if (x < offsets[0] || k == last) {
			uint32_t c = x < offsets[0] ? colours[0] : colours[last];

			out[0] = (uint8_t) (c >> 24);
			out[1] = (uint8_t) (c >> 16);
			out[2] = (uint8_t) (c >> 8);
			out[3] = (uint8_t) c;
			continue;
		}
		double u = (x - offsets[k]) / (offsets[k + 1] - offsets[k]);

		for (int ch = 0; ch < 4; ch++) {
			double a = (double) ((colours[k] >> (24 - 8 * ch)) & 0xFF);
			double b = (double) ((colours[k + 1] >> (24 - 8 * ch)) & 0xFF);

			out[ch] = (uint8_t) (int) floor(a + (b - a) * u + 0.5);
		}
	}
}

int paint_gradient_matrix(int kind, const double *geometry, const double forward[6], double m[6])
{
	const double a = forward[0], b = forward[1], c = forward[2], d = forward[3], e = forward[4], f = forward[5];
	double det = a * d - b * c, inv[6], g[6];

	if (det == 0.0 || !isfinite(det)) {
		return 0;
	}
	/* the transform undone: x = inv0 X + inv2 Y + inv4, y = inv1 X + inv3 Y + inv5 */
	inv[0] = d / det;
	inv[1] = -b / det;
	inv[2] = -c / det;
	inv[3] = a / det;
	inv[4] = (c * f - d * e) / det;
	inv[5] = (b * e - a * f) / det;

	if (kind == PAINT_LINEAR) {
		/* u along the line from (x0, y0), 1 at (x1, y1); v across it, in the same units */
		double dx = geometry[2] - geometry[0], dy = geometry[3] - geometry[1], l = dx * dx + dy * dy;

		if (!(l > 0.0) || !isfinite(l)) {
			return 0;
		}
		g[0] = dx / l;
		g[1] = -dy / l;
		g[2] = dy / l;
		g[3] = dx / l;
		g[4] = -(geometry[0] * dx + geometry[1] * dy) / l;
		g[5] = (geometry[0] * dy - geometry[1] * dx) / l;
	} else {
		/* (u, v) from the centre, 1 at the radius */
		double r = geometry[2];

		if (!(r > 0.0) || !isfinite(r)) {
			return 0;
		}
		g[0] = 1.0 / r;
		g[1] = 0.0;
		g[2] = 0.0;
		g[3] = 1.0 / r;
		g[4] = -geometry[0] / r;
		g[5] = -geometry[1] / r;
	}
	/* the gradient's map after the transform undone */
	m[0] = g[0] * inv[0] + g[2] * inv[1];
	m[1] = g[1] * inv[0] + g[3] * inv[1];
	m[2] = g[0] * inv[2] + g[2] * inv[3];
	m[3] = g[1] * inv[2] + g[3] * inv[3];
	m[4] = (g[0] * inv[4] + g[2] * inv[5]) + g[4];
	m[5] = (g[1] * inv[4] + g[3] * inv[5]) + g[5];
	for (int i = 0; i < 6; i++) {
		if (!isfinite(m[i])) {
			return 0;
		}
	}
	return 1;
}
