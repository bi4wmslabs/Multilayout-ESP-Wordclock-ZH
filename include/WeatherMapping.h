#pragma once

#include "WordClockTypes/FrontWord.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/*--------------------------------------------------
List of Conditions http://openweathermap.org/weather-conditions
Weather condition code (wetterid):

2xx Thunderstorm
3xx Drizzle
5xx Rain
6xx Snow
7xx Atmosphere: 701 mist, 721 haze and 741 fog show as clouds,
    smoke, dust, sand, ash, squalls and tornado as a warning
800 Clear
80x Clouds
--------------------------------------------------*/

namespace forecast {

const uint8_t slots = 4;
const uint8_t maxWords = 9;

inline int8_t temperatureStep(double celsius) {
    if (celsius >= 30) {
        return 30;
    } else if (celsius >= 0 && celsius <= 5) {
        return 1;
    } else if (celsius < 0 && celsius >= -5) {
        return -1;
    } else if (celsius <= -25) {
        return -25;
    }
    return static_cast<int8_t>(celsius - fmod(celsius, 5));
}

/*--------------------------------------------------
Whole degrees for the Chinese face, which spells the temperature out digit by
digit (二十三度). The layout covers -39..39; anything beyond is clamped so the
words still fit on the two rows.
--------------------------------------------------*/
inline int8_t temperatureCelsius(double celsius) {
    const double rounded = celsius >= 0 ? celsius + 0.5 : celsius - 0.5;
    int value = static_cast<int>(rounded);
    if (value > 39) {
        value = 39;
    } else if (value < -39) {
        value = -39;
    }
    return static_cast<int8_t>(value);
}

inline uint16_t conditionGroup(uint16_t id) {
    if (id == 800) {
        return 800;
    } else if ((id > 800 && id < 900) || id == 701 || id == 721 || id == 741) {
        return 801;
    } else if (id >= 200 && id < 800) {
        return id / 100 * 100;
    }
    return 0;
}

/*--------------------------------------------------
QWeather (和风天气) reports a three digit icon code instead of the numeric
OpenWeatherMap ids. Only the groups below are understood by the layouts, so the
icons are folded onto them:

    100 / 150                    clear sky              -> 800  晴
    101 - 103, 151 - 153         few / partly cloudy    -> 801  多云
    104, 154                     overcast               -> 802  阴
    302 - 304                    thunderstorm           -> 200  雷
    305, 309, 313, 314           light rain / drizzle   -> 300  小雨
    306 - 399                    moderate to heavy rain -> 500  大雨
    4xx                          snow / sleet           -> 600  雪
    5xx                          fog, haze, dust        -> 700  雾霾

    802 is the one group that OpenWeatherMap does not know: OWM folds overcast
    into 80x, QWeather separates 阴 from 多云, and the Chinese layout has a
    word of its own for it.
--------------------------------------------------*/
inline uint16_t conditionGroupFromIcon(const char *icon) {
    if (icon == nullptr) {
        return 0;
    }
    const int id = atoi(icon);
    if (id == 100 || id == 150) {
        return 800;
    }
    if (id == 101 || id == 102 || id == 103 || id == 151 || id == 152 ||
        id == 153) {
        return 801;
    }
    if (id == 104 || id == 154) {
        return 802;
    }
    if (id >= 302 && id <= 304) {
        return 200;
    }
    if (id == 305 || id == 309 || id == 313 || id == 314) {
        return 300;
    }
    if (id >= 300 && id <= 399) {
        return 500;
    }
    if (id >= 400 && id <= 499) {
        return 600;
    }
    if (id >= 500 && id <= 599) {
        return 700;
    }
    return 0;
}

inline uint8_t daytimeFor(uint8_t hour) { return ((hour + 21) % 24) / 6; }

/*--------------------------------------------------
Open-Meteo reports WMO weather codes (0..99) instead of an icon string. The
codes are folded onto the same groups as above, so the layouts do not need to
know which service the data came from:

    0                    clear                -> 800  晴
    1, 2                 mainly clear, partly -> 801  多云
    3                    overcast             -> 802  阴
    45, 48               fog                  -> 700  雾
    51..57               drizzle              -> 300  小雨
    61, 80               slight rain/showers  -> 300  小雨
    63..67, 81, 82       rain, freezing rain  -> 500  大雨
    71..77, 85, 86       snow, snow showers   -> 600  雪
    95..99               thunderstorm         -> 200  雷
--------------------------------------------------*/
inline uint16_t conditionGroupFromWmo(int code) {
    if (code == 0) {
        return 800;
    }
    if (code == 1 || code == 2) {
        return 801;
    }
    if (code == 3) {
        return 802;
    }
    if (code == 45 || code == 48) {
        return 700;
    }
    if (code >= 51 && code <= 57) {
        return 300;
    }
    if (code == 61 || code == 80) {
        return 300;
    }
    if ((code >= 63 && code <= 67) || code == 81 || code == 82) {
        return 500;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return 600;
    }
    if (code >= 95 && code <= 99) {
        return 200;
    }
    return 0;
}

/*--------------------------------------------------
Plausibility checks for the two QWeather settings.

Both live in flash that an older firmware never wrote, so after an update the
fields can contain whatever was left in the EEPROM sector. A garbage host name
is not a cosmetic problem: the DNS lookup for it blocks the loop task long
enough to trip the hardware watchdog, which turns into a reboot loop that looks
like a dead clock. Only values that cannot be mistaken for garbage are
accepted.
--------------------------------------------------*/
namespace qweather {

inline bool hostIsPlausible(const char *host) {
    if (host == nullptr) {
        return false;
    }
    size_t length = 0;
    while (host[length] != '\0') {
        length++;
    }
    if (length < 8 || length > 60) {
        return false;
    }
    bool hasDot = false;
    for (size_t i = 0; i < length; i++) {
        const char c = host[i];
        if (c == '.') {
            hasDot = true;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return hasDot && host[0] != '.' && host[0] != '-' &&
           host[length - 1] != '.' && host[length - 1] != '-';
}

inline bool locationIsPlausible(const char *location) {
    if (location == nullptr) {
        return false;
    }
    size_t length = 0;
    while (location[length] != '\0') {
        length++;
    }
    /* QWeather location ids are nine digits; six is the shortest id in the
     * database, eleven keeps a little room for future extensions. */
    if (length < 6 || length > 11) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (location[i] < '0' || location[i] > '9') {
            return false;
        }
    }
    return true;
}

} // namespace qweather

/*--------------------------------------------------
The Open-Meteo settings consist of a single city name, typed by the user. It is
stored as UTF-8, so the bytes above 0x7F are expected; what is not acceptable
are control characters and the 0xFF that an EEPROM which was never written
contains. A name that fails here is dropped before it reaches the URL builder.
--------------------------------------------------*/
namespace openmeteo {

inline bool cityIsPlausible(const char *city) {
    if (city == nullptr) {
        return false;
    }
    size_t length = 0;
    while (length < 40 && city[length] != '\0') {
        const unsigned char c = static_cast<unsigned char>(city[length]);
        if ((c < 0x20 && c != '\t') || c == 0x7F || c == 0xFF) {
            return false;
        }
        length++;
    }
    return length > 0 && length < 40;
}

} // namespace openmeteo

struct Words {
    FrontWord word[maxWords];
    uint8_t count;

    const FrontWord *begin() const { return word; }
    const FrontWord *end() const { return word + count; }
};

namespace detail {

inline void append(Words &words, FrontWord word) {
    if (word != FrontWord{} && words.count < maxWords) {
        words.word[words.count++] = word;
    }
}

template <size_t N> void append(Words &words, const FrontWord (&add)[N]) {
    for (FrontWord word : add) {
        append(words, word);
    }
}

inline void appendDaytime(Words &words, uint8_t index) {
    static const FrontWord daytimeWords[7][2] = {
        {FrontWord::w_mittag},
        {FrontWord::w_abend},
        {FrontWord::w_nacht},
        {FrontWord::w_morgen, FrontWord::w_frueh},
        {FrontWord::w_morgen, FrontWord::w_mittag},
        {FrontWord::w_morgen, FrontWord::w_abend},
        {FrontWord::w_morgen, FrontWord::w_nacht},
    };
    if (index < 7) {
        append(words, daytimeWords[index]);
    }
}

inline void appendTemperature(Words &words, int8_t temperature) {
    struct Degrees {
        uint8_t value;
        FrontWord number[3];
    };
    static const Degrees degrees[] = {
        {1, {FrontWord::w_null}},
        {5, {FrontWord::w_fuenf}},
        {10, {FrontWord::w_zehn}},
        {15, {FrontWord::w_fuenf, FrontWord::w_zehn}},
        {20, {FrontWord::w_zwanzig}},
        {25, {FrontWord::w_fuenf, FrontWord::w_und, FrontWord::w_zwanzig}},
        {30, {FrontWord::w_dreissig}},
    };

    for (const Degrees &entry : degrees) {
        if (entry.value != abs(temperature)) {
            continue;
        }
        if (temperature > 0) {
            append(words, FrontWord::w_ueber);
        } else {
            append(words, FrontWord::w_unter);
            append(words, FrontWord::w_minus);
        }
        append(words, entry.number);
        append(words, FrontWord::w_grad);
        return;
    }
}

inline void appendCondition(Words &words, uint16_t condition) {
    switch (condition) {
    case 200:
        append(words, FrontWord::w_gewitter);
        break;
    case 300:
    case 500:
        append(words, FrontWord::w_regen);
        break;
    case 600:
        append(words, FrontWord::w_schnee);
        break;
    case 700:
        append(words, FrontWord::w_warnung);
        break;
    case 800:
        append(words, FrontWord::w_klar);
        break;
    case 801:
        append(words, FrontWord::w_wolken);
        break;
    /* Overcast. OpenWeatherMap never produces this group, but QWeather does, so
     * the German weather layouts keep their 多云 wording instead of going
     * blank. */
    case 802:
        append(words, FrontWord::w_wolken);
        break;
    }
}

} // namespace detail

inline Words wordsFor(uint8_t slot, uint8_t daytime, int8_t temperature,
                      uint16_t condition) {
    Words words = {};
    detail::appendDaytime(words, slot + daytime);
    detail::appendTemperature(words, temperature);
    detail::appendCondition(words, condition);
    return words;
}

} // namespace forecast
