#!/usr/bin/env python3
"""Generate Jekyll posts for bi4wmslabs.github.io from docs/blog/*.md.

The blog uses a single-file bilingual layout:
  - front matter: title / title_zh / excerpt / excerpt_zh / series fields
  - body: two divs, .bi-lang-en and .bi-lang-zh, each opened by a language sentinel
  - every post opens with a .bi-series masthead (series name + prev/next nav)

Run from anywhere:  python docs/blog/tools/publish_site.py
"""
import re
import shutil
from pathlib import Path

BLOG = Path(__file__).resolve().parents[1]
SITE = Path("H:/github/bi4wmslabs.github.io")

SERIES_ZH = "从一块灯板到一台中文字钟"
SERIES_EN = "From an LED Board to a Chinese Word Clock"
SERIES_SLUG = "wordclock"

POSTS = [
    dict(
        src="00-from-hardware-to-chinese-firmware.md",
        slug="wordclock-00-prologue",
        date="2026-11-10 20:00:00 +0800",
        title="From an LED Board to a Chinese Word Clock",
        title_zh="00｜序章：硬件第一版，与中文丝印上那些空白格的去处",
        excerpt="The open-source base, three stacked boards, and the blank cells on the Chinese film that had to be filled.",
        excerpt_zh="官方开源底座、三层硬件结构，以及中文丝印上那些必须被填上的空白格。",
        categories="[wordclock, hardware]",
        tags="[word-clock, esp8266, hardware, chinese]",
        nav_zh="系列起点",
        nav_en="Series start",
    ),
    dict(
        src="01-why-the-weather-rows-stayed-dark.md",
        slug="wordclock-01-weather-rows-dark",
        date="2026-11-11 20:00:00 +0800",
        title="Two Dead Rows: an Investigation That Started at “the Display Looks Fine”",
        title_zh="01｜天气两行全灭：一次「显示没问题」的排查",
        excerpt="Time and weekday were perfect, weather and temperature were black. One invariant moved the fault from the display to the data layer.",
        excerpt_zh="时间、星期都对，天气和温度两行全黑。一条判据把故障从显示层搬到数据层，外带两个白跑半天的陷阱。",
        categories="[wordclock, firmware]",
        tags="[word-clock, esp8266, debugging, ota]",
    ),
    dict(
        src="02-half-connected-network.md",
        slug="wordclock-02-half-connected-network",
        date="2026-11-12 20:00:00 +0800",
        title="TCP Connects, but No Response Ever Arrives",
        title_zh="02｜TCP 连得上，却永远等不到响应：一次「半连通」网络的排查",
        excerpt="A 248 ms handshake followed by ten seconds of silence. How to split “network down” into layers you can actually test.",
        excerpt_zh="握手 248 毫秒成功，然后十秒静默。把「网络不通」拆成几层可验证的假设，以及为什么最后换了数据源。",
        categories="[wordclock, firmware]",
        tags="[word-clock, networking, tcp, debugging]",
    ),
    dict(
        src="03-tls-and-gzip-in-30kb-heap.md",
        slug="wordclock-03-tls-gzip-30kb-heap",
        date="2026-11-13 20:00:00 +0800",
        title="TLS and 6 KB of JSON in a 30 KB Heap",
        title_zh="03｜在 30 KB 堆里同时装下 TLS 和 6 KB JSON",
        excerpt="BearSSL wants 16 KB before the handshake. MFLN, disconnect-then-inflate, and a byte-level check on the PC.",
        excerpt_zh="BearSSL 默认缓冲一开就是 16 KB。MFLN 压缩记录、先断连再解压，以及一次 PC 端字节级验证。",
        categories="[wordclock, firmware]",
        tags="[word-clock, esp8266, tls, gzip, memory]",
    ),
    dict(
        src="04-boot-loop-and-heap-fragmentation.md",
        slug="wordclock-04-boot-loop-heap",
        date="2026-11-14 20:00:00 +0800",
        title="wdt reset on Boot: From Static Init to Heap Fragmentation",
        title_zh="04｜开机就 wdt reset：从静态初始化到堆碎片",
        excerpt="The board would not boot. Then 19 KB free and malloc still failed. Two ends of the same weak spot.",
        excerpt_zh="烧进去就起不来，只有一行 wdt reset；修好后又冒出「空闲 19 KB 却分配失败」。同一处薄弱点的两端。",
        categories="[wordclock, firmware]",
        tags="[word-clock, esp8266, watchdog, heap]",
    ),
    dict(
        src="05-one-degree-chinese-clock.md",
        slug="wordclock-05-one-degree",
        date="2026-11-15 20:00:00 +0800",
        title="One Degree on a Chinese Word Clock",
        title_zh="05｜让中文字钟精确到 1℃：字模、布局与丝印的联动改造",
        excerpt="The clock could only show 5 °C steps. One degree meant a new glyph set, a rewritten panel, and a scripted film check.",
        excerpt_zh="原来只能显示 5℃ 一档。要精确到 1℃，得新造字模、重刻面板，并用脚本代替目测校验丝印。",
        categories="[wordclock, hardware]",
        tags="[word-clock, chinese, glyphs, silkscreen]",
    ),
    dict(
        src="06-keyless-weather-and-chinese-ui.md",
        slug="wordclock-06-keyless-city",
        date="2026-11-16 09:00:00 +0800",
        title="Just Type a City: Keyless Weather and a Chinese UI",
        title_zh="06｜只填一个城市：免 key 天气源与中文界面的改造",
        excerpt="Four configuration steps became one, and the web UI finally opens in Chinese — after the zh-CN i18n trap.",
        excerpt_zh="四步配置压成一步，网页第一次打开就是中文——中间踩了 zh-CN 落回英文的坑。",
        categories="[wordclock, documentation]",
        tags="[word-clock, weather, i18n, web-ui]",
    ),
]

NEXT_HOOK_ZH = {
    "wordclock-00-prologue": "下一篇：时间、星期全对，天气和温度两行却一个灯都不亮——先从「是不是灯板坏了」查起。",
    "wordclock-01-weather-rows-dark": "下一篇：日志说连上了，可一个字节都等不回来——握手的成功是怎么骗人的。",
    "wordclock-02-half-connected-network": "下一篇：换到 HTTPS，握手成功了，紧接着 30 KB 的堆不够用——TLS 和 gzip 怎么挤进同一块内存。",
    "wordclock-03-tls-gzip-30kb-heap": "下一篇：功能齐了，设备却在开机阶段翻车，串口只剩一行 wdt reset。",
    "wordclock-04-boot-loop-heap": "下一篇：设备稳定了，温度却只有 5℃ 台阶——为了 1℃，得回硬件再要一行字。",
    "wordclock-05-one-degree": "下一篇：功能都齐了，配置却要走四步——把它砍到「只填一个城市」。",
    "wordclock-06-keyless-city": "系列到此结束：一块丝印板上的空白格，最终牵出了硬件、固件、字模、网络栈和界面的一整条改造链。",
}

NEXT_HOOK_EN = {
    "wordclock-00-prologue": "Next: time and weekday were perfect, weather and temperature were black — starting with “is the panel broken?”.",
    "wordclock-01-weather-rows-dark": "Next: the log says connected, yet not a single byte comes back — how a successful handshake lies.",
    "wordclock-02-half-connected-network": "Next: HTTPS works, the handshake succeeds, and 30 KB of heap is not enough — fitting TLS and gzip together.",
    "wordclock-03-tls-gzip-30kb-heap": "Next: features are complete and the device stops booting, leaving one line on serial: wdt reset.",
    "wordclock-04-boot-loop-heap": "Next: stable at last, but temperature only moves in 5 °C steps — one degree needs one more row.",
    "wordclock-05-one-degree": "Next: everything works, but configuration takes four steps — cutting it down to one city name.",
    "wordclock-06-keyless-city": "That is the series: one panel of blank cells pulled hardware, firmware, glyphs, a network stack and a UI along with it.",
}

INLINE_CODE = re.compile(r"`[^`]*`")


def typeset_quotes(text: str) -> str:
    """Turn ASCII double quotes into Chinese typographic quotes, skipping code."""
    out, in_fence, open_q = [], False, True
    for line in text.splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            out.append(line)
            continue
        if in_fence:
            out.append(line)
            continue
        pieces = INLINE_CODE.split(line)
        codes = INLINE_CODE.findall(line)
        rebuilt = []
        for i, piece in enumerate(pieces):
            buf = []
            for ch in piece:
                if ch == '"':
                    buf.append("“" if open_q else "”")
                    open_q = not open_q
                else:
                    buf.append(ch)
            rebuilt.append("".join(buf))
            if i < len(codes):
                rebuilt.append(codes[i])
        out.append("".join(rebuilt))
    return "\n".join(out)


def split_source(path: Path):
    raw = path.read_text(encoding="utf-8")
    zh, en = raw.split("\n---\n\n# English version\n")
    return zh, en


def strip_heading(lines):
    """Drop only the leading H1 (zh) / H3 subtitle line, never a section heading."""
    idx = 0
    # skip leading blanks
    while idx < len(lines) and not lines[idx].strip():
        idx += 1
    if idx < len(lines) and lines[idx].startswith("# "):
        idx += 1
        while idx < len(lines) and not lines[idx].strip():
            idx += 1
    if idx < len(lines) and lines[idx].startswith("### "):
        idx += 1
    return lines[idx:]


def body_only(lines):
    text = "\n".join(lines).strip("\n")
    # drop the first horizontal rule that separated the hook from the sections
    text = re.sub(r"\n\n---\n\n", "\n\n", text, count=1)
    text = text.replace("](images/", "](/assets/images/wordclock/")
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip() + "\n"


def masthead(no, lang, prev_slug, next_slug):
    total = len(POSTS)
    if lang == "zh":
        nav = f"字钟开发调试手记 · 第 {no + 1} 篇 / 共 {total} 篇 · <a href=\"/{SERIES_SLUG}-series/\">系列目录</a>"
        if prev_slug:
            nav += f" · <a href=\"/posts/{prev_slug}/\">← 上一篇</a>"
        if next_slug:
            nav += f" · <a href=\"/posts/{next_slug}/\">下一篇 →</a>"
        return (
            '<div class="bi-series">\n'
            f'  <p class="bi-series-name">{SERIES_ZH}</p>\n'
            f'  <p class="bi-series-nav">{nav}</p>\n'
            "</div>\n"
        )
    nav = f"Word Clock Build Notes · Part {no + 1} of {total} · <a href=\"/{SERIES_SLUG}-series/\">Series index</a>"
    if prev_slug:
        nav += f" · <a href=\"/posts/{prev_slug}/\">← Prev</a>"
    if next_slug:
        nav += f" · <a href=\"/posts/{next_slug}/\">Next →</a>"
    return (
        '<div class="bi-series">\n'
        f'  <p class="bi-series-name">{SERIES_EN}</p>\n'
        f'  <p class="bi-series-nav">{nav}</p>\n'
        "</div>\n"
    )


def build_post(i, meta):
    no = i
    prev_slug = POSTS[i - 1]["slug"] if i > 0 else None
    next_slug = POSTS[i + 1]["slug"] if i < len(POSTS) - 1 else None
    zh, en = split_source(BLOG / meta["src"])

    zh_body = body_only(strip_heading(zh.splitlines()))
    en_body = body_only(strip_heading(en.splitlines()))
    zh_body = typeset_quotes(zh_body)

    zh_body = zh_body.rstrip() + "\n\n" + f"*{NEXT_HOOK_ZH[meta['slug']]}*\n"
    en_body = en_body.rstrip() + "\n\n" + f"*{NEXT_HOOK_EN[meta['slug']]}*\n"

    front = f"""---
title: {meta['title']!r}
title_zh: "{meta['title_zh']}"
excerpt: {meta['excerpt']!r}
excerpt_zh: "{meta['excerpt_zh']}"
date: {meta['date']}
categories: {meta['categories']}
tags: {meta['tags']}
permalink: /posts/{meta['slug']}/
series: "{SERIES_ZH}"
series_en: "{SERIES_EN}"
series_no: {no + 1}
series_total: {len(POSTS)}
---

<span data-zh-title="{meta['title_zh']}" hidden></span>

<div class="bi-lang-en" markdown="1">

<span class="bi-lang-mark" data-lang="en" hidden></span>

{masthead(no, "en", prev_slug, next_slug)}
{en_body}
</div>

<div class="bi-lang-zh" markdown="1">

<span class="bi-lang-mark" data-lang="zh" hidden></span>

{masthead(no, "zh", prev_slug, next_slug)}
{zh_body}
</div>
"""
    dest = SITE / "_posts" / f"{meta['date'][:10]}-{meta['slug']}.md"
    dest.write_text(front, encoding="utf-8")
    return dest


def main():
    img_src = BLOG / "images"
    img_dst = SITE / "assets" / "images" / "wordclock"
    img_dst.mkdir(parents=True, exist_ok=True)
    for png in img_src.glob("*.png"):
        shutil.copy2(png, img_dst / png.name)

    for i, meta in enumerate(POSTS):
        print("wrote", build_post(i, meta).name)


if __name__ == "__main__":
    main()
