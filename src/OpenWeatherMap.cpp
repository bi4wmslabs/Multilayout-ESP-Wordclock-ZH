#include "OpenWeatherMap.h"

#include "Config.h"
#include "Led.h"
#include "SensitiveData.h"
#include "WeatherGzip.h"
#include "WordClockState.h"
#include "WordClockTypes/ClockType.hpp"

// EEPROMAnything.h defines its functions in the header, so it cannot be
// included in a second TU without multiple definitions. Only eeprom::write() is
// needed here - it is defined once in WordClock.cpp.
namespace eeprom {
void write();
}
#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

extern ClockType *usedClockType;
extern Led led;

OpenWMap weather;

namespace {

/* QWeather (和风天气). Every account gets its own API host since 2024, which
 * is why the host is a setting instead of a constant. Port 80 is not served at
 * all, so the request has to go through TLS. */
constexpr uint16_t qweatherPort = 443;
/* Open-Meteo answers on plain HTTP as well, which is what makes it usable on
 * this device: no TLS buffers (~12 kB) and no gzip inflater (~8 kB) needed. */
constexpr uint16_t openMeteoPort = 80;
constexpr char openMeteoForecastHost[] = "api.open-meteo.com";
constexpr char openMeteoGeocodeHost[] = "geocoding-api.open-meteo.com";

/* A TLS handshake needs considerably more than a plain TCP connect: DNS plus
 * RSA/ECC work regularly takes two to three seconds on a busy 2.4 GHz link. */
constexpr int32_t connectTimeoutMs = 8000;
constexpr uint32_t dnsTimeoutMs = 3000;
/* Nothing is fetched during the first seconds after a reset: the clock, the
 * LEDs and the web server must be up before a request that can block for
 * several seconds is allowed to run. This also keeps the update page reachable
 * on a device whose saved weather settings are broken. */
constexpr uint32_t startupGraceMs = 20000;
constexpr uint32_t responseTimeoutMs = 10000;
constexpr uint32_t requestIntervalMs = 10UL * 60UL * 1000UL;
/* A failed request is retried far sooner than the regular interval, so a
 * freshly entered API host or a router that was rebooting does not cost the
 * user ten dark minutes. */
constexpr uint32_t retryIntervalMs = 60UL * 1000UL;
constexpr uint32_t forecastMaxAgeMs =
    3 * requestIntervalMs + requestIntervalMs / 2;
/* A TLS session with the default record size needs ~17 kB of buffers, which is
 * more than this firmware has left over once the LED buffers and the stack are
 * in place. QWeather echoes the max fragment length extension, so the session
 * normally runs with 512 byte records and about 1 kB of buffers instead. The
 * large buffers stay as a fallback in case an edge server ever stops
 * supporting the extension. */
constexpr int smallRxBuffer = 1024 + 325;
constexpr int largeRxBuffer = 16384 + 325;
constexpr int txBuffer = 512;
/* BearSSL allocates the buffers while connecting, so a request is only started
 * when that much heap is actually available. */
constexpr uint32_t minFreeHeap = 12000;
constexpr uint32_t minFreeHeapLargeBuffers = 26000;

/* Bodies are buffered completely before they are parsed. QWeather compresses
 * every response with gzip - Content-Encoding is set even on small error
 * documents, and neither "Accept-Encoding: identity" nor the legacy "gzip=1"
 * query parameter is honoured any more - so its body is inflated afterwards,
 * when closing the TLS session has released the ~12 kB of BearSSL buffers. */
constexpr uint16_t maxBodyBytes = 4096;
/* 24 hourly entries are about 5.5...6 kB of JSON. The buffer is deliberately
 * larger, but must stay small enough to be found as one contiguous block: after
 * the TLS session is closed the free heap is around 19 kB while the largest
 * block is far smaller, so asking for 12 kB fails even though the heap looks
 * big enough. */
constexpr size_t maxInflatedBytes = 8192;
/* Smallest buffer worth trying: below that the 24h response is truncated. */
constexpr size_t minInflatedBytes = 5120;

/* QWeather reports 24 hourly entries starting with the next full hour, so the
 * third, ninth, fifteenth and twenty-first entry are six hour steps. Open-Meteo
 * starts its hourly list at the current hour, which is what the layouts want
 * for the current slot - the four entries are therefore 0/6/12/18. */
constexpr uint8_t qweatherSlotHours[forecast::slots] = {3, 9, 15, 21};
constexpr uint8_t openMeteoSlotHours[forecast::slots] = {0, 6, 12, 18};

bool qweatherConfigured() {
    return G.qweatherHost[0] != '\0' && G.qweatherLocation[0] != '\0' &&
           G.openWeatherMap.apikey[0] != '\0' &&
           forecast::qweather::hostIsPlausible(G.qweatherHost) &&
           forecast::qweather::locationIsPlausible(G.qweatherLocation);
}

bool openMeteoConfigured() {
    return forecast::openmeteo::cityIsPlausible(G.cityName);
}

/* Percent-encodes everything outside the unreserved set. Needed for the city
 * name, which arrives as UTF-8 from the web interface: three bytes per Chinese
 * character become nine characters in the query string. */
void urlEncode(const char *in, char *out, size_t outSize) {
    static const char hex[] = "0123456789ABCDEF";
    size_t written = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(in);
         *p != '\0' && written + 4 < outSize; p++) {
        const unsigned char c = *p;
        const bool unreserved = (c >= 'a' && c <= 'z') ||
                                (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                c == '.' || c == '~';
        if (unreserved) {
            out[written++] = static_cast<char>(c);
        } else {
            out[written++] = '%';
            out[written++] = hex[c >> 4];
            out[written++] = hex[c & 0x0F];
        }
    }
    out[written] = '\0';
}

/* "南京市" / "义乌市" / "朝阳区" are what users type, but the geocoding service
 * matches names literally and finds nothing for them. The administrative suffix
 * is therefore dropped before the query is built. */
void stripCitySuffix(char *city, size_t size) {
    (void)size;
    static const char *const suffixes[] = {"市", "区", "县", "省", "镇", "乡"};
    bool stripped = true;
    while (stripped) {
        stripped = false;
        size_t length = strlen(city);
        for (const char *suffix : suffixes) {
            const size_t suffixLength = strlen(suffix);
            if (length > suffixLength &&
                strcmp(city + length - suffixLength, suffix) == 0) {
                city[length - suffixLength] = '\0';
                stripped = true;
                break;
            }
        }
    }
}

} // namespace

//------------------------------------------------------------------------------

bool OpenWMap::usesQWeather() const { return G.weatherSource == 1; }

//------------------------------------------------------------------------------

bool OpenWMap::currentWeather(int8_t &temperature, uint16_t &condition) const {
    if (!forecastValid) {
        return false;
    }
    /* Whole degrees: the Chinese face writes the temperature out digit by
     * digit, the layouts that use wordsFor() round to 5 degree steps
     * themselves. */
    temperature = wTemp[shownSlot];
    condition = wWeather[shownSlot];
    return true;
}

//------------------------------------------------------------------------------

void OpenWMap::calcWeatherClockface() {
    if (!forecastValid) {
        return;
    }

    for (FrontWord word : forecast::wordsFor(
             shownSlot, daytime,
             forecast::temperatureStep(wTemp[shownSlot]),
             wWeather[shownSlot])) {
        usedClockType->show(word);
    }
}

//------------------------------------------------------------------------------

void OpenWMap::requestNow() { requestNowPending = true; }

//------------------------------------------------------------------------------

void OpenWMap::cityChanged() {
    /* The cached coordinates belong to the previous city. Keeping them would
     * show the weather of the old town until the next manual save. */
    G.latitude = 0;
    G.longitude = 0;
    requestNowPending = true;
}

//------------------------------------------------------------------------------

void OpenWMap::skipUntilNextBoot() { blocked = true; }

//------------------------------------------------------------------------------

void OpenWMap::loop() {
    if (blocked) {
        return;
    }

    if (forecastValid && millis() - forecastMillis > forecastMaxAgeMs) {
        forecastValid = false;
        Serial.println("Weather forecast is outdated, hiding it");
    }

    /* Only the full Ger22x11Weather clockface rotates through the forecast
     * slots (and resets the LEDs for it). Layouts that draw the weather in
     * dedicated rows (e.g. ZH10x11) keep showing the current slot. */
    if (usedClockType->hasWeatherLayout() && _second % 10 == 0) {
        shownSlot = (shownSlot + 1) % forecast::slots;
        led.clear();
    }

    if (requestState != RequestState::Idle) {
        pollWeatherResponse();
    } else if (WiFi.status() == WL_CONNECTED &&
               (requestNowPending ||
                (millis() >= startupGraceMs && requestDue()))) {
        requestNowPending = false;
        requestedOnce = true;
        startWeatherRequest();
    }
}

//------------------------------------------------------------------------------

bool OpenWMap::requestDue() const {
    if (!requestedOnce) {
        return true;
    }
    const uint32_t interval =
        lastRequestSucceeded ? requestIntervalMs : retryIntervalMs;
    return millis() - lastRequestMillis >= interval;
}

//------------------------------------------------------------------------------

void OpenWMap::releaseClient() {
    if (connection != nullptr) {
        connection->stop();
        if (secureConnection != nullptr) {
            /* `connection` is the same object, deleted through its own type. */
            delete secureConnection;
            secureConnection = nullptr;
        } else {
            delete connection;
        }
        connection = nullptr;
    }
}

//------------------------------------------------------------------------------

bool OpenWMap::trySecureConnect(const IPAddress &address, bool smallBuffers) {
    releaseClient();
    secureConnection = new WiFiClientSecure();
    if (secureConnection == nullptr) {
        Serial.println("Weather: out of memory, cannot create the TLS client");
        return false;
    }
    connection = secureConnection;

    /* The account specific host is not validated against a certificate chain:
     * the name changes per account, and an ESP8266 cannot afford a CA store on
     * top of the TLS buffers. */
    secureConnection->setInsecure();
    secureConnection->setBufferSizes(smallBuffers ? smallRxBuffer : largeRxBuffer,
                                     txBuffer);
    secureConnection->setTimeout(connectTimeoutMs);

    const uint32_t connectStartMillis = millis();
    const bool ok = secureConnection->connect(address, qweatherPort) == 1;
    const uint32_t elapsed = millis() - connectStartMillis;

    if (!ok) {
        char sslError[64] = {0};
        const int code =
            secureConnection->getLastSSLError(sslError, sizeof(sslError));
        Serial.printf("TLS connection to %s failed after %u ms "
                      "(heap %u, %s buffers, ssl error %d %s)\n",
                      requestHost, static_cast<unsigned>(elapsed),
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      smallBuffers ? "small" : "full", code, sslError);
        releaseClient();
        return false;
    }

    Serial.printf("TLS connected to %s in %u ms (heap %u, %s buffers, "
                  "MFLN %d)\n",
                  requestHost, static_cast<unsigned>(elapsed),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  smallBuffers ? "small" : "full",
                  secureConnection->getMFLNStatus());
    return true;
}

//------------------------------------------------------------------------------

bool OpenWMap::tryPlainConnect(const IPAddress &address) {
    releaseClient();
    connection = new WiFiClient();
    if (connection == nullptr) {
        Serial.println("Weather: out of memory, cannot create the TCP client");
        return false;
    }

    const uint32_t connectStartMillis = millis();
    const bool ok = connection->connect(address, openMeteoPort) == 1;
    const uint32_t elapsed = millis() - connectStartMillis;

    if (!ok) {
        Serial.printf("TCP connection to %s failed after %u ms (heap %u)\n",
                      requestHost, static_cast<unsigned>(elapsed),
                      static_cast<unsigned>(ESP.getFreeHeap()));
        releaseClient();
        return false;
    }

    Serial.printf("TCP connected to %s in %u ms (heap %u)\n", requestHost,
                  static_cast<unsigned>(elapsed),
                  static_cast<unsigned>(ESP.getFreeHeap()));
    return true;
}

//------------------------------------------------------------------------------

bool OpenWMap::connectToServer() {
    const uint32_t freeHeap = ESP.getFreeHeap();

#ifdef ESP8266
    Serial.printf("Weather: heap %u, loop stack free %u\n",
                  static_cast<unsigned>(freeHeap),
                  static_cast<unsigned>(ESP.getFreeContStack()));
#endif

    /* Only the TLS path needs the reserve; a plain socket costs a few hundred
     * bytes and works with whatever is left. */
    if (usesQWeather() && freeHeap < minFreeHeap) {
        Serial.printf("Weather: only %u bytes heap free, need %u - skipped\n",
                      static_cast<unsigned>(freeHeap),
                      static_cast<unsigned>(minFreeHeap));
        return false;
    }

    /* Resolve the name here instead of letting the client do it: an unbounded
     * lookup of an unreachable or malformed name blocks the loop task long
     * enough for the watchdog to reset the clock. */
    IPAddress address;
    const uint32_t dnsStartMillis = millis();
    if (WiFi.hostByName(requestHost, address, dnsTimeoutMs) != 1) {
        Serial.printf("Weather: DNS lookup for %s failed after %u ms\n",
                      requestHost,
                      static_cast<unsigned>(millis() - dnsStartMillis));
        return false;
    }
    Serial.printf("Weather: %s -> %s (%u ms)\n", requestHost,
                  address.toString().c_str(),
                  static_cast<unsigned>(millis() - dnsStartMillis));

    if (!usesQWeather()) {
        return tryPlainConnect(address);
    }

    if (trySecureConnect(address, true)) {
        return true;
    }

    if (ESP.getFreeHeap() < minFreeHeapLargeBuffers) {
        Serial.println("Weather: not enough heap left for full size TLS "
                       "buffers, will retry later");
        return false;
    }

    Serial.println("Weather: retrying with full size TLS buffers");
    return trySecureConnect(address, false);
}

//------------------------------------------------------------------------------

void OpenWMap::startWeatherRequest() {
    lastRequestMillis = millis();
    lastRequestSucceeded = false;

    if (usesQWeather()) {
        startForecastRequest();
    } else if (G.latitude == 0 && G.longitude == 0) {
        startGeocodeRequest();
    } else {
        startForecastRequest();
    }
}

//------------------------------------------------------------------------------

void OpenWMap::startForecastRequest() {
    requestKind = RequestKind::Forecast;

    if (usesQWeather()) {
        if (!qweatherConfigured()) {
            /* Only once per boot, otherwise this fills the log every minute. */
            if (!warnedMissingConfig) {
                warnedMissingConfig = true;
                Serial.println("Weather: API host, API key and location id "
                               "must all be set in the web interface - the "
                               "weather rows stay dark until then");
            }
            return;
        }

        Serial.println("");
        Serial.println("--------------------------------------");
        Serial.println("Connecting to QWeather");
        Serial.println("--------------------------------------");

        char apiKeyMasked[sizeof(G.openWeatherMap.apikey) + 1] = {0};
        sensitive::maskPreservingSuffix(apiKeyMasked,
                                        G.openWeatherMap.apikey);

        snprintf(resource, sizeof(resource),
                 "/v7/weather/24h?location=%s&key=%s&lang=zh",
                 G.qweatherLocation, G.openWeatherMap.apikey);

        char maskedResource[sizeof(resource)];
        snprintf(maskedResource, sizeof(maskedResource),
                 "/v7/weather/24h?location=%s&key=%s&lang=zh",
                 G.qweatherLocation, apiKeyMasked);
        Serial.print("Calling URL: ");
        Serial.println(maskedResource);

        strlcpy(requestHost, G.qweatherHost, sizeof(requestHost));
    } else {
        if (!openMeteoConfigured()) {
            if (!warnedMissingConfig) {
                warnedMissingConfig = true;
                Serial.println("Weather: enter a city name in the web "
                               "interface - the weather rows stay dark until "
                               "then");
            }
            return;
        }

        Serial.println("");
        Serial.println("--------------------------------------");
        Serial.println("Connecting to Open-Meteo");
        Serial.println("--------------------------------------");

        snprintf(resource, sizeof(resource),
                 "/v1/forecast?latitude=%.4f&longitude=%.4f"
                 "&hourly=temperature_2m,weather_code&forecast_hours=24"
                 "&timezone=auto",
                 static_cast<double>(G.latitude),
                 static_cast<double>(G.longitude));
        Serial.print("Calling URL: ");
        Serial.println(resource);

        strlcpy(requestHost, openMeteoForecastHost, sizeof(requestHost));
    }

    if (!connectToServer()) {
        return;
    }

    if (connection == nullptr) {
        return;
    }

    connection->print("GET ");
    connection->print(resource);
    connection->println(" HTTP/1.0");
    connection->print("Host: ");
    connection->println(requestHost);
    /* QWeather ignores what the client asks for; Open-Meteo honours it and
     * skipping the gzip saves the inflater entirely. */
    if (usesQWeather()) {
        connection->println("Accept-Encoding: gzip");
    } else {
        connection->println("Accept-Encoding: identity");
    }
    connection->println("Connection: close");
    connection->println();

    headerEndMatched = 0;
    headerLineLength = 0;
    contentLength = 0;
    bodyReceived = 0;
    requestStartMillis = millis();
    requestState = RequestState::ReadingHeader;
}

//------------------------------------------------------------------------------

void OpenWMap::startGeocodeRequest() {
    requestKind = RequestKind::Geocode;

    if (!openMeteoConfigured()) {
        if (!warnedMissingConfig) {
            warnedMissingConfig = true;
            Serial.println("Weather: enter a city name in the web interface - "
                           "the weather rows stay dark until then");
        }
        return;
    }

    /* Work on a copy: the suffix is stripped for the query only, the stored
     * name stays exactly as the user typed it. */
    char city[sizeof(G.cityName)] = {0};
    sensitive::copyBoundedString(city, G.cityName);
    stripCitySuffix(city, sizeof(city));

    char encoded[128] = {0};
    urlEncode(city, encoded, sizeof(encoded));

    Serial.println("");
    Serial.println("--------------------------------------");
    Serial.println("Resolving city with Open-Meteo");
    Serial.println("--------------------------------------");
    Serial.printf("City    : %s\n", city);

    snprintf(resource, sizeof(resource),
             "/v1/search?name=%s&count=1&language=zh&format=json", encoded);
    Serial.print("Calling URL: ");
    Serial.println(resource);

    strlcpy(requestHost, openMeteoGeocodeHost, sizeof(requestHost));

    if (!connectToServer()) {
        return;
    }

    if (connection == nullptr) {
        return;
    }

    connection->print("GET ");
    connection->print(resource);
    connection->println(" HTTP/1.0");
    connection->print("Host: ");
    connection->println(requestHost);
    connection->println("Accept-Encoding: identity");
    connection->println("Connection: close");
    connection->println();

    headerEndMatched = 0;
    headerLineLength = 0;
    contentLength = 0;
    bodyReceived = 0;
    requestStartMillis = millis();
    requestState = RequestState::ReadingHeader;
}

//------------------------------------------------------------------------------

void OpenWMap::finishWeatherRequest() {
    /* Dropping the client releases the TLS buffers again, which matters on
     * this device: the remaining heap decides whether the next request can run
     * at all. */
    releaseClient();
    requestState = RequestState::Idle;
}

//------------------------------------------------------------------------------

void OpenWMap::freeBody() {
    free(body);
    body = nullptr;
    bodyCapacity = 0;
    bodyReceived = 0;
}

//------------------------------------------------------------------------------

void OpenWMap::beginBodyRead() {
    /* QWeather always sends Content-Length. Open-Meteo answers on a
     * "Connection: close" basis and the body simply ends with the socket, so
     * in that case the buffer is the upper bound and the close is the end of
     * the message. */
    uint16_t wanted = contentLength;
    if (wanted == 0) {
        wanted = maxBodyBytes;
    } else if (wanted > maxBodyBytes) {
        Serial.printf("Weather: unexpected Content-Length %u (limit %u)\n",
                      static_cast<unsigned>(wanted),
                      static_cast<unsigned>(maxBodyBytes));
        finishWeatherRequest();
        return;
    }

    body = static_cast<uint8_t *>(malloc(wanted));
    if (body == nullptr) {
        Serial.printf("Weather: cannot buffer the %u byte body, only %u bytes "
                      "heap free\n",
                      static_cast<unsigned>(wanted),
                      static_cast<unsigned>(ESP.getFreeHeap()));
        finishWeatherRequest();
        return;
    }

    bodyCapacity = wanted;
    bodyReceived = 0;
    requestState = RequestState::ReadingBody;
    /* The handshake time must not eat into the body timeout. */
    requestStartMillis = millis();
}

//------------------------------------------------------------------------------

void OpenWMap::pollWeatherResponse() {
    if (connection == nullptr) {
        freeBody();
        requestState = RequestState::Idle;
        return;
    }

    if (requestState == RequestState::ReadingHeader) {
        while (connection->available() > 0) {
            const int c = connection->read();
            if (c < 0) {
                break;
            }

            /* Collect the header lines: the only one that matters is
             * Content-Length, everything else is skipped. */
            if (c == '\n') {
                headerLine[headerLineLength] = '\0';
                if (strncasecmp(headerLine, "Content-Length:", 15) == 0) {
                    const unsigned long value =
                        strtoul(headerLine + 15, nullptr, 10);
                    /* Clamp instead of wrapping: anything above the limit is
                     * rejected by beginBodyRead(). */
                    contentLength = value > 65535UL
                                        ? 65535U
                                        : static_cast<uint16_t>(value);
                }
                headerLineLength = 0;
            } else if (c != '\r' &&
                       static_cast<size_t>(headerLineLength) + 1 <
                           sizeof(headerLine)) {
                headerLine[headerLineLength++] = static_cast<char>(c);
            }

            if (c == "\r\n\r\n"[headerEndMatched]) {
                headerEndMatched++;
            } else {
                headerEndMatched = (c == '\r') ? 1 : 0;
            }
            if (headerEndMatched == 4) {
                beginBodyRead();
                return;
            }
        }

        if (!connection->connected()) {
            Serial.println("Connection closed before response header ended");
            finishWeatherRequest();
        } else if (millis() - requestStartMillis > responseTimeoutMs) {
            Serial.println("Timeout while waiting for weather response");
            finishWeatherRequest();
        }
        return;
    }

    while (connection->available() > 0 && bodyReceived < bodyCapacity) {
        const int received =
            connection->read(body + bodyReceived, bodyCapacity - bodyReceived);
        if (received <= 0) {
            break;
        }
        bodyReceived = static_cast<uint16_t>(bodyReceived + received);
    }

    if (bodyCapacity > 0 && bodyReceived >= bodyCapacity) {
        handleBodyComplete();
        return;
    }

    if (!connection->connected()) {
        if (contentLength == 0 && bodyReceived > 0) {
            /* No Content-Length: the close is the end of the body. */
            handleBodyComplete();
            return;
        }
        Serial.printf("Connection closed after %u of %u body bytes\n",
                      static_cast<unsigned>(bodyReceived),
                      static_cast<unsigned>(bodyCapacity));
        finishWeatherRequest();
        freeBody();
    } else if (millis() - requestStartMillis > responseTimeoutMs) {
        Serial.printf("Timeout while reading the weather body (%u of %u bytes)\n",
                      static_cast<unsigned>(bodyReceived),
                      static_cast<unsigned>(bodyCapacity));
        finishWeatherRequest();
        freeBody();
    }
}

//------------------------------------------------------------------------------

void OpenWMap::handleBodyComplete() {
    const bool wasGeocode = requestKind == RequestKind::Geocode;

    /* Close the session before touching the payload: the QWeather inflater
     * needs a buffer several times the size of the compressed body, and the
     * 12 kB of BearSSL buffers are exactly what pays for it. */
    finishWeatherRequest();

    if (usesQWeather()) {
        decodeWeatherBody();
    } else if (wasGeocode) {
        processGeocodeResponse(body, bodyReceived);
    } else {
        processOpenMeteoResponse(body, bodyReceived);
    }
    freeBody();
}

//------------------------------------------------------------------------------

void OpenWMap::decodeWeatherBody() {
    if (body == nullptr || bodyReceived < 2) {
        return;
    }

    if (body[0] != 0x1f || body[1] != 0x8b) {
        /* Not compressed after all - parse it where it lies. */
        processQWeatherResponse(body, bodyReceived);
        return;
    }

    /* The heap after closing the TLS session looks large enough but is split
     * into pieces: BearSSL's buffers are freed one by one and leave gaps. What
     * matters for the buffer below is the largest contiguous block, not
     * getFreeHeap(). */
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t maxBlock = ESP.getMaxFreeBlockSize();

    /* Try the full size, then smaller ones: a compact response still parses in
     * a smaller buffer and a failed attempt costs nothing. Only malloc() can
     * tell - the heap can look large and still have no block of that size. */
    static const size_t candidates[] = {maxInflatedBytes, 6144, minInflatedBytes};
    uint8_t *json = nullptr;
    size_t capacity = 0;
    for (const size_t candidate : candidates) {
        json = static_cast<uint8_t *>(malloc(candidate));
        if (json != nullptr) {
            capacity = candidate;
            break;
        }
    }
    if (json == nullptr) {
        Serial.printf("Weather: no room to inflate the response "
                      "(heap %u, largest block %u, need %u)\n",
                      static_cast<unsigned>(freeHeap),
                      static_cast<unsigned>(maxBlock),
                      static_cast<unsigned>(minInflatedBytes));
        return;
    }

    const uint32_t inflateStartMillis = millis();
    const int length = weathergzip::inflate(body, bodyReceived, json, capacity);
    const uint32_t elapsed = millis() - inflateStartMillis;

    if (length < 0) {
        if (length == -2) {
            Serial.printf("Weather: response exceeds the %u byte buffer\n",
                          static_cast<unsigned>(capacity));
        } else {
            Serial.printf("Weather: cannot inflate the %u byte gzip body "
                          "(error %d)\n",
                          static_cast<unsigned>(bodyReceived), length);
        }
        free(json);
        return;
    }

    Serial.printf("Weather: %u bytes gzip -> %d bytes JSON in %u ms "
                  "(heap %u, block %u)\n",
                  static_cast<unsigned>(bodyReceived), length,
                  static_cast<unsigned>(elapsed),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxFreeBlockSize()));

    /* The compressed body is not needed any more and the JSON document is
     * allocated next, so hand the space back first. */
    freeBody();
    processQWeatherResponse(json, static_cast<size_t>(length));
    free(json);
}

//------------------------------------------------------------------------------

void OpenWMap::processQWeatherResponse(const uint8_t *json, size_t length) {
    StaticJsonDocument<512> filter;
    filter["code"] = true;
    filter["hourly"][0]["temp"] = true;
    filter["hourly"][0]["icon"] = true;
    filter["error"]["status"] = true;
    filter["error"]["title"] = true;
    filter["error"]["detail"] = true;
#if WEATHER_VERBOSE
    filter["hourly"][0]["text"] = true;
    const uint32_t parseStartMillis = millis();
#endif

    DynamicJsonDocument doc(1280);
    auto error =
        deserializeJson(doc, reinterpret_cast<const char *>(json), length,
                        DeserializationOption::Filter(filter));
    if (error) {
        Serial.print(F("deserializeJson() failed with code "));
        Serial.println(error.c_str());
        return;
    }

#if WEATHER_VERBOSE
    Serial.printf("Weather body parsed in %lu ms: ",
                  static_cast<unsigned long>(millis() - parseStartMillis));
    serializeJson(doc, Serial);
    Serial.println();
#endif

    const char *code = doc["code"] | "";
    if (strcmp(code, "200") != 0) {
        Serial.printf("QWeather error %s: %s (%s)\n",
                      code[0] != '\0' ? code : "no code",
                      doc["error"]["title"] | "unknown",
                      doc["error"]["detail"] | "no detail");
        return;
    }

    JsonArray hourly = doc["hourly"];
    if (hourly.size() == 0) {
        Serial.println("QWeather: response contains no hourly entries");
        return;
    }

    for (uint8_t slot = 0; slot < forecast::slots; slot++) {
        /* Clamp instead of rejecting the whole forecast: a shorter response
         * still carries usable data for the first slots. */
        size_t index = qweatherSlotHours[slot];
        if (index >= hourly.size()) {
            index = hourly.size() - 1;
        }
        JsonObject entry = hourly[index];
        const char *icon = entry["icon"] | "";
        const char *tempText = entry["temp"] | "0";
        const double temp = atof(tempText);

#if WEATHER_VERBOSE
        Serial.println("----------");
        Serial.printf("+%uh\n",
                      static_cast<unsigned>(qweatherSlotHours[slot]));
        Serial.print("Type: ");
        Serial.println(entry["text"].as<const char *>());
        Serial.print("Icon: ");
        Serial.println(icon);
        Serial.print("Temp: ");
        Serial.print(tempText);
        Serial.println(" C");
#endif

        wTemp[slot] = forecast::temperatureCelsius(temp);
        wWeather[slot] = forecast::conditionGroupFromIcon(icon);
        if (wWeather[slot] == 0) {
            Serial.printf("[ERROR] Unknown QWeather icon %s\n", icon);
        }
    }

    storeForecast();
}

//------------------------------------------------------------------------------

void OpenWMap::processOpenMeteoResponse(const uint8_t *json, size_t length) {
    StaticJsonDocument<384> filter;
    filter["hourly"]["temperature_2m"] = true;
    filter["hourly"]["weather_code"] = true;
    filter["error"] = true;
    filter["reason"] = true;

    DynamicJsonDocument doc(1536);
    auto error =
        deserializeJson(doc, reinterpret_cast<const char *>(json), length,
                        DeserializationOption::Filter(filter));
    if (error) {
        Serial.print(F("Open-Meteo: deserializeJson() failed with code "));
        Serial.println(error.c_str());
        return;
    }

    if (doc["error"] | false) {
        Serial.printf("Open-Meteo error: %s\n", doc["reason"] | "unknown");
        /* Back off to the regular interval: an error document is a service
         * answer, not a broken link. */
        lastRequestSucceeded = true;
        return;
    }

    JsonArray temperatures = doc["hourly"]["temperature_2m"];
    JsonArray codes = doc["hourly"]["weather_code"];
    if (temperatures.size() == 0 || codes.size() == 0) {
        Serial.println("Open-Meteo: response contains no hourly entries");
        return;
    }

    for (uint8_t slot = 0; slot < forecast::slots; slot++) {
        /* Clamp instead of rejecting the whole forecast: a shorter response
         * still carries usable data for the first slots. */
        size_t index = openMeteoSlotHours[slot];
        if (index >= temperatures.size()) {
            index = temperatures.size() - 1;
        }
        const double temp = temperatures[index] | 0.0;
        const int code = codes[index] | 0;

#if WEATHER_VERBOSE
        Serial.println("----------");
        Serial.printf("+%uh\n",
                      static_cast<unsigned>(openMeteoSlotHours[slot]));
        Serial.printf("Code: %d, Temp: %.1f C\n", code, temp);
#endif

        wTemp[slot] = forecast::temperatureCelsius(temp);
        wWeather[slot] = forecast::conditionGroupFromWmo(code);
        if (wWeather[slot] == 0) {
            Serial.printf("[ERROR] Unknown WMO weather code %d\n", code);
        }
    }

    storeForecast();
}

//------------------------------------------------------------------------------

void OpenWMap::storeForecast() {
    daytime = forecast::daytimeFor(_hour);
    forecastValid = true;
    forecastMillis = millis();
    lastRequestSucceeded = true;

    /* One unconditional line per update: with WEATHER_VERBOSE off this is the
     * only proof in the serial log that the chain works, and it tells whether
     * rows 7-8 are dark because of missing data or because of the layout. */
    Serial.printf("Weather updated: %d degC, condition group %u (slot 0)\n",
                  static_cast<int>(wTemp[0]),
                  static_cast<unsigned>(wWeather[0]));

#if WEATHER_VERBOSE
    Serial.println("Hour");
    Serial.println(_hour);
    Serial.println("----------");
    printDeterminedData();
#endif
}

//------------------------------------------------------------------------------

void OpenWMap::processGeocodeResponse(const uint8_t *json, size_t length) {
    StaticJsonDocument<384> filter;
    filter["results"][0]["latitude"] = true;
    filter["results"][0]["longitude"] = true;
    filter["results"][0]["name"] = true;
    filter["results"][0]["admin1"] = true;
    filter["results"][0]["country_code"] = true;
    filter["error"] = true;
    filter["reason"] = true;

    DynamicJsonDocument doc(768);
    auto error =
        deserializeJson(doc, reinterpret_cast<const char *>(json), length,
                        DeserializationOption::Filter(filter));
    if (error) {
        Serial.print(F("Geocoding: deserializeJson() failed with code "));
        Serial.println(error.c_str());
        return;
    }

    if (doc["error"] | false) {
        Serial.printf("Geocoding error: %s\n", doc["reason"] | "unknown");
        lastRequestSucceeded = true;
        return;
    }

    JsonArray results = doc["results"];
    if (results.size() == 0) {
        Serial.printf("Geocoding: city \"%s\" not found - enter the name "
                      "without 市/区/县, e.g. 南京 instead of 南京市\n",
                      G.cityName);
        /* A wrong city name is not a transport problem: retrying every minute
         * would only hammer the service until the user edits the field. */
        lastRequestSucceeded = true;
        return;
    }

    JsonObject entry = results[0];
    const double latitude = entry["latitude"] | 0.0;
    const double longitude = entry["longitude"] | 0.0;
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
        longitude > 180.0 || (latitude == 0.0 && longitude == 0.0)) {
        Serial.printf("Geocoding: implausible coordinates %.4f/%.4f\n",
                      latitude, longitude);
        return;
    }

    G.latitude = static_cast<float>(latitude);
    G.longitude = static_cast<float>(longitude);

    /* Store the spellings the service returned, so the settings page shows
     * what the clock is actually using. */
    const char *name = entry["name"] | "";
    const char *region = entry["admin1"] | "";
    const char *country = entry["country_code"] | "";
    if (name[0] != '\0') {
        strlcpy(G.cityName, name, sizeof(G.cityName));
    }

    eeprom::write();

    Serial.printf("Geocoding: %s (%s %s) -> %.4f, %.4f, saved\n",
                  G.cityName, region, country, latitude, longitude);

    /* The forecast itself follows immediately; without this the rows would
     * stay dark until the next interval. */
    requestNowPending = true;
}

//------------------------------------------------------------------------------

void OpenWMap::printDeterminedData() {
    for (uint8_t slot = 0; slot < forecast::slots; slot++) {
        const int hours = 6 * (slot + 1);
        Serial.printf("Temp_%d - %d\n", hours, wTemp[slot]);
        Serial.printf("wWeather%d - %u\n", hours, wWeather[slot]);
        Serial.println("--------- ");
    }
    Serial.print("daytime - ");
    Serial.println(daytime);
    Serial.println("--------- ");
}
