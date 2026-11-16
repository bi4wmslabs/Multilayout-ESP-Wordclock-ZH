#!/usr/bin/env python3
"""Render the Chinese half of each article into WeChat-paste-ready HTML.

WeChat's editor drops <style> blocks and class selectors, so every element here
carries its own inline style. The output is one self-contained .html per part,
plus an index with titles and draft summaries.

    python docs/blog/tools/make_wechat.py
"""
import html
import re
from pathlib import Path

BLOG = Path(__file__).resolve().parents[1]
OUT = BLOG / "wechat"
IMG_BASE = "https://bi4wmslabs.github.io/assets/images/wordclock/"

SERIES = "从一块灯板到一台中文字钟"

PARTS = [
    ("00-from-hardware-to-chinese-firmware.md", "wordclock-00-prologue",
     "从一块灯板到一台中文字钟｜00 序章：硬件第一版与中文丝印上的空白格",
     "官方开源底座、三层硬件结构，以及中文丝印上那些必须被填上的空白格。"),
    ("01-why-the-weather-rows-stayed-dark.md", "wordclock-01-weather-rows-dark",
     "从一块灯板到一台中文字钟｜01 两个灯不亮，我查了三个小时",
     "时间、星期都对，天气和温度两行全黑。一条判据把故障从显示层搬到数据层。"),
    ("02-half-connected-network.md", "wordclock-02-half-connected-network",
     "从一块灯板到一台中文字钟｜02 TCP 握手成功，却收不到一个字节",
     "握手 248 毫秒成功，然后十秒静默。把「网络不通」拆成几层可验证的假设。"),
    ("03-tls-and-gzip-in-30kb-heap.md", "wordclock-03-tls-gzip-30kb-heap",
     "从一块灯板到一台中文字钟｜03 ESP8266 只剩 30 KB 堆，怎么跑 HTTPS",
     "BearSSL 默认缓冲一开就是 16 KB。MFLN 压缩记录、先断连再解压。"),
    ("04-boot-loop-and-heap-fragmentation.md", "wordclock-04-boot-loop-heap",
     "从一块灯板到一台中文字钟｜04 开机就重启：看门狗、静态初始化与堆碎片",
     "烧进去就起不来，只有一行 wdt reset；修好后又冒出「空闲 19 KB 却分配失败」。"),
    ("05-one-degree-chinese-clock.md", "wordclock-05-one-degree",
     "从一块灯板到一台中文字钟｜05 想精确到 1℃，为什么必须换一块面板",
     "原来只能显示 5℃ 一档。要精确到 1℃，得新造字模、重刻面板、脚本校验丝印。"),
    ("06-keyless-weather-and-chinese-ui.md", "wordclock-06-keyless-city",
     "从一块灯板到一台中文字钟｜06 把天气配置砍到「只填城市」",
     "四步配置压成一步，网页第一次打开就是中文——中间踩了 zh-CN 落回英文的坑。"),
]

FONT = "-apple-system,BlinkMacSystemFont,'Helvetica Neue','PingFang SC','Hiragino Sans GB','Microsoft YaHei',sans-serif"
MONO = "Menlo,Consolas,'Courier New',monospace"

S_P = f"margin:0 0 18px;font-size:16px;line-height:1.8;color:#2f3336;letter-spacing:.3px;font-family:{FONT};"
S_H2 = f"margin:34px 0 16px;padding-left:10px;border-left:4px solid #c0523f;font-size:18px;font-weight:700;line-height:1.5;color:#1b1f23;letter-spacing:.2px;font-family:{FONT};"
S_UL = f"margin:0 0 18px;padding-left:22px;font-size:16px;line-height:1.8;color:#2f3336;font-family:{FONT};"
S_LI = "margin:0 0 8px;"
S_CODE = f"background:#f4f5f7;padding:2px 5px;border-radius:3px;font-family:{MONO};font-size:14px;color:#c7254e;"
S_PRE = f"margin:0 0 18px;padding:12px 14px;background:#f6f7f9;border:1px solid #e6e8eb;border-radius:6px;overflow-x:auto;font-family:{MONO};font-size:12.5px;line-height:1.65;color:#333;white-space:pre;"
S_QUOTE = f"margin:0 0 22px;padding:12px 16px;background:#faf7f5;border-left:3px solid #d8b7ae;font-size:15px;line-height:1.8;color:#5d5a57;font-family:{FONT};"
S_CALLOUT = f"margin:0 0 18px;padding:12px 16px;background:#fdf4f1;border:1px solid #f0d9d2;border-radius:6px;font-size:15px;line-height:1.8;color:#6a4a42;font-family:{FONT};"
S_TABLE = f"width:100%;border-collapse:collapse;margin:0 0 20px;font-size:14px;font-family:{FONT};"
S_TH = "border:1px solid #e1e4e8;padding:7px 9px;background:#fafafa;font-weight:700;color:#1b1f23;text-align:left;"
S_TD = "border:1px solid #e1e4e8;padding:7px 9px;color:#3a3f45;"
S_IMG = "display:block;max-width:100%;margin:20px auto;border-radius:4px;"
S_FOOT = f"margin:30px 0 0;padding-top:14px;border-top:1px solid #eceff2;font-size:13px;line-height:1.8;color:#8b9199;font-family:{FONT};"


def inline(text: str) -> str:
    """Escape and apply **bold**, *italic*, `code`, [text](url)."""
    codes = []

    def stash(m):
        codes.append(m.group(1))
        return f"\x00{len(codes) - 1}\x00"

    text = re.sub(r"`([^`]+)`", stash, text)
    text = html.escape(text, quote=False)
    text = re.sub(r"\*\*([^*]+)\*\*", r'<strong style="font-weight:700;color:#111;">\1</strong>', text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"\1", text)  # WeChat: links are not clickable
    for i, code in enumerate(codes):
        text = text.replace(
            f"\x00{i}\x00",
            f'<code style="{S_CODE}">{html.escape(code, quote=False)}</code>',
        )
    return text


def image(line: str):
    m = re.match(r"!\[([^\]]*)\]\(([^)]+)\)", line.strip())
    if not m:
        return None
    alt, src = m.group(1), m.group(2)
    src = IMG_BASE + Path(src).name
    return f'<p style="margin:20px 0;text-align:center;"><img src="{src}" alt="{html.escape(alt)}" style="{S_IMG}"/></p>'


def table(rows):
    head, body = rows[0], rows[2:]
    out = [f'<table style="{S_TABLE}">']
    out.append("<thead><tr>")
    for cell in head:
        out.append(f'<th style="{S_TH}">{inline(cell)}</th>')
    out.append("</tr></thead><tbody>")
    for row in body:
        out.append("<tr>")
        for cell in row:
            out.append(f'<td style="{S_TD}">{inline(cell)}</td>')
        out.append("</tr>")
    out.append("</tbody></table>")
    return "".join(out)


def split_row(line: str):
    cells = [c.strip() for c in line.strip().strip("|").split("|")]
    return cells


def render(md: str) -> str:
    lines = md.splitlines()
    out, i, para = [], 0, []

    def flush_para():
        if para:
            text = " ".join(p.strip() for p in para)
            out.append(f'<p style="{S_P}">{inline(text)}</p>')
            para.clear()

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if not stripped:
            flush_para()
            i += 1
            continue

        if stripped.startswith("```"):
            flush_para()
            i += 1
            buf = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            code = html.escape("\n".join(buf), quote=False)
            out.append(f'<pre style="{S_PRE}">{code}</pre>')
            continue

        if stripped.startswith("## "):
            flush_para()
            out.append(f'<h2 style="{S_H2}">{inline(stripped[3:])}</h2>')
            i += 1
            continue

        if stripped.startswith(">"):
            flush_para()
            buf = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                buf.append(lines[i].strip()[1:].strip())
                i += 1
            out.append(f'<blockquote style="{S_QUOTE}">{inline(" ".join(buf))}</blockquote>')
            continue

        if stripped.startswith("|"):
            flush_para()
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append(split_row(lines[i]))
                i += 1
            out.append(table(rows))
            continue

        if stripped == "---":
            i += 1
            continue

        img = image(stripped)
        if img:
            flush_para()
            out.append(img)
            i += 1
            continue

        if re.match(r"^[-*] ", stripped) or re.match(r"^\d+\. ", stripped):
            flush_para()
            ordered = bool(re.match(r"^\d+\. ", stripped))
            tag = "ol" if ordered else "ul"
            items = []
            while i < len(lines) and (re.match(r"^[-*] ", lines[i].strip()) or re.match(r"^\d+\. ", lines[i].strip())):
                items.append(re.sub(r"^([-*]|\d+\.)\s+", "", lines[i].strip()))
                i += 1
                # continuation lines of a wrapped bullet
                while i < len(lines) and lines[i].startswith("  ") and lines[i].strip():
                    items[-1] += " " + lines[i].strip()
                    i += 1
            out.append(f'<{tag} style="{S_UL}">' + "".join(
                f'<li style="{S_LI}">{inline(it)}</li>' for it in items) + f"</{tag}>")
            continue

        if stripped.startswith("**开篇**") or stripped.startswith("**接上篇**"):
            flush_para()
            out.append(f'<section style="{S_CALLOUT}">{inline(stripped)}</section>')
            i += 1
            continue

        para.append(stripped)
        i += 1

    flush_para()
    return "\n".join(out)


INLINE_CODE = re.compile(r"`[^`]*`")


def typeset_quotes(text: str) -> str:
    """ASCII double quotes -> Chinese typographic quotes, skipping code."""
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


def body_of(path: Path):
    raw = path.read_text(encoding="utf-8")
    zh = raw.split("\n---\n\n# English version\n")[0]
    lines = zh.splitlines()
    # drop the H1 series title and the "### NN｜..." subtitle: they go to the draft title
    idx = 0
    while idx < len(lines) and not lines[idx].strip():
        idx += 1
    if lines[idx].startswith("# "):
        idx += 1
    while idx < len(lines) and not lines[idx].strip():
        idx += 1
    if lines[idx].startswith("### "):
        idx += 1
    return typeset_quotes("\n".join(lines[idx:]).strip("\n"))


def page(title: str, inner: str, no: int, total: int) -> str:
    foot = (
        f'<section style="{S_FOOT}">'
        f"《{SERIES}》· 第 {no} 篇 / 共 {total} 篇<br/>"
        f"系列目录：https://bi4wmslabs.github.io/wordclock-series/"
        "</section>"
    )
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>{html.escape(title)}</title>
</head>
<body style="margin:0;padding:20px 16px;background:#fff;">
<section style="max-width:720px;margin:0 auto;">
{inner}
{foot}
</section>
</body>
</html>
"""


def main():
    OUT.mkdir(exist_ok=True)
    total = len(PARTS)
    for no, (src, slug, title, summary) in enumerate(PARTS, start=1):
        inner = render(body_of(BLOG / src))
        (OUT / f"{no:02d}-{slug}.html").write_text(page(title, inner, no, total), encoding="utf-8")
        print("wrote", f"{no:02d}-{slug}.html")

    index = ["# 公众号草稿 · 粘贴说明", "",
             f"系列：**{SERIES}**（共 {total} 篇，中文正文，内联样式）", "",
             "## 用法", "",
             "1. 用浏览器打开 `wechat/NN-*.html`（建议 Chrome/Edge）。",
             "2. 全选（Ctrl+A）→ 复制（Ctrl+C）。",
             "3. 打开公众号后台「草稿箱 → 新的创作 → 图文消息」，把标题填成下表标题，正文区粘贴（Ctrl+V）。",
             "4. 图片：文中图片指向线上博客地址，公众号编辑器一般会自动转存；若显示不出来，用编辑器的图片按钮重新上传 `images/` 里的同名文件。",
             "5. 代码块若在手机上换行，用截图替换对应段落。", "",
             "## 标题与摘要", "",
             "| # | 标题 | 摘要 |", "|---|---|---|"]
    for no, (_, _, title, summary) in enumerate(PARTS, start=1):
        index.append(f"| {no:02d} | {title} | {summary} |")
    index += ["",
              "## 建议", "",
              "- 系列连载建议按 00 → 06 顺序发；公众号一次只能群发一条，草稿可先全部建好再排期。",
              "- 开头三行定留存：每篇标题下的「开篇／接上篇」色块就是导语，别删。",
              "- 文末已带系列目录地址，公众号不能点外链，保留纯文本即可。"]
    (OUT / "README.md").write_text("\n".join(index) + "\n", encoding="utf-8")
    print("wrote README.md")


if __name__ == "__main__":
    main()
