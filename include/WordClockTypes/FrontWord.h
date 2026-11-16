#pragma once

enum class FrontWord {
    error,
    min_1,
    min_2,
    min_3,
    min_4,
    min_5,
    min_6,
    min_7,
    min_8,
    min_9,
    min_10,
    min_11,
    min_12,
    min_13,
    min_14,
    min_15,
    min_16,
    min_17,
    min_18,
    min_19,
    min_20,
    min_21,
    min_22,
    min_23,
    min_24,
    min_25,
    min_26,
    min_27,
    min_28,
    min_29,
    min_30,
    min_31,
    min_32,
    min_33,
    min_34,
    min_35,
    min_36,
    min_37,
    min_38,
    min_39,
    min_40,
    min_41,
    min_42,
    min_43,
    min_44,
    min_45,
    min_46,
    min_47,
    min_48,
    min_49,
    min_50,
    min_51,
    min_52,
    min_53,
    min_54,
    min_55,
    min_56,
    min_57,
    min_58,
    min_59,
    m_num1,
    m_num2,
    m_num3,
    m_num4,

    es_ist,
    es_ist__singular__,
    es_ist___plural___,
    nach,
    vor,
    viertel,
    dreiviertel,
    uhr,
    halb,
    eins,
    minute,
    minuten,
    und,
    v_vor,
    v_nach,
    a_quarter,
    nur,
    gewesen,

    hour_0,
    hour_1,
    hour_2,
    hour_3,
    hour_4,
    hour_5,
    hour_6,
    hour_7,
    hour_8,
    hour_9,
    hour_10,
    hour_11,
    hour_12,
    hour_13,
    hour_14,
    hour_15,
    hour_16,
    hour_17,
    hour_18,
    hour_19,
    hour_20,
    hour_21,
    hour_22,
    hour_23,

    day_morning,
    day_early,
    day_evening,
    day_noon,
    day_night,

    w_morgen,
    w_frueh,
    w_abend,
    w_mittag,
    w_nacht,
    mitternachts,
    w_schnee,
    w_klar,
    w_warnung,
    w_regen,
    w_wolken,
    w_gewitter,
    w_unter,
    w_ueber,
    w_minus,
    w_null,
    w_fuenf,
    w_zehn,
    w_und,
    w_zwanzig,
    w_dreissig,
    w_grad,

    plus,
    day_afternoon,
    full_hour,
    weekday_1,
    weekday_2,
    weekday_3,
    weekday_4,
    weekday_5,
    weekday_6,
    weekday_7,
    happy_birthday,

    /* Chinese weather display (ZH10x11, one cell per character) */
    zh_duo,   /* 多 */
    zh_yun,   /* 云 */
    zh_yin,   /* 阴 */
    zh_qing,  /* 晴 */
    zh_xiao,  /* 小 */
    zh_da,    /* 大 */
    zh_yu,    /* 雨 */
    zh_lei,   /* 雷 */
    zh_wu,    /* 雾 */
    zh_mai,   /* 霾 */
    zh_xue,   /* 雪 */

    /* Row 8: sign and tens digit (零下 十 二十 三十) */
    zh_ling,    /* 零 (零下 / 零度) */
    zh_xia,     /* 下 (零下) */
    zh_shi,     /* 十 (十/十五) */
    zh_er_ten,  /* 二 (二十) */
    zh_san_ten, /* 三 (三十) */
    zh_shi2,    /* 十 (三十) */

    /* Row 9: ones digit followed by 度 */
    zh_yi,    /* 一 */
    zh_er,    /* 二 */
    zh_san,   /* 三 */
    zh_si,    /* 四 */
    zh_wu_num,/* 五 */
    zh_liu,   /* 六 */
    zh_qi,    /* 七 */
    zh_ba,    /* 八 */
    zh_jiu,   /* 九 */
    zh_du     /* 度 */
};
