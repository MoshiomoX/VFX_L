Particle sheets (Assets/Particles/Sheets)
=========================================

One atlas PNG + one manifest JSON per sheet. The sheet number an effect
stores ("sheet" in VFXData) is the order in Res::ParticleSheet::kManifests,
not the file name, so never reorder that table.

  0  Legacy.json           ../particlesSheet.jpg (the original 6x6 sheet, straight alpha)
  1  KenneyParticles.json  KenneyParticles.png  10x8 cells of 256px
  2  KenneySmoke.json      KenneySmoke.png      9x9 cells of 256px

Every new atlas stores PREMULTIPLIED alpha in a plain PNG ("premultiplied":
true in the manifest). Image viewers therefore show darker edges than the
game does. Rebuild with Tools/BuildParticleSheets.ps1 from the unzipped packs.

Manifest: name, texture (relative to the manifest), rows, cols,
filter ("linear" / "point"), premultiplied, frames (one name per cell,
may be empty), groups (named runs of cells: name / start / count).

Sources (all CC0 1.0, public domain, no attribution required)
------------------------------------------------------------
Kenney Particle Pack 1.1 by Kenney (kenney.nl)
  https://kenney.nl/assets/particle-pack
  "PNG (Black background)" set, 512px scaled to 256px, luminance -> alpha,
  white rgb so the particle colour tints it. Rotated/ variants left out.

Kenney Smoke Particles by Kenney (kenney.nl)
  https://kenney.nl/assets/smoke-particles
  All 77 puffs (black smoke, explosion, fart, flash, white puff), fitted into
  256px cells, colours kept.

PVFX Foundry (pixel sprite sheets) is NOT packed here: it is played by the
Sprite entry from Assets/VFX/SpriteSheet/PVFX.
