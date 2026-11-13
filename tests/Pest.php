<?php

declare(strict_types=1);

if (! extension_loaded('paint')) {
    throw new RuntimeException('The paint extension is not loaded; run pest with -d extension=/path/to/paint.so');
}

/** The pixel at (x, y) of a surface as [r, g, b, a], whatever its byte order. */
function pixel(PaintSurface $surface, int $x, int $y, int $order = PAINT_ORDER_RGBA): array
{
    $at = ($y * $surface->width() + $x) * 4;
    $b = array_map('ord', str_split(substr($surface->bytes(), $at, 4)));

    return match ($order) {
        PAINT_ORDER_RGBA => [$b[0], $b[1], $b[2], $b[3]],
        PAINT_ORDER_BGRA => [$b[2], $b[1], $b[0], $b[3]],
        PAINT_ORDER_ARGB => [$b[1], $b[2], $b[3], $b[0]],
        PAINT_ORDER_ABGR => [$b[3], $b[2], $b[1], $b[0]],
    };
}

/** A square path op, corners (x0, y0) and (x1, y1). */
function square(float $x0, float $y0, float $x1, float $y1): array
{
    return [PAINT_PATH, [[$x0, $y0, $x1, $y0, $x1, $y1, $x0, $y1]], PAINT_NON_ZERO];
}
