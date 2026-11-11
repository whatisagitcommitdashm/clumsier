"""Render a light installer background that keeps Finder's black labels readable.

Build-time only: requires Pillow. Does not modify the application or user settings.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUTPUT = Path(__file__).resolve().parents[1] / 'packaging/macos/dmg/background.png'
BACKGROUND = '#eef2e9'
TEXT = '#1b2920'
MUTED = '#435342'
ACCENT = '#39582b'


def render():
    image = Image.new('RGB', (720, 440), BACKGROUND)
    draw = ImageDraw.Draw(image)
    font_path = '/System/Library/Fonts/Supplemental/Arial.ttf'

    def centered(y, text, size, color):
        font = ImageFont.truetype(font_path, size)
        bounds = draw.textbbox((0, 0), text, font=font)
        draw.text(((720 - bounds[2]) / 2, y), text, font=font, fill=color)

    centered(34, 'Clumsier for Mac', 28, TEXT)
    centered(80, 'Drag Clumsier into Applications', 17, ACCENT)
    draw.line((305, 210, 405, 210), fill=ACCENT, width=3)
    draw.line((392, 198, 405, 210, 392, 222), fill=ACCENT, width=3)
    centered(330, 'Then open Clumsier from Applications.', 15, MUTED)
    centered(360, 'Need a hand?  clumsier.vercel.app', 13, MUTED)
    image.save(OUTPUT)


if __name__ == '__main__':
    render()
