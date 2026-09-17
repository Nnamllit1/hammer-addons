# Logo and brand assets

The Hammer Addons mark combines an angled workshop hammer, editor viewport
corners, and a raised **2** that connects it to Source 2 tools. The lettering and mark are original
vector paths. SVG files have transparent backgrounds and need no installed fonts,
external images, or scripts.

<div class="ha-brand-grid">
  <div class="ha-brand-sample ha-brand-sample--dark">
    <img src="../assets/brand/logo-dark.svg" width="288" height="96" alt="Horizontal logo on charcoal">
  </div>
  <div class="ha-brand-sample ha-brand-sample--light">
    <img src="../assets/brand/logo-light.svg" width="288" height="96" alt="Horizontal logo on light grey">
  </div>
  <div class="ha-brand-sample ha-brand-sample--dark">
    <img src="../assets/brand/stacked-dark.svg" width="208" height="184" alt="Stacked logo on charcoal">
  </div>
  <div class="ha-brand-sample ha-brand-sample--light">
    <img src="../assets/brand/stacked-light.svg" width="208" height="184" alt="Stacked logo on light grey">
  </div>
</div>

## Downloads

Variant names describe the **background**: use `dark` on dark surfaces and
`light` on light surfaces. All versions share the same geometry.

| Configuration | Dark background | Light background | Monochrome |
| --- | --- | --- | --- |
| Compact mark | [icon-dark.svg](assets/brand/icon-dark.svg) | [icon-light.svg](assets/brand/icon-light.svg) | [Black](assets/brand/icon-mono-black.svg) / [White](assets/brand/icon-mono-white.svg) |
| Horizontal logo | [logo-dark.svg](assets/brand/logo-dark.svg) | [logo-light.svg](assets/brand/logo-light.svg) | [Black](assets/brand/logo-mono-black.svg) / [White](assets/brand/logo-mono-white.svg) |
| Stacked logo | [stacked-dark.svg](assets/brand/stacked-dark.svg) | [stacked-light.svg](assets/brand/stacked-light.svg) | [Black](assets/brand/stacked-mono-black.svg) / [White](assets/brand/stacked-mono-white.svg) |

The [favicon](assets/brand/favicon.svg) includes a charcoal rounded-square
background so it remains legible in either browser theme. For desktop software,
export the mark to the application's required raster or icon format; renaming an
SVG to `.ico` does not convert it.

## Usage

- Use the compact mark for toolbars, small buttons, and avatars. Its minimum
  intended size is 16 × 16 px; use 24 px or larger where space allows.
- Use the horizontal logo at 216 px wide or larger, and the stacked logo at
  156 px wide or larger. Switch to the mark if the lettering becomes too small.
- Preserve proportions and the built-in clear space. Keep the raised 2 and
  viewport corners with the hammer.
- Choose monochrome for single-colour printing or surfaces where the blue accent
  would not have enough contrast.

| Colour | Dark surfaces | Light surfaces |
| --- | --- | --- |
| Main mark and lettering | `#D2D8E2` | `#293342` |
| Viewport corners, raised 2, and secondary lettering | `#91B2DF` | `#315C8C` |
| Suggested background | `#181C23` | `#E8EBEF` |

The documentation header remains charcoal in both themes and therefore always
uses `icon-dark.svg`. Logos in page content follow the selected site theme.
Embedded SVG images do not inherit a page's CSS colours: select the matching
asset explicitly when integrating the logo elsewhere.

## Maintain the assets

The source geometry and colour definitions are in `scripts/generate-brand.py`.
Regenerate all 13 SVG files with Python 3.11+ from the repository root:

```powershell
python scripts/generate-brand.py
```

Keep generated SVGs committed so consumers can use the assets without a build
step. Edit the generator rather than individual copies to preserve consistency.
