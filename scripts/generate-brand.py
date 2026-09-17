"""Regenerate the font-independent SVG identity assets using only Python's stdlib."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "assets" / "brand"

# Angled workshop hammer, editor viewport corners, and a raised Source 2 cue.
MARK = '''<g class="accent-stroke" fill="none" stroke-width="2.5" stroke-linecap="square">
<path d="M6 19V6h13M6 45v13h13M45 58h13V45"/>
</g>
<g transform="rotate(-38 31 32)">
<path class="ink" d="M17 14h23l5 5v8H17Z"/>
<path class="ink" d="M27 29h9v23a2 2 0 0 1-2 2h-5a2 2 0 0 1-2-2Z"/>
</g>
<path class="accent" d="M44 9q2-3 6-3 7 0 7 6 0 3-4 6l-4 3h8v3H44v-3l7-6q3-2 3-3 0-3-4-3-3 0-4 3Z"/>'''
# Original geometric lettering: no font files, text rendering, or external references.
LETTERS = {
    "H": "M0 0V28M18 0V28M0 14H18",
    "A": "M0 28 9 0 18 28M4 18H14",
    "M": "M0 28V0L10 16 20 0V28",
    "E": "M18 0H0V28H18M0 14H15",
    "R": "M0 28V0H11Q18 0 18 7T11 14H0M10 14 20 28",
    "D": "M0 0V28H8Q20 28 20 14T8 0Z",
    "O": "M10 0Q0 0 0 10V18Q0 28 10 28T20 18V10Q20 0 10 0Z",
    "N": "M0 28V0L20 28V0",
    "S": "M19 3Q16 0 10 0 0 0 0 7T10 14Q20 14 20 21T10 28Q4 28 0 25",
}
PALETTES = {
    "dark": ("#d2d8e2", "#91b2df"),
    "light": ("#293342", "#315c8c"),
    "mono-black": ("#000000", "#000000"),
    "mono-white": ("#ffffff", "#ffffff"),
}


def lettering(word, x, y, scale, color):
    paths = "".join(f'<path transform="translate({i * 29} 0)" d="{LETTERS[letter]}"/>'
                    for i, letter in enumerate(word))
    return (f'<g class="{color}-stroke" transform="translate({x} {y}) scale({scale})" '
            f'fill="none" stroke-width="4.5" stroke-linecap="square" stroke-linejoin="round">{paths}</g>')


def artwork(layout):
    if layout == "icon":
        return 64, 64, MARK
    if layout == "logo":
        return 288, 96, ('<g transform="translate(0 0) scale(1.5)">' + MARK + '</g>'
                         + lettering("HAMMER", 110, 18, 1, "ink")
                         + lettering("ADDONS", 110, 61, 0.64, "accent"))
    return 208, 184, ('<g transform="translate(56 0) scale(1.5)">' + MARK + '</g>'
                      + lettering("HAMMER", 21, 111, 1, "ink")
                      + lettering("ADDONS", 51, 155, 0.64, "accent"))


def svg(layout, palette):
    width, height, content = artwork(layout)
    ink, accent = PALETTES[palette]
    style = (f'.ink{{fill:{ink}}}.accent{{fill:{accent}}}'
             f'.ink-stroke{{stroke:{ink}}}.accent-stroke{{stroke:{accent}}}')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" role="img" aria-labelledby="title">\n'
            f'<title id="title">Hammer Addons</title>\n<style>{style}</style>\n{content}\n</svg>\n')


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for layout in ("icon", "logo", "stacked"):
        for palette in PALETTES:
            (OUTPUT / f"{layout}-{palette}.svg").write_text(svg(layout, palette), encoding="utf-8")
    # A fixed dark tile stays legible in both light and dark browser tab bars.
    favicon = svg("icon", "dark").replace(
        MARK, '<rect width="64" height="64" rx="12" fill="#181c23"/>\n' + MARK)
    (OUTPUT / "favicon.svg").write_text(favicon, encoding="utf-8")
    print(f"Generated 13 SVG assets in {OUTPUT}")


if __name__ == "__main__":
    main()
