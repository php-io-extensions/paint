<?php

declare(strict_types=1);

it('refuses an op, a paint or a number that is not one', function (array $ops, array $paints): void {
    $surface = new PaintSurface(4, 4);

    expect(fn () => $surface->fill($ops, $paints, 0, 0, 4, 4))->toThrow(ValueError::class);
})->with([
    'an op that is not a list' => [['path'], [0xFF]],
    'a kind that is not one' => [[[99, 1, 2]], [0xFF]],
    'a path with no rule' => [[[PAINT_PATH, [[0, 0, 1, 0, 1, 1]]]], [0xFF]],
    'a contour of odd length' => [[[PAINT_PATH, [[0, 0, 1]], PAINT_NON_ZERO]], [0xFF]],
    'a number that is a string' => [[[PAINT_ELLIPSE, '2', 2, 1, 1]], [0xFF]],
    'a number that is not finite' => [[[PAINT_ELLIPSE, NAN, 2, 1, 1]], [0xFF]],
    'a number past the limit' => [[[PAINT_ELLIPSE, PAINT_LIMIT * 2, 2, 1, 1]], [0xFF]],
    'three radii' => [[[PAINT_ROUND_RECT, 0, 0, 2, 2, [1, 1, 1]]], [0xFF]],
    'more ops than paints' => [[square(0, 0, 1, 1), square(0, 0, 1, 1)], [0xFF]],
    'a colour past 32 bits' => [[square(0, 0, 1, 1)], [0x1FFFFFFFF]],
    'a gradient with a short ramp' => [[square(0, 0, 1, 1)], [[PAINT_LINEAR, PAINT_PAD, 'abc', 1, 0, 0, 1, 0, 0]]],
    'a gradient with a kind that is not one' => [[square(0, 0, 1, 1)], [[7, PAINT_PAD, str_repeat("\0", PAINT_RAMP_BYTES), 1, 0, 0, 1, 0, 0]]],
    'a gradient of eight entries' => [[square(0, 0, 1, 1)], [[PAINT_LINEAR, PAINT_PAD, str_repeat("\0", PAINT_RAMP_BYTES), 1, 0, 0, 1, 0]]],
]);

it('has painted the ops before the one it refuses', function (): void {
    $surface = new PaintSurface(2, 1);

    expect(fn () => $surface->fill([square(0, 0, 1, 1), [99]], [0xFFFFFFFF, 0xFFFFFFFF], 0, 0, 2, 1))->toThrow(ValueError::class)
        ->and(pixel($surface, 0, 0)[3])->toBe(255);
});

it('declares the constants and methods its stubs do', function (): void {
    expect(PAINT_PATH)->toBe(1)
        ->and(PAINT_POLYLINE)->toBe(7)
        ->and(PAINT_RAMP_BYTES)->toBe(1024)
        ->and(PAINT_LIMIT)->toBe(1073741824.0)
        ->and(array_map(fn (ReflectionMethod $m): string => $m->getName(), (new ReflectionClass(PaintSurface::class))->getMethods()))
        ->toBe(['__construct', 'over', 'gradient', 'width', 'height', 'bytes', 'clear', 'fill']);
});
