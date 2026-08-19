#!/usr/bin/env python3

import argparse
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "docs" / "images"

PANEL_WIDTH = 240
PANEL_HEIGHT = 135

FONT_HEIGHT_SMALL = 19
HEADER_HEIGHT = FONT_HEIGHT_SMALL + 1
FONT_PX = 16
TEXT_X = 4
BODY_ROWS = 6
SCALE = 3
BEZEL = 14

BODY_ROW_TOPS = [HEADER_HEIGHT + index * FONT_HEIGHT_SMALL for index in range(BODY_ROWS)]

BACKGROUND = "#000000"
BODY = "#101010"
TEXT = "#67ea94"
HEADER_BG = "#67ea94"
HEADER_TEXT = "#000000"

BATTERY_PERCENT = 78
CLOCK = "14:23"


RENDERER_SOURCES = ["airtime.cpp", "bridge_log.cpp", "lora_profile.cpp", "settings.cpp", "stats_text.cpp", "ui_nav.cpp",
                    "ui_text.cpp"]


def build_renderer(work: pathlib.Path) -> pathlib.Path:
    binary = work / "render_ui"
    sources = [str(ROOT / "tools" / "render_ui" / "main.cpp")] + [
        str(ROOT / "bridge" / "src" / name) for name in RENDERER_SOURCES
    ]
    command = ["g++", "-std=c++17", "-I", str(ROOT / "bridge" / "include"), *sources, "-o", str(binary)]
    subprocess.run(command, check=True)
    return binary


def collect_frames(binary: pathlib.Path) -> dict:
    output = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
    frames = {}
    name = None
    for raw in output.splitlines():
        if not raw.strip():
            name = None
            continue
        if "\t" not in raw:
            name = raw.strip()
            frames[name] = []
            continue
        marker, text = raw.split("\t", 1)
        frames[name].append((marker, text))
    return frames


def escape(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def px(value: float) -> float:
    return round(BEZEL + value * SCALE, 1)


def render_header(title: str) -> list:
    panel_w = PANEL_WIDTH * SCALE
    baseline = px(FONT_HEIGHT_SMALL - 5)
    font = FONT_PX * SCALE

    battery_x = px(TEXT_X)
    battery_y = px(4)
    battery_w = 18 * SCALE
    battery_h = 13 * SCALE

    parts = [
        f'<rect x="{BEZEL}" y="{BEZEL}" width="{panel_w}" height="{px(HEADER_HEIGHT) - BEZEL}" rx="{2 * SCALE}" '
        f'fill="{HEADER_BG}"/>',
        f'<g font-family="Helvetica, Arial, DejaVu Sans, sans-serif" font-size="{font}" fill="{HEADER_TEXT}">',
        f'<rect x="{battery_x}" y="{battery_y}" width="{battery_w}" height="{battery_h}" rx="{1.5 * SCALE}" '
        f'fill="none" stroke="{HEADER_TEXT}" stroke-width="{SCALE}"/>',
        f'<rect x="{battery_x + 1.5 * SCALE}" y="{battery_y + 1.5 * SCALE}" '
        f'width="{(battery_w - 3 * SCALE) * BATTERY_PERCENT / 100:.1f}" height="{battery_h - 3 * SCALE}" fill="{HEADER_TEXT}"/>',
        f'<rect x="{battery_x + battery_w}" y="{battery_y + 4 * SCALE}" width="{1.5 * SCALE}" '
        f'height="{5 * SCALE}" fill="{HEADER_TEXT}"/>',
        f'<text x="{battery_x + battery_w + 7 * SCALE}" y="{baseline}">{BATTERY_PERCENT}%</text>',
        f'<text x="{BEZEL + panel_w / 2}" y="{baseline}" text-anchor="middle" font-weight="bold">{escape(title)}</text>',
        f'<text x="{px(PANEL_WIDTH - TEXT_X)}" y="{baseline}" text-anchor="end">{CLOCK}</text>',
        "</g>",
    ]
    return parts


def render(name: str, lines: list, title: str) -> str:
    width = PANEL_WIDTH * SCALE + BEZEL * 2
    height = PANEL_HEIGHT * SCALE + BEZEL * 2

    header = lines[0][1] if lines else title
    body = [(marker == ">", text) for marker, text in lines[1:]]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-label="{escape(title)}">',
        f'<rect width="{width}" height="{height}" rx="10" fill="{BODY}"/>',
        f'<rect x="{BEZEL}" y="{BEZEL}" width="{PANEL_WIDTH * SCALE}" height="{PANEL_HEIGHT * SCALE}" '
        f'rx="3" fill="{BACKGROUND}"/>',
    ]

    parts += render_header(header)

    parts.append(f'<g font-family="Helvetica, Arial, DejaVu Sans, sans-serif" font-size="{FONT_PX * SCALE}">')

    for row, (selected, text) in enumerate(body[:BODY_ROWS]):
        top = BODY_ROW_TOPS[row]
        baseline = px(top + FONT_HEIGHT_SMALL - 5)
        if selected:
            parts.append(f'<rect x="{BEZEL}" y="{px(top + 2)}" width="{PANEL_WIDTH * SCALE}" '
                         f'height="{(FONT_HEIGHT_SMALL - 5) * SCALE}" fill="{TEXT}"/>')
        colour = BACKGROUND if selected else TEXT
        parts.append(f'<text x="{px(TEXT_X + 2)}" y="{baseline}" fill="{colour}">{escape(text)}</text>')

    parts.append("</g>")
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Render the device UI frames to SVG from the firmware's own strings")
    parser.add_argument("--check", action="store_true", help="fail if the committed SVGs are stale")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        binary = build_renderer(pathlib.Path(tmp))
        frames = collect_frames(binary)

    titles = {
        "status": "MeshCompromise status frame, aligned mode",
        "status-split": "MeshCompromise status frame, split mode with the alignment hint",
        "settings": "MeshCompromise settings frame",
        "settings-scrolled": "MeshCompromise settings frame, scrolled while editing",
    }

    stale = []
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    for name, lines in frames.items():
        svg = render(name, lines, titles.get(name, name))
        target = OUT_DIR / f"ui-{name}.svg"

        if args.check:
            if not target.exists() or target.read_text() != svg:
                stale.append(target)
            continue

        target.write_text(svg)
        print(f"wrote {target.relative_to(ROOT)} ({len(lines)} lines)")

    if args.check:
        if stale:
            names = ", ".join(str(p.relative_to(ROOT)) for p in stale)
            sys.exit(f"stale UI images: {names}\nrun: python3 scripts/render_ui_svg.py")
        print("UI images match the firmware strings")


if __name__ == "__main__":
    main()
