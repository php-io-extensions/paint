/*
 * Shapes into pixels. Every shape is scanned the same way: for each sample
 * line the shape says where the line is inside it, as intervals of x. With
 * hard edges there is one sample line a row, through the pixel centres, and
 * a pixel is in when its centre is. Anti-aliased, there are PAINT_SAMPLES
 * lines a row; each interval adds the exact width it covers of each pixel,
 * so a pixel's coverage is the area of it inside the shape.
 */
#include "core.h"

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
	double px = x + 0.5, py = row + 0.5;
	double u = m[0] * px + m[2] * py + m[4];
	double v = m[1] * px + m[3] * py + m[5];

	for (int i = 0; i < length; i++, p += 4, u += m[0], v += m[1]) {
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

/* ---- the scan ---- */

/* Where sample line y is inside the shape: *out is count pairs [from, to), in increasing x. Lines are asked in increasing y. */
typedef int (*across_fn)(void *shape, double y, const double **out);

static double clampd(double v, double low, double high)
{
	return v < low ? low : (v > high ? high : v);
}

/* The rows and columns of the shape's extent, a pixel more each side, cut to the clip. */
static int scan(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, across_fn across, void *shape, int antialias, const paint_paint *paint, paint_box *box)
{
	int left = (int) clampd(floor(x0) - 1, clip.x0, clip.x1);
	int right = (int) clampd(ceil(x1) + 1, clip.x0, clip.x1);
	int top = (int) clampd(floor(y0) - 1, clip.y0, clip.y1);
	int bottom = (int) clampd(ceil(y1) + 1, clip.y0, clip.y1);
	const double *spans;

	if (right <= left || bottom <= top) {
		return 0;
	}

	if (! antialias) {
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

	int width = right - left;
	const double weight = 1.0 / PAINT_SAMPLES;
	/* cover: what each pixel's own partial ends add. delta: where a run of whole pixels starts and stops. */
	double *cover = calloc((size_t) width, sizeof(double));
	double *delta = calloc((size_t) width + 1, sizeof(double));

	if (cover == NULL || delta == NULL) {
		free(cover);
		free(delta);
		return -1;
	}

	for (int row = top; row < bottom; row++) {
		int low = width, high = -1;

		for (int k = 0; k < PAINT_SAMPLES; k++) {
			int n = across(shape, row + (k + 0.5) / PAINT_SAMPLES, &spans);

			for (int i = 0; i < n; i++) {
				double a = spans[2 * i], b = spans[2 * i + 1];

				if (a < left) a = left;
				if (b > right) b = right;
				if (b <= a) {
					continue;
				}

				int ia = (int) floor(a), ib = (int) floor(b);

				if (ia == ib) {
					cover[ia - left] += (b - a) * weight;
				} else {
					cover[ia - left] += ((ia + 1) - a) * weight;
					if (ia + 1 < ib) {
						delta[ia + 1 - left] += weight;
						delta[ib - left] -= weight;
					}
					if (b > ib) {
						cover[ib - left] += (b - ib) * weight;
					}
				}
				if (ia - left < low) low = ia - left;
				if (ib - left > high) high = ib - left;
			}
		}
		if (high < 0) {
			continue;
		}

		int last = high < width - 1 ? high : width - 1;
		double running = 0.0;
		int start = -1, value = 0;

		int flat = 0;    /* the pixel before held no partial end: its coverage was running alone */

		for (int i = low; i <= last; i++) {
			int v;

			if (flat && delta[i] == 0.0 && cover[i] == 0.0) {
				v = value;    /* the same sum as the pixel before, so the same byte */
			} else {
				running += delta[i];
				v = (int) floor(clampd(cover[i] + running, 0.0, 1.0) * 255 + 0.5);
				flat = cover[i] == 0.0;
			}

			if (! (start >= 0 && v == value)) {
				if (start >= 0 && value > 0) {
					run(s, paint, row, left + start, i - start, value, box);
				}
				start = i;
				value = v;
			}
		}
		if (value > 0) {
			run(s, paint, row, left + start, last + 1 - start, value, box);
		}

		memset(cover + low, 0, (size_t) (last - low + 1) * sizeof(double));
		memset(delta + low, 0, (size_t) (high - low + 1) * sizeof(double));
	}

	free(cover);
	free(delta);

	return 0;
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

	result = scan(s, clip, left, top, right, bottom, path_across, &p, antialias, paint, box);

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

int paint_ellipse(const paint_surface *s, paint_clip clip, double cx, double cy, double rx, double ry, int antialias, const paint_paint *paint, paint_box *box)
{
	round_shape e = {cx, cy, rx, ry, 0.0, {0}};

	if (rx <= 0 || ry <= 0) {
		return 0;
	}

	return scan(s, clip, cx - rx, cy - ry, cx + rx, cy + ry, ellipse_across, &e, antialias, paint, box);
}

int paint_ring(const paint_surface *s, paint_clip clip, double cx, double cy, double rx, double ry, double stroke, int antialias, const paint_paint *paint, paint_box *box)
{
	round_shape e = {cx, cy, rx, ry, stroke / 2, {0}};

	if (rx <= 0 || ry <= 0 || stroke <= 0) {
		return 0;
	}

	return scan(s, clip, cx - (rx + e.half), cy - (ry + e.half), cx + (rx + e.half), cy + (ry + e.half), ring_across, &e, antialias, paint, box);
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

int paint_round_rect(const paint_surface *s, paint_clip clip, double x0, double y0, double x1, double y1, const double r[4], int antialias, const paint_paint *paint, paint_box *box)
{
	box_shape b = {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1, .r = {r[0], r[1], r[2], r[3]}};

	if (x1 <= x0 || y1 <= y0) {
		return 0;
	}

	return scan(s, clip, x0, y0, x1, y1, box_across, &b, antialias, paint, box);
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

	return scan(s, clip, x0, y0, x1, y1, box_across, &b, antialias, paint, box);
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
