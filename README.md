<div align="center" id="header">

<h1>ext-paint</h1>

<h3>Shapes painted straight into pixels in C, for PHP 8.4+</h3>

[![CI](https://img.shields.io/github/actions/workflow/status/php-io-extensions/paint/ci.yml?style=flat-square&label=CI)](https://github.com/php-io-extensions/paint/actions/workflows/ci.yml)
[![Packagist](https://img.shields.io/packagist/v/php-io-extensions/paint?style=flat-square)](https://packagist.org/packages/php-io-extensions/paint)
[![PHP](https://img.shields.io/packagist/dependency-v/php-io-extensions/paint/php?style=flat-square&label=PHP)](https://packagist.org/packages/php-io-extensions/paint)
[![License](https://img.shields.io/github/license/php-io-extensions/paint?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-linux_%7C_macos-blue?style=flat-square&logo=linux&logoColor=white)](#requirements)
[![OKF](https://img.shields.io/badge/OKF-0.2-6f42c1?style=flat-square&label=knowledge%20bundle)](.okf/index.md)
[![PIE](https://img.shields.io/badge/PIE-installable-4f5b93?style=flat-square&logo=php&logoColor=white)](https://github.com/php/pie)

</div>

Drawing a filled shape means deciding, for every pixel near it, how much of that pixel the shape covers, and then mixing the shape's colour into the pixel by that amount. In PHP that is a loop over rows and a walk along each edge through the pixels it crosses, with float arithmetic in the middle and four byte writes at the end. ext-paint works the covered area out exactly, from the shape's edges and curves, instead of sampling it.

<b>ext-paint</b> runs those loops in C. You hand it a list of shapes and their paints; it works out the coverage and blends them into 32-bit pixels, its own or memory you already hold. It has no dependency, no window, no fonts and no drawing state: shapes go in, pixels change, and the same input gives the same bytes on every platform.

## Basic usage

```php
$surface = new PaintSurface(320, 200);
$surface->clear(0x101820FF, 0, 0, 320, 200);

$surface->fill(
    [
        [PAINT_PATH, [[40, 160, 160, 30, 280, 160]], PAINT_NON_ZERO],    // a triangle
        [PAINT_ELLIPSE, 160, 110, 40, 40],
        [PAINT_ROUND_RECT, 20, 170, 300, 190, [8, 8, 8, 8]],
    ],
    [0xF2AA4CFF, 0xFFFFFF80, 0x2E86DEFF],    // one paint an op, 0xRRGGBBAA
    0, 0, 320, 200,                          // paint nothing outside this rect
);

$rgba = $surface->bytes();    // 320 * 200 * 4 bytes, rows top first
```

### Examples

A gradient is a paint: a ramp of 256 colours and six numbers that take a pixel to a position along it.

```php
$ramp = '';
for ($i = 0; $i < 256; $i++) {
    $ramp .= chr($i).chr(0).chr(255 - $i)."\xFF";    // blue to red
}

// u = px / 320: left edge is the ramp's start, right edge its end
$surface->fill([[PAINT_ROUND_RECT, 0, 0, 320, 200, [0, 0, 0, 0]]], [[PAINT_LINEAR, PAINT_PAD, $ramp, 1 / 320, 0, 0, 0, 0, 0]], 0, 0, 320, 200);
```

Paint into pixels another extension holds, with nothing copied:

```php
$surface = PaintSurface::over($address, $width, $height, $stride, PAINT_ORDER_BGRA, $owner);
$boxes = $surface->fill($ops, $paints, 0, 0, $width, $height);    // the rect each op covered, or null
```

`over()` trusts the address. It is for a caller that already holds native memory; `$owner` is kept alive for as long as the surface is.

Hard edges, where a pixel is in when its centre is:

```php
$surface->fill($ops, $paints, 0, 0, 320, 200, antialias: false);
```

## Requirements

* PHP 8.4 or later, NTS or ZTS
* Linux or macOS
* A C compiler and the PHP development headers. No system library.

## Installation

### With PHP PIE (recommended)

```bash
pie install php-io-extensions/paint
```

### Manually (install script)

```bash
./install-macos.sh            # Homebrew php@8.4 and php@8.4-zts
./install-debian-trixie.sh    # Debian trixie, Raspberry Pi OS
```

### Manually (phpize)

```bash
phpize
./configure --enable-paint
make
sudo make install
```

Then add `extension=paint` to your `php.ini`.

### Verify

```bash
php -r 'var_dump(extension_loaded("paint"));'
```

## API

The full reference, with how coverage and blending are worked step by step, is in [`.okf/api/surface.md`](.okf/api/surface.md).

### PaintSurface

| Method | What it does |
|---|---|
| `new PaintSurface(width, height, order = PAINT_ORDER_RGBA)` | A surface with its own pixels, every byte 0 |
| `PaintSurface::over(address, width, height, stride, order, owner)` | A surface over memory something else holds |
| `width()`, `height()` | The sides |
| `bytes()` | The pixels, rows tight |
| `clear(rgba, x, y, width, height)` | Set a rect to a colour, not blended |
| `fill(ops, paints, x, y, width, height, antialias = true)` | Paint each op with its paint inside a rect |

### Ops

| Kind | Op |
|---|---|
| `PAINT_PATH` | `[kind, contours, rule]`, a contour a flat list `x0, y0, x1, y1, ...` |
| `PAINT_ELLIPSE` | `[kind, cx, cy, rx, ry]` |
| `PAINT_RING` | `[kind, cx, cy, rx, ry, stroke]` |
| `PAINT_ROUND_RECT` | `[kind, x0, y0, x1, y1, [tl, tr, br, bl]]` |
| `PAINT_ROUND_RING` | `[kind, x0, y0, x1, y1, [tl, tr, br, bl], inset]` |
| `PAINT_SHADOW` | `[kind, x0, y0, x1, y1, radius, blur]` |
| `PAINT_POLYLINE` | `[kind, [x0, y0, x1, y1, ...], closed]`, one-pixel lines |

A wide stroke is a path: work out its outline and fill it.

### Paints

A colour `0xRRGGBBAA`, or a gradient `[kind, spread, ramp, a, b, c, d, e, f]` with `kind` `PAINT_LINEAR` or `PAINT_RADIAL` and `spread` `PAINT_PAD`, `PAINT_REPEAT` or `PAINT_REFLECT`.

### What is refused

An op, a paint or a number that is not one is a `ValueError` naming the op. `fill()` paints one op at a time, so the ops before a refused one have been painted.

## Testing

```bash
composer install
vendor/bin/pest
```

## License

MIT. See [LICENSE](LICENSE).
