#pragma once

#include <stddef.h>
#include <stdint.h>

/* QWeather compresses every response body with gzip (the Content-Encoding
 * header is even set on error documents, and neither "Accept-Encoding:
 * identity" nor the legacy "gzip=n" query parameter changes that). ArduinoJson
 * cannot read a gzip stream, so the body has to be inflated first.
 *
 * The decompressor works on buffers that are already in memory: the compressed
 * body is small (~2 kB for 24 hourly entries) while the inflated JSON needs
 * several kB, and doing both while the TLS session still holds its buffers does
 * not fit into the heap of an ESP8266. */
namespace weathergzip {

/* Inflates one gzip member from `source` into `destination`.
 *
 * Returns the number of bytes written, or
 *   -1 when the stream is not a valid gzip/deflate stream,
 *   -2 when the result does not fit into `capacity`,
 *   -3 when the stream is truncated. */
int inflate(const uint8_t *source, size_t sourceLength, uint8_t *destination,
            size_t capacity);

} // namespace weathergzip
