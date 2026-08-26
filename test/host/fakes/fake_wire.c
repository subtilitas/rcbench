#include "fake_wire.h"

#include <string.h>

void fake_wire_reset(fake_wire_t *w)
{
    memset(w, 0, sizeof(*w));
    w->corrupt_byte   = -1;
    w->truncate_after = -1;
}

void fake_wire_write(fake_wire_t *w, const uint8_t *data, size_t n)
{
    if (w->deaf) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        const long index = (long)w->bytes_written;
        ++w->bytes_written;

        if (w->truncate_after >= 0 && index >= w->truncate_after) {
            continue;
        }
        if (w->len >= FAKE_WIRE_CAP) {
            continue;   /* a real receiver drops too, and silently */
        }
        uint8_t byte = data[i];
        if (w->corrupt_byte >= 0 && index == w->corrupt_byte) {
            byte ^= 0x20u;
        }
        w->buf[w->len++] = byte;
    }
}

size_t fake_wire_read(fake_wire_t *w, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (n < max && w->read < w->len) {
        out[n++] = w->buf[w->read++];
    }
    if (w->read == w->len) {
        w->read = 0;
        w->len  = 0;
    }
    return n;
}

size_t fake_wire_pending(const fake_wire_t *w)
{
    return w->len - w->read;
}
