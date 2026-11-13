<?php

declare(strict_types=1);

it('starts with every byte 0, at the size it was given', function (): void {
    $surface = new PaintSurface(5, 3);

    expect($surface->width())->toBe(5)
        ->and($surface->height())->toBe(3)
        ->and($surface->bytes())->toBe(str_repeat("\0", 5 * 3 * 4));
});

it('refuses a side or a byte order it does not have', function (Closure $make): void {
    expect($make)->toThrow(ValueError::class);
})->with([
    'no width' => [fn () => new PaintSurface(0, 4)],
    'a height past the limit' => [fn () => new PaintSurface(4, PAINT_MAX_SIDE + 1)],
    'an order that is not one' => [fn () => new PaintSurface(4, 4, 9)],
]);

it('is final, and cannot be cloned or serialized', function (): void {
    $surface = new PaintSurface(2, 2);

    expect((new ReflectionClass(PaintSurface::class))->isFinal())->toBeTrue()
        ->and(fn () => clone $surface)->toThrow(Error::class)
        ->and(fn () => serialize($surface))->toThrow(Exception::class);
});

it('refuses to be constructed twice', function (): void {
    $surface = new PaintSurface(2, 2);

    expect(fn () => $surface->__construct(2, 2))->toThrow(Error::class);
});

it('clears a rect to a colour without blending, cut to the surface', function (): void {
    $surface = new PaintSurface(4, 3);
    $surface->clear(0x11223344, 1, 1, 10, 10);

    expect(pixel($surface, 0, 0))->toBe([0, 0, 0, 0])
        ->and(pixel($surface, 1, 1))->toBe([0x11, 0x22, 0x33, 0x44])
        ->and(pixel($surface, 3, 2))->toBe([0x11, 0x22, 0x33, 0x44])
        ->and(pixel($surface, 0, 2))->toBe([0, 0, 0, 0]);

    // a second clear replaces: the alpha is the colour's, not a blend
    $surface->clear(0xFFFFFF00, 1, 1, 1, 1);
    expect(pixel($surface, 1, 1))->toBe([255, 255, 255, 0]);
});

it('holds each channel where the byte order says', function (int $order): void {
    $surface = new PaintSurface(2, 1, $order);
    $surface->clear(0x0A0B0C0D, 0, 0, 2, 1);
    $surface->fill([square(1, 0, 2, 1)], [0x102030FF], 0, 0, 2, 1);

    expect(pixel($surface, 0, 0, $order))->toBe([0x0A, 0x0B, 0x0C, 0x0D])
        ->and(pixel($surface, 1, 0, $order))->toBe([0x10, 0x20, 0x30, 0xFF]);
})->with([PAINT_ORDER_RGBA, PAINT_ORDER_BGRA, PAINT_ORDER_ARGB, PAINT_ORDER_ABGR]);

it('paints into memory another object owns, rows a stride apart, and keeps the owner alive', function (): void {
    // a 3 x 2 surface inside rows 20 bytes long, in memory an FFI buffer owns
    if (! extension_loaded('ffi')) {
        $this->markTestSkipped('ext-ffi is not loaded.');
    }
    $ffi = FFI::cdef();
    $buffer = $ffi->new('unsigned char[40]');
    $address = $ffi->cast('uintptr_t', FFI::addr($buffer))->cdata;
    $owner = new class($buffer) { public function __construct(public $buffer) {} };

    $surface = PaintSurface::over($address, 3, 2, 20, PAINT_ORDER_RGBA, $owner);
    unset($buffer);
    $surface->fill([square(0, 0, 3, 2)], [0x01020304 | 0xFF], 0, 0, 3, 2);

    $bytes = FFI::string($owner->buffer, 40);
    expect(substr($bytes, 0, 12))->toBe(str_repeat("\x01\x02\x03\xFF", 3))
        ->and(substr($bytes, 12, 8))->toBe(str_repeat("\0", 8))      // the padding is not touched
        ->and(substr($bytes, 20, 12))->toBe(str_repeat("\x01\x02\x03\xFF", 3))
        ->and($surface->bytes())->toBe(str_repeat("\x01\x02\x03\xFF", 6));
});

it('refuses memory at 0, or rows shorter than the pixels', function (Closure $make): void {
    expect($make)->toThrow(ValueError::class);
})->with([
    'address 0' => [fn () => PaintSurface::over(0, 2, 2, 8, PAINT_ORDER_RGBA, new stdClass())],
    'a short stride' => [fn () => PaintSurface::over(4096, 2, 2, 7, PAINT_ORDER_RGBA, new stdClass())],
]);
