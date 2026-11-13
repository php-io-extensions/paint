# Security Policy

## Supported versions

ext-paint is pre-1.0. No 0.x release receives security fixes or advisories; fixes land in
the next release line. Security support starts with 1.0.

| Version | Security fixes |
|---------|----------------|
| < 1.0   | No             |

## Reporting a vulnerability

Please don't open a public issue for a security problem.

Report it privately through GitHub: the **Report a vulnerability** button on the
[php-io-extensions/paint](https://github.com/php-io-extensions/paint) repository's
**Security** tab. If that isn't available, email **info@projectsaturnstudios.com**.

Include what you found, the affected version, your platform and PHP build (NTS or ZTS), the
surface's size and the call that triggers it, and steps to reproduce. Reports are read
by the maintainers and answered as soon as we can.

## What the extension touches

ext-paint is plain C on the Zend API. It opens no file, socket or device, links no system
library and holds no state between calls beyond each surface's pixels. It reads the ops and
paints it is given and writes pixels.

- **Input.** Every number of an op is checked as it is read: finite, with magnitude at most
  `PAINT_LIMIT`. An op or a paint of the wrong shape is a `ValueError` naming the op.
- **Bounds.** A surface's sides are 1..32767. The rect a call paints inside is cut to the
  surface first, and every run of pixels written is inside that rect.
- **Memory.** A surface made with `new` owns its pixels. A surface made with
  `PaintSurface::over()` writes to an address the caller gives: the extension checks that the
  address is not 0 and that the stride holds a row, and trusts the rest. A wrong address or a
  size larger than the memory is a write outside it. `over()` is for a caller that already
  holds native memory, such as another extension's framebuffer.

A report is welcome for any way to make a surface that owns its pixels write outside them,
or to crash the extension with an argument it should have refused.
