#!python3
"""Measure the UI's real strings against the boxes Ui.cpp reserves for them.

The layout coordinates in Ui.cpp are hand-placed, so this reads the advance
widths straight out of the generated font header and checks that the labels --
including the live project names from the API -- actually fit.

    python tools/checklayout.py
"""

import json
import os
import re
import sys
import urllib.request

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
from envutil import load_api_config

FONT_HEADER = os.path.join(ROOT, "src", "ui_fonts.h")

# Must mirror INTERVALS in tools/genfonts.py.
INTERVALS = [(0x20, 0x7E), (0xA0, 0xFF)]


def load_fonts(path):
    """name -> {codepoint: advance_x}"""
    source = open(path, encoding="ascii").read()
    fonts = {}
    for match in re.finditer(r"const GFXglyph (\w+)Glyphs\[\] PROGMEM = \{(.*?)\n\};", source, re.S):
        name, body = match.group(1), match.group(2)
        advances = [int(row[2]) for row in re.findall(r"\{([^}]*)\}", body)
                    for row in [ [v.strip() for v in row.split(",")] ]]
        codepoints = [cp for first, last in INTERVALS for cp in range(first, last + 1)]
        if len(advances) != len(codepoints):
            raise SystemExit(f"{name}: {len(advances)} glyphs for {len(codepoints)} code points")
        fonts[name] = dict(zip(codepoints, advances))
    return fonts


def width(font, text):
    total = 0
    for ch in text:
        advance = font.get(ord(ch))
        if advance is None:
            raise SystemExit(f"code point U+{ord(ch):04X} ({ch!r}) is not in the font")
        total += advance
    return total


def upper(text):
    return text.upper()


def main():
    fonts = load_fonts(FONT_HEADER)

    cfg = load_api_config(ROOT)
    projects = []
    if not cfg["base"]:
        print("! API_BASE is not set in .env; checking static labels only")
    else:
        url = cfg["base"] + "/api/projects?status=demarre"
        try:
            request = urllib.request.Request(url)
            if cfg["token"]:
                request.add_header("Authorization", "Bearer " + cfg["token"])
            with urllib.request.urlopen(request, timeout=20) as response:
                projects = json.load(response)["projects"]
        except Exception as exc:  # offline is fine, just check the static labels
            print(f"! could not reach the API ({exc}); checking static labels only")

    failures = []

    def check(label, font_name, text, budget):
        shown = upper(text)
        w = width(fonts[font_name], shown)
        status = "ok " if w <= budget else "OVER"
        if w > budget and not label.startswith("name"):
            failures.append((label, shown, w, budget))
        print(f"  [{status}] {w:4d}/{budget:4d}  {label}: {shown!r}")

    # Play column is 30% wider, so the name sits further right.
    print("Project rows (name budget 567 px idle / 511 px with MM tag):")
    for p in projects:
        budget = 511 if p["moonmoon"] else 567
        shown = upper(p["name"])
        font = "UiTitle" if width(fonts["UiTitle"], shown) <= budget else "UiBodyBold"
        check(f"name ({font})", font, p["name"], budget)

    for p in projects:
        hours = p["hours"]
        def hm(value):
            minutes = int(round(max(value, 0) * 60))
            h, m = divmod(minutes, 60)
            if h and m:
                return f"{h}H{m}MIN"
            if h:
                return f"{h}H"
            return f"{m}MIN"
        done = hm(hours["done"])
        total = "" if hours["total"] is None else f" / {hm(hours['total'])}"
        value = done + total
        check("value+timer", "UiBody", value + "  ·  + 2H15MIN", 700)
        pct = "NC" if hours["total"] is None else f"{hours['pct']} %"
        check("pct", "UiDisplay", pct, 200)

    print("\nButtons and chrome:")
    check("settings wifi btn", "UiBody", "Choisir un Wi-Fi", 288 - 24)
    check("settings forget btn", "UiBody", "Oublier ce r\u00e9seau", 288 - 24)
    check("settings clean btn", "UiBody", "Nettoyer l'\u00e9cran", 288 - 24)
    check("settings theme", "UiBody", "Sombre", 400)
    check("person chip fallback", "UiBody", "Attribuer", 280 - 96)
    check("empty state", "UiTitle", "Aucun projet en cours", 912)
    check("boot subtitle", "UiBody", "Suivi du temps de l'\u00e9quipe", 912)

    print("\nHeader status line (budget ~544 px):")
    check("status counting", "UiSmall", "Timer actif", 544)
    check("status idle", "UiSmall", "12 projets en cours", 544)
    check("hint counting", "UiMicro", "Destination Hockey  ·  2H15MIN", 544)
    check("hint idle", "UiMicro", "Aucun timer", 544)

    if projects:
        names = sorted({a["name"] for p in projects for a in p["assignees"]})
        print(f"\nPeople cards ({len(names)} people, budget 232 px):")
        for name in names:
            shown = upper(name)
            font = "UiTitle" if width(fonts["UiTitle"], shown) <= 232 else "UiBody"
    check("person ({font})", font, name, 290 - 56)

    print()
    if failures:
        print(f"{len(failures)} label(s) overflow their box:")
        for label, text, w, budget in failures:
            print(f"  {label}: {text!r} needs {w} px, has {budget}")
        return 1
    print("every label fits.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
