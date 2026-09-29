Player character (Assets/Model/Quaternius_Ranger)
=================================================

Ranger.fbx is assembled by Tools/BuildPlayerModel.py (Blender, background) from
four free packs by Quaternius (https://quaternius.com), all CC0 1.0 (public domain):

  Universal Base Characters [Standard]           https://quaternius.itch.io/universal-base-characters
      Superhero_Male_FullBody (only head + neck kept), eyes, eyebrows,
      Hair_Beard (rigged to the head bone; beard and eyebrows lightened to white)
  Modular Character Outfits - Fantasy [Standard]  https://quaternius.itch.io/modular-character-outfits-fantasy
      Male_Ranger outfit
  Universal Animation Library [Standard]          https://quaternius.itch.io/universal-animation-library
  Universal Animation Library 2 [Standard]        https://quaternius.itch.io/universal-animation-library-2
      24 clips (list in the script): Idle_Loop, Walk_Loop, Jog_Fwd_Loop, Sprint_Loop,
      Jump_Start/Loop/Land, Roll, Crouch_*, Spell_Simple_*, Hit_*, Death01,
      Slide_Start/Loop/Exit, NinjaJump_*

One 65-bone skeleton (UE mannequin names: root, pelvis, spine_01..03, neck_01, Head, ...),
metres, faces +Z (PlayerFactory modelYawOffsetDeg = 0; the KayKit models face -Z and use 180).
Textures are shrunk to 1024x1024 (the packs ship 2048/4096) and embedded as PNG.

License text shipped with the packs:
-------------------------------------------------------
License:
CC0 1.0 Universal (CC0 1.0) 
Public Domain Dedication
https://creativecommons.org/publicdomain/zero/1.0/

------------------------------------------------------
Models by @Quaternius
Consider supporting me on Patreon!

https://www.patreon.com/quaternius
