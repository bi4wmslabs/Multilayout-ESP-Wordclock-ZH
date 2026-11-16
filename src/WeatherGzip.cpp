#include "WeatherGzip.h"

#include <string.h>

/* uzlib, the tiny inflate implementation of the ESP8266 SDK. Only the
 * decompression half is vendored (see lib/uzlib), because the compression half
 * is dead weight on a device that never compresses anything. */
extern "C" {
#include <uzlib.h>
}

namespace {

/* About 1.3 kB: the Huffman trees are part of the structure, so it lives in
 * .bss instead of on the loop stack, which is tight enough already. */
TINF_DATA inflater;

/* Guard against a corrupt stream that keeps producing output without ever
 * reaching its final block. One 16 kB result needs far fewer rounds than
 * this. */
constexpr int maxInflateRounds = 4096;

} // namespace

//------------------------------------------------------------------------------

int weathergzip::inflate(const uint8_t *source, size_t sourceLength,
                         uint8_t *destination, size_t capacity) {
    if (source == nullptr || destination == nullptr || sourceLength < 2 ||
        capacity == 0) {
        return -1;
    }

    memset(&inflater, 0, sizeof(inflater));
    inflater.source = source;
    inflater.source_limit = source + sourceLength;
    /* No read callback: the whole body is already in memory, so a truncated
     * stream is detected instead of being fed with zeros. */
    inflater.source_read_cb = nullptr;

    if (uzlib_gzip_parse_header(&inflater) != TINF_OK) {
        return -1;
    }

    /* A null window switches uzlib to "whole output in one buffer" mode: back
     * references are resolved inside `destination` (and rejected when they
     * point before its start), so no 32 kB sliding window is needed. */
    uzlib_uncompress_init(&inflater, nullptr, 0);
    inflater.dest_start = destination;
    inflater.dest = destination;
    inflater.dest_limit = destination + capacity;

    for (int round = 0; round < maxInflateRounds; round++) {
        const int result = uzlib_uncompress(&inflater);
        if (result == TINF_DONE) {
            return static_cast<int>(inflater.dest - destination);
        }
        if (result != TINF_OK) {
            /* TINF_DATA_ERROR / TINF_DICT_ERROR: corrupt or unsupported. */
            return -1;
        }
        if (inflater.dest >= inflater.dest_limit) {
            return -2;
        }
        if (inflater.eof) {
            return -3;
        }
    }

    return -1;
}
