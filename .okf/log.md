# Log

## 2026-11-13

* A translucent run blends two bytes a multiply (bytes 0 and 2 in one word's halves, 1 and 3 in another's), dividing each half by 255 as `(y + 1 + (y >> 8)) >> 8`; the scan does not work a pixel's byte again when its sum is the one before it. The bytes are unchanged: 46 passed on NTS and ZTS, NTS under guard malloc. [surface](api/surface.md)
* Bundle created with ext-paint 0.11.0: [surface](api/surface.md) (`PaintSurface`, 22 constants), [build](runbooks/build.md). Seven shape ops (path, ellipse, ring, rounded box and ring, shadow, one-pixel polyline), colour and gradient paints, four byte orders, `over()` for memory another object holds. `PHP_PAINT_VERSION` 0.11.0, `branch-alias` `dev-main` is `0.11.x-dev`, on the Venusian 0.11 line. Built with `-Wall -Wextra` with no warning from `src/`, `fmadd` count 0, installed and 46 passed on Homebrew PHP 8.4.27 NTS and ZTS (macOS arm64), NTS also under guard malloc. Not yet built on Linux.
