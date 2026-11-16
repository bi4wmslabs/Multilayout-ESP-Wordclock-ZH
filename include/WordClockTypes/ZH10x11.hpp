#pragma once

#include "ClockType.hpp"

/*
 *           Layout Front (ZH10x11 - 中文)
 *                COL  (display order, left to right)
 *       0 1 2 3 4 5 6 7 8 9 10
 * ROW + - - - - - - - - - - -
 *  0  | 现 在 是 · · · · · · · ·
 *  1  | 早 上 午 中 午 下 午 晚 上 凌 晨
 *  2  | 一 二 三 四 五 六 七 八 九 十 点
 *  3  | 十 一 十 二 · · · · · · ·
 *  4  | 二 十 五 十 五 三 十 五 四 十 五
 *  5  | 分 整 · · · · · · · · ·
 *  6  | 星 期 一 二 三 四 五 六 日 · ·
 *  7  | 多 云 阴 晴 小 大 雨 雷 雾 霾 雪
 *  8  | 零 下 二 十 三 十 · · · · ·
 *  9  | · 一 二 三 四 五 六 七 八 九 度
 *
 * Reading order (top row first, left to right):
 *
 *   现在 是 [时段] [时] 点 [分钟] 分        e.g. 现在是下午三点二十五分
 *   整 replaces 分 on full hours:          e.g. 现在是上午九点整
 *   星期 [一..日] on its own row:          e.g. 星期三
 *
 * Row 1 holds all six daytime words (凌晨/早上/上午/中午/下午/晚上), rows 2-3
 * the hours 1-12 followed by 点, row 4 the minute words in 5 minute steps and
 * row 5 the 分/整 suffix. Row 6 shows the weekday.
 *
 * Row 4 is a shortest common superstring of the eleven minute numbers: every
 * value 5/10/.../55 can be read as a contiguous run of lit cells:
 *
 *   5 五     10 十    15 十五   20 二十   25 二十五
 *  30 三十   35 三十五 40 四十   45 四十五 50 五十   55 五十五
 *
 * Rows 7-9 show the current weather (data from the built-in QWeather client,
 * see ClockWork). Row 7 is a superstring of all weather conditions, rows 8-9
 * spell out the temperature in whole degrees:
 *
 *   row 7: 多云 阴 晴 小雨 大雨 雷 雾霾 雪
 *   row 8: 零/下 (sign) and the tens digit: 十 二十 三十
 *   row 9:  一..九 (ones digit) followed by 度
 *
 * So 23 degC reads 二十 (row 8) 三 度 (row 9) = 二十三度, -5 degC reads
 * 零 下 (row 8) 五 度 (row 9) = 零下五度, 0 degC reads 零 (row 8) 度 (row 9).
 *
 * Row 9 replaces the decorative couplet the earlier revision carried there;
 * the ones digit needed real digit cells. The remaining cells hold a plain
 * dot and are easy to replace with own characters.
 *
 * The four remainder minute LEDs are the usual MINUTE_LED4x corner LEDs at
 * the end of the strip (1-4 minutes after the last shown 5 minute step).
 */

class Zh10x11_t : public ClockType {
private:
    /*
     * Draw the cells from display column `from` to `to` (both inclusive,
     * counted from the LEFT edge exactly like the picture above). The front
     * matrix numbers its columns from the right, so the flip happens here
     * once and every drawWord() case can be read straight off the picture.
     */
    void cells(const uint8_t row, const uint8_t from, const uint8_t to) {
        setFrontMatrixWord(row, static_cast<uint8_t>(colsWordMatrix() - 1 - to),
                           static_cast<uint8_t>(colsWordMatrix() - 1 - from));
    }

    void cells(const uint8_t row, const uint8_t col) { cells(row, col, col); }

public:
    virtual LanguageAbbreviation usedLang() override {
        return LanguageAbbreviation::ZH;
    };

    //------------------------------------------------------------------------------

    /* The clock face is rendered by ClockWork::setChineseClock(). */
    virtual bool usesChineseTimeFormat() override { return true; }

    //------------------------------------------------------------------------------

    /* 凌晨/早上/上午/中午/下午/晚上 */
    virtual bool hasDaytimeWords() override { return true; }

    //------------------------------------------------------------------------------

    /* Show weather on rows 7-8 (fetched by the built-in OpenWeatherMap
     * client; the words are drawn by ClockWork::setChineseClock()). */
    virtual bool hasWeatherData() override { return true; }

    //------------------------------------------------------------------------------

    void drawWord(FrontWord word) override {
        switch (word) {

        case FrontWord::es_ist:
            /* 现在是 */
            this->cells(0, 0, 2);
            break;

        case FrontWord::mitternachts:
        case FrontWord::day_night:
            /* 凌晨 (also used for hour 0, where 十二点 is shown) */
            this->cells(1, 9, 10);
            break;

        case FrontWord::day_early:
            /* 早上 (05:00 - 09:59) */
            this->cells(1, 0, 1);
            break;

        case FrontWord::day_morning:
            /* 上午 (10:00 - 11:59) */
            this->cells(1, 1, 2);
            break;

        case FrontWord::day_noon:
            /* 中午 (12:00 - 13:59) */
            this->cells(1, 3, 4);
            break;

        case FrontWord::day_afternoon:
            /* 下午 (14:00 - 17:59) */
            this->cells(1, 5, 6);
            break;

        case FrontWord::day_evening:
            /* 晚上 (18:00 - 23:59) */
            this->cells(1, 7, 8);
            break;

        case FrontWord::uhr:
            /* 点 - only as an alias, the hour words always draw it below */
            this->cells(2, 10);
            break;

        case FrontWord::hour_1:
            /* 一点 */
            this->cells(2, 0);
            this->cells(2, 10);
            break;

        case FrontWord::hour_2:
            /* 二点 */
            this->cells(2, 1);
            this->cells(2, 10);
            break;

        case FrontWord::hour_3:
            /* 三点 */
            this->cells(2, 2);
            this->cells(2, 10);
            break;

        case FrontWord::hour_4:
            /* 四点 */
            this->cells(2, 3);
            this->cells(2, 10);
            break;

        case FrontWord::hour_5:
            /* 五点 */
            this->cells(2, 4);
            this->cells(2, 10);
            break;

        case FrontWord::hour_6:
            /* 六点 */
            this->cells(2, 5);
            this->cells(2, 10);
            break;

        case FrontWord::hour_7:
            /* 七点 */
            this->cells(2, 6);
            this->cells(2, 10);
            break;

        case FrontWord::hour_8:
            /* 八点 */
            this->cells(2, 7);
            this->cells(2, 10);
            break;

        case FrontWord::hour_9:
            /* 九点 */
            this->cells(2, 8);
            this->cells(2, 10);
            break;

        case FrontWord::hour_10:
            /* 十点 */
            this->cells(2, 9);
            this->cells(2, 10);
            break;

        case FrontWord::hour_11:
            /* 十一点 */
            this->cells(3, 0, 1);
            this->cells(2, 10);
            break;

        case FrontWord::hour_12:
            /* 十二点 */
            this->cells(3, 2, 3);
            this->cells(2, 10);
            break;

        case FrontWord::min_5:
            /* 五分 */
            this->cells(4, 2);
            this->cells(5, 0);
            break;

        case FrontWord::min_10:
            /* 十分 */
            this->cells(4, 1);
            this->cells(5, 0);
            break;

        case FrontWord::min_15:
            /* 十五分 */
            this->cells(4, 1, 2);
            this->cells(5, 0);
            break;

        case FrontWord::min_20:
            /* 二十分 */
            this->cells(4, 0, 1);
            this->cells(5, 0);
            break;

        case FrontWord::min_25:
            /* 二十五分 */
            this->cells(4, 0, 2);
            this->cells(5, 0);
            break;

        case FrontWord::min_30:
            /* 三十分 */
            this->cells(4, 5, 6);
            this->cells(5, 0);
            break;

        case FrontWord::min_35:
            /* 三十五分 */
            this->cells(4, 5, 7);
            this->cells(5, 0);
            break;

        case FrontWord::min_40:
            /* 四十分 */
            this->cells(4, 8, 9);
            this->cells(5, 0);
            break;

        case FrontWord::min_45:
            /* 四十五分 */
            this->cells(4, 8, 10);
            this->cells(5, 0);
            break;

        case FrontWord::min_50:
            /* 五十分 */
            this->cells(4, 2, 3);
            this->cells(5, 0);
            break;

        case FrontWord::min_55:
            /* 五十五分 */
            this->cells(4, 2, 4);
            this->cells(5, 0);
            break;

        case FrontWord::minute:
            /* 分 - suffix, also drawn by the min_* cases above */
            this->cells(5, 0);
            break;

        case FrontWord::full_hour:
            /* 整 - full hour, replaces 分 */
            this->cells(5, 1);
            break;

        case FrontWord::weekday_1:
            /* 星期一 */
            this->cells(6, 0, 1);
            this->cells(6, 2);
            break;

        case FrontWord::weekday_2:
            /* 星期二 */
            this->cells(6, 0, 1);
            this->cells(6, 3);
            break;

        case FrontWord::weekday_3:
            /* 星期三 */
            this->cells(6, 0, 1);
            this->cells(6, 4);
            break;

        case FrontWord::weekday_4:
            /* 星期四 */
            this->cells(6, 0, 1);
            this->cells(6, 5);
            break;

        case FrontWord::weekday_5:
            /* 星期五 */
            this->cells(6, 0, 1);
            this->cells(6, 6);
            break;

        case FrontWord::weekday_6:
            /* 星期六 */
            this->cells(6, 0, 1);
            this->cells(6, 7);
            break;

        case FrontWord::weekday_7:
            /* 星期日 */
            this->cells(6, 0, 1);
            this->cells(6, 8);
            break;

        /*---- row 7: weather condition 多云阴晴小大雨雷雾霾雪 ----*/

        case FrontWord::zh_duo: /* 多 (多云) */
            this->cells(7, 0);
            break;

        case FrontWord::zh_yun: /* 云 (多云) */
            this->cells(7, 1);
            break;

        case FrontWord::zh_yin: /* 阴 */
            this->cells(7, 2);
            break;

        case FrontWord::zh_qing: /* 晴 */
            this->cells(7, 3);
            break;

        case FrontWord::zh_xiao: /* 小 (小雨) */
            this->cells(7, 4);
            break;

        case FrontWord::zh_da: /* 大 (大雨) */
            this->cells(7, 5);
            break;

        case FrontWord::zh_yu: /* 雨 (小雨/大雨) */
            this->cells(7, 6);
            break;

        case FrontWord::zh_lei: /* 雷 */
            this->cells(7, 7);
            break;

        case FrontWord::zh_wu: /* 雾 */
            this->cells(7, 8);
            break;

        case FrontWord::zh_mai: /* 霾 */
            this->cells(7, 9);
            break;

        case FrontWord::zh_xue: /* 雪 */
            this->cells(7, 10);
            break;

        /*---- row 8: sign and tens digit 零 下 二 十 三 十 ----*/

        case FrontWord::zh_ling: /* 零 (零下 / 零度) */
            this->cells(8, 0);
            break;

        case FrontWord::zh_xia: /* 下 (零下) */
            this->cells(8, 1);
            break;

        case FrontWord::zh_er_ten: /* 二 (二十) */
            this->cells(8, 2);
            break;

        case FrontWord::zh_shi: /* 十 (十/十五/二十三) */
            this->cells(8, 3);
            break;

        case FrontWord::zh_san_ten: /* 三 (三十) */
            this->cells(8, 4);
            break;

        case FrontWord::zh_shi2: /* 十 (三十) */
            this->cells(8, 5);
            break;

        /*---- row 9: ones digit 一 二 三 四 五 六 七 八 九 度 ----*/

        case FrontWord::zh_yi: /* 一 */
            this->cells(9, 1);
            break;

        case FrontWord::zh_er: /* 二 */
            this->cells(9, 2);
            break;

        case FrontWord::zh_san: /* 三 */
            this->cells(9, 3);
            break;

        case FrontWord::zh_si: /* 四 */
            this->cells(9, 4);
            break;

        case FrontWord::zh_wu_num: /* 五 */
            this->cells(9, 5);
            break;

        case FrontWord::zh_liu: /* 六 */
            this->cells(9, 6);
            break;

        case FrontWord::zh_qi: /* 七 */
            this->cells(9, 7);
            break;

        case FrontWord::zh_ba: /* 八 */
            this->cells(9, 8);
            break;

        case FrontWord::zh_jiu: /* 九 */
            this->cells(9, 9);
            break;

        case FrontWord::zh_du: /* 度 */
            this->cells(9, 10);
            break;

        default:
            break;
        };
    };
};

Zh10x11_t _zh10x11;
