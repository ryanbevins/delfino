# Delfino

An Unreal Engine 5 plugin for importing Super Mario Sunshine assets directly into the editor.

Delfino reads the original GameCube binary formats (BMD, BCK, BTI, etc.) and converts them into native UE5 assets — skeletal meshes with full bone hierarchies, skeletal animations, materials with decoded GX textures, and collision geometry.

## Features

- **BMD/BDL Model Import** — Full J3D binary model parsing including VTX1, SHP1, JNT1, EVP1, DRW1, MAT3, TEX1 sections. Supports both static and skeletal mesh generation.
- **Skeletal Mesh Pipeline** — Correct bone hierarchy from INF1 scene graph, weighted/rigid vertex skinning via EVP1 envelopes, and GC-to-UE coordinate conversion with proper matrix conventions.
- **BCK Animation Import** — Joint transform animations with keyframe interpolation, rotation fraction scaling, and ZYX Euler-to-quaternion conversion.
- **BTK/BTP/BRK Animations** — Texture scroll, texture pattern swap, and register color animations.
- **BTI/GX Texture Decoding** — Supports CMPR (S3TC/DXT1), I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA32, C4, C8, C14X2 formats with proper tile decoding.
- **COL Collision Import** — Surface type mapping to UE physical materials.
- **Scene Graph Import** — JDrama scene.bin parser that generates Blueprint actors positioned according to the original level layout.
- **SZS/RARC Archive Support** — Yaz0 decompression and RARC filesystem traversal.
- **Editor UI** — Slate-based importer window with ISO browse, level selection, and import controls.

## Requirements

- Unreal Engine 5.5+
- A legally obtained Super Mario Sunshine ISO

## Installation

1. Clone this repository
2. Copy the `Plugins/SMSLevelImporter` folder into your UE5 project's `Plugins/` directory
3. Regenerate project files and build

Alternatively, create a junction/symlink from your project's Plugins folder to avoid copying:
```powershell
New-Item -ItemType Junction -Path "YourProject/Plugins/SMSLevelImporter" -Target "path/to/Delfino/Plugins/SMSLevelImporter"
```

## Usage

1. Open your UE5 project
2. Go to **Window > SMS Level Importer**
3. Browse to your SMS ISO file
4. Select a level from the dropdown
5. Click **Import**

The plugin will extract and convert all assets from the selected level, creating:
- `/Content/SMS/Characters/` — Skeletal meshes, skeletons, and animations
- `/Content/SMS/Levels/` — Static meshes, materials, textures, and collision
- `/Content/SMS/Levels/<LevelName>/` — Blueprint actors for scene layout

## Technical Notes

### Coordinate System Conversion
GameCube uses a right-handed Y-up coordinate system. UE5 uses left-handed Z-up. The plugin converts via:
- Vertex positions: `UE = (GC.X, GC.Z, GC.Y)`
- Bone transforms: Matrix sandwich `M_ue = P * M_gc * P` where P swaps Y/Z
- Triangle winding: Reversed for handedness change

### Skinning Pipeline
- **Rigid vertices** (DRW1 type 0): Stored in bone-local space, transformed to model space via the joint's world matrix
- **Weighted vertices** (DRW1 type 1): Stored in model space, using EVP1 envelope weights with inverse bind matrices
- **Matrix tables**: Per-packet GX matrix slot tables with 0xFFFF carry-forward resolution across packets

### J3D Format References
The implementation references format documentation from:
- [noclip.website](https://github.com/magcius/noclip.website) — J3D rendering and format parsing
- [SuperBMD](https://github.com/Sage-of-Mirrors/SuperBMD) — BMD import/export library
- [blemd](https://github.com/niacdoial/blemd) — Blender BMD importer
- [WindEditor Wiki](https://github.com/LordNed/WindEditor/wiki/BMD-and-BDL-Model-Format) — Format documentation

## License

MIT

## Disclaimer

This project is not affiliated with or endorsed by Nintendo. Super Mario Sunshine is a trademark of Nintendo. You must own a legal copy of the game to use this tool.
