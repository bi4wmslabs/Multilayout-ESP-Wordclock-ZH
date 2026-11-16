#pragma once

#include "WeatherMapping.h"
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <stddef.h>
#include <stdint.h>

/* Despite the historical class name this fetches the forecast from one of two
 * services, selected by G.weatherSource:
 *
 *   Open-Meteo  free, no registration, the user only enters a city name.
 *               Plain HTTP on port 80 (the service answers there as well), so
 *               neither a TLS session nor a gzip inflater is needed - both of
 *               which are expensive on an ESP8266.
 *   QWeather    (和风天气) needs an account specific API host, key and location
 *               id, and only answers on TLS 443 with gzip encoded bodies.
 *
 * Everything the layouts consume (temperature in whole degrees and a condition
 * group) is identical for both sources. */
class OpenWMap {
public:
    void calcWeatherClockface();
    void loop();

    /* Fetch again as soon as possible. Used right after new credentials were
     * saved, so the rows light up without waiting for the next interval. */
    void requestNow();

    /* Forget the cached coordinates, so the next request resolves the city
     * name again. Called when the city was edited. */
    void cityChanged();

    /* Keep the weather rows dark until the next reset. setup() calls this when
     * the previous reset was a watchdog or a crash: the clock has to come up
     * even if the request is what killed it. */
    void skipUntilNextBoot();

    /* Current forecast slot (or slot 0 for layouts that do not rotate the
     * slots). Returns false while no valid forecast is available. */
    bool currentWeather(int8_t &temperature, uint16_t &condition) const;

private:
    enum class RequestState : uint8_t { Idle, ReadingHeader, ReadingBody };
    enum class RequestKind : uint8_t { Forecast, Geocode };

    bool usesQWeather() const;
    bool requestDue() const;
    bool connectToServer();
    bool trySecureConnect(const IPAddress &address, bool smallBuffers);
    bool tryPlainConnect(const IPAddress &address);
    void startWeatherRequest();
    void startForecastRequest();
    void startGeocodeRequest();
    void finishWeatherRequest();
    void pollWeatherResponse();
    void beginBodyRead();
    void handleBodyComplete();
    void decodeWeatherBody();
    void processQWeatherResponse(const uint8_t *json, size_t length);
    void processOpenMeteoResponse(const uint8_t *json, size_t length);
    void processGeocodeResponse(const uint8_t *json, size_t length);
    void storeForecast();
    void freeBody();
    void printDeterminedData();
    void releaseClient();

    /* Created on first use. A global WiFiClientSecure allocates its context
     * (and a BearSSL stack thunk) from the heap while the C++ runtime is still
     * starting, which is a needless risk during boot. */
    WiFiClient *connection = nullptr;
    /* Non-null only while `connection` points at a TLS session. */
    WiFiClientSecure *secureConnection = nullptr;
    RequestState requestState = RequestState::Idle;
    RequestKind requestKind = RequestKind::Forecast;
    uint32_t requestStartMillis = 0;
    uint32_t lastRequestMillis = 0;
    bool requestedOnce = false;
    bool lastRequestSucceeded = false;
    bool requestNowPending = false;
    bool warnedMissingConfig = false;
    bool blocked = false;
    uint8_t headerEndMatched = 0;
    char requestHost[48] = {};
    /* Longest resource: the geocoding query with a 13 character UTF-8 city
     * name, where every byte becomes %XX. */
    char resource[192] = {};
    /* The response body is buffered before it is parsed: for QWeather because
     * it has to be inflated after the TLS session is closed, for Open-Meteo
     * because the body has no Content-Length and ends when the server closes
     * the connection. */
    char headerLine[40] = {};
    uint8_t headerLineLength = 0;
    uint16_t contentLength = 0;
    uint8_t *body = nullptr;
    uint16_t bodyCapacity = 0;
    uint16_t bodyReceived = 0;
    int8_t wTemp[forecast::slots] = {};
    uint16_t wWeather[forecast::slots] = {};
    uint8_t daytime = 0;
    uint8_t shownSlot = 0;
    bool forecastValid = false;
    uint32_t forecastMillis = 0;
};

extern OpenWMap weather;
