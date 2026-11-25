# qrcodegen

The QR (Quick Response) code encoder of the servo label
(`shared/ui/label_layout.c`): text in, a matrix of modules out. No heap; the
caller gives two buffers of `qrcodegen_BUFFER_LEN_FOR_VERSION(v)` bytes.

| Item | Value |
| --- | --- |
| Library | QR Code generator library, C version, by Project Nayuki |
| Source | https://github.com/nayuki/QR-Code-generator, tag `v1.8.0`, commit `720f62bddb7226106071d4728c292cb1df519ceb`, folder `c/` |
| Files | `qrcodegen.c`, `qrcodegen.h`, unchanged |
| SHA-256 `qrcodegen.c` | `300eff07ee25baaa7578f20284411638154716379437391e7e689c0e6ce81403` |
| SHA-256 `qrcodegen.h` | `e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589` |
| Licence | MIT, `LICENSE` in this folder; entry in the repository's `NOTICE` |

The files are not edited here. A new release replaces both files whole and
updates the table. The host build compiles them without the project's
warning flags, and clang-tidy, cppcheck and the coverage figure leave this
folder out. `qrcodegen.c` is 1,022 lines, under 1,000 code lines.
