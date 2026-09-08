# Source textures — build input, not product

These are the master images `SetupPBRTextures.py` imports to produce the three shipped
textures in `Content/InstantOrganicCaves/Textures/`:

| File here | Imported as |
|---|---|
| `tex_rock_obsidian.png` | `T_RockObsidian` |
| `tex_rock_limestone.png` | `T_RockLimestone` |
| `tex_rock_alien.png` | `T_RockAlien` |

The script derives the asset name from the file name, so **do not rename these** without
changing the script — `tex_rock_alien.png` is what produces `T_RockAlien`.

## Why they are PNG and not the original JPGs

The original JPGs were lost. These were recovered losslessly on 2026-09-07 by exporting
`UTexture::Source` back out of the shipped `.uasset` files with `TextureExporterPNG` —
editor builds keep the full source pixels inside the package, so nothing was resampled or
re-compressed. They are the authored pixels, not a re-encode of a JPEG artefact.

## They do not ship

`Config/FilterPlugin.ini` excludes this folder. All three are 1024x1024 RGBA and total about
6.2 MB, which is roughly ten times the entire rest of `Resources/` — there is no reason for a
customer to download the masters when the imported textures are already in `Content/`.

Keep them here, in version control, so the textures can be rebuilt. That is the whole point:
before this, regenerating them was impossible.
