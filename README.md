# xray_re-tools

X-Ray unofficial toolset for complex use with official S.T.A.L.K.E.R. MOD SDK. The code to load/save X-Ray files closely follows the GSC one. Regarding the rest source code, you can do whatever you want, just do not say you wrote it.

The toolset includes:

- **converter** — game asset converter (levels, `.ogf`, `.omf`, `.dds`, spawn, etc.), see `docs/tools/converter_en.txt`;
- **aiwrapper** — AI compiler wrapper, see `docs/tools/aiwrapper_en.txt`;
- **xrayMayaTools** — Maya plug-in: import of `.ogf`, `.omf`, `.dm`, `.object`, `.skl`, `.skls`, `.anm` and export of `.object`, `.skl`, `.omf`, `.anm`, see `docs/plugins/plugin_maya_en.txt`.

## What's new in this fork

- Maya 2025 support (SDK in `sources/3rd party/Maya_2025`), Visual Studio 2022 build.
- Motions are imported as Time Editor animation sources instead of Trax clips.
- `.anm` import: the motion goes onto a `<name>_locator`, and a `<name>_camera` follows it through a parent constraint.
- `.omf` export, done the same way as the SDK.
- Import of models and animations that the original tools refused or crashed on:
  - `.ogf` with the compression bit set on raw chunks and garbage in the bone IK data version;
  - `.omf` protected in the Gunslinger style (random motion ids, scrambled motion names) — motions are matched by order, like the release engine does.
- A broken file no longer takes Maya down: the import fails with an error in the Script Editor instead.
- When imported motions don't match the selected character, missing bones are listed once; if no bones match at all, nothing is imported.
- Correct clip duration on motion import.
- Decompiling build 1472 models without breaking geometry (bind pose is restored from the first motion).
- Texture name extraction from `standardSurface` materials (Maya 2020+); `default` shader/material are remapped to `models\model`/`default_object` on skeletal object export.

## Build

Requirements: Visual Studio 2022 with the "Desktop development with C++" workload (toolset v143, Windows 10 SDK).

1. Open `bld-vs2022\EngineToolset_vs2022.sln`.
2. Select **Release** / **x64** (Maya 2025 is 64-bit only).
3. Build the solution or individual projects (`maya_tools`, `converter`, `aiwrapper`).

Binaries go to `binaries\<platform>-<configuration>\`.

Most projects in the solution still declare toolset v142. If it is not installed, either retarget them in Visual Studio (*Project → Retarget solution*) or override the toolset from the command line:

```
msbuild bld-vs2022\EngineToolset_vs2022.sln -t:plugins\maya_tools;utils\converter;utils\aiwrapper -p:Configuration=Release -p:Platform=x64 -p:PlatformToolset=v143 -m
```

The old `bld-vs2015\EngineToolset_vs2015.sln` builds the Maya plug-in against the Maya 2019.2 SDK (`sources/3rd party/Maya_2019_2`).

## Maya plug-in installation (Maya 2025)

1. Copy `xrayMayaTools.mll` to `Documents\maya\2025\plug-ins\` (or `%MAYA_LOCATION%\bin\plug-ins\`).
2. Copy the MEL scripts (`xray_re_object_translator_options.mel`, `xray_re_omf_translator_options.mel`, `xray_re_motion_refs.mel`, `AEXRayMtlTemplate.mel`) to `Documents\maya\2025\scripts\`.
3. Copy `xray_path.ltx` to `%MAYA_LOCATION%\bin\` (e.g. `C:\Program Files\Autodesk\Maya2025\bin\`) and point `$sdk_root$` to your SDK (or game) folder; `$game_data$` must lead to the `gamedata` with textures.
4. Load the plug-in in *Windows → Settings/Preferences → Plug-in Manager*.

Animations (`.omf`, `.skl`, `.skls`, and motions embedded in `.ogf`) are imported as **Time Editor animation sources**, one per motion and named after it; nothing is put on the timeline, create clips from the sources you need. Select any joint of the skeleton before importing (with nothing selected, all joints in the scene are used). Motions are applied by bone name, so the animation must belong to the same skeleton (e.g. hands animations need the hands model).

The sources of each skeleton go to a namespace of their own — `character:idle` for a `bip01` root, `wpn:idle` for `wpn_body` (`wpn1` for a second weapon whose root Maya renamed to `wpn_body1`), `<root>_motions` for any other root — so the hands and the weapon can both have an `idle` without Maya renaming one of them to `idle1`. A source remembers its skeleton (`xraySkeleton`, connected from the root joint) and its motion name (`xrayMotionName`): the `.omf` export of a skeleton takes only its own sources and writes them under their own names.

`.omf` export (*File → Export Selection*, type "X-Ray game skeletal motions", with the skinned mesh selected) follows the SDK (`CExportSkeleton::ExportMotionKeys/ExportMotionDefs`): every Time Editor animation source of the mesh's skeleton becomes a motion (sources made by hand, with no `xraySkeleton`, count when they animate its bones), keyed for all bones of the skin at 30 fps. Motion parameters are edited right in the export options: pick one or more motions in the list and set Speed, Accrue, Falloff, Type FX, the bone part and the Stop at end / No mix / Sync part / UseFootSteps / Move XForm / Idle / UseWeaponBone flags for cycles, or the start bone and Power for FX, as in ActorEditor. They are stored as `xray*` attributes of the animation sources (changes go to the scene at once, even if the export is cancelled), and the `.omf` import fills them, together with the motion order, the marks and the bone parts (kept on the skeleton root); without them the SDK defaults and a single `default` part are used. Alternatively, *Export: Scene animation* plays the scene over the playback range (both ends included; keys, Time Editor clips, IK, constraints — whatever moves the joints) and writes it as one motion, named in the options (the file name by default), with its parameters kept on the skeleton root. The scene animation is also used when there are no animation sources.

Motion marks are keyable booleans `xrayMark_<name>` on the skeleton root: a stepped key of 1 starts an interval, 0 ends it. The `.omf` import puts their curves into the animation sources; the export turns the keys back into intervals (exactly for anim sources, per frame for the scene animation). The *Marks* block of the options adds/removes the SDK's Left/Right and Left2/Right2 channels and opens the Graph Editor on their curves.

The same motion panel is available outside of the export as `xray_re_motions_window()` (`xray_re_omf_translator_options.mel`).

Motion references of a skeletal object (`.omf` names relative to `$game_meshes$`, like ActorEditor's *Motion reference*) are kept on the skeleton root (`xrayMotionRefs`), filled by the `.ogf`/`.object` import and written by the skeletal `.object` export. They are edited in `xray_re_motion_refs_window()` (`xray_re_motion_refs.mel`, also the *Motion references...* button of the object options): add from `.omf` files, remove, reorder, export/import an `.ltx` list as in the SDK.

The *Keys* switch of the export options sets the precision of the whole file: *SDK (8/16 bit)* — 16 bit rotations, 8 bit translations, 16 bit for bones moving more than 1.5 m; *16 bit* — the SDK's "Force 16bit MotionT", 16 bit translations for every bone; *32-bit float* — plain 32 bit float keys (key flag `0x08`: a float quaternion and a float vector per key, CRCs as usual, no quantization), which the stock engines can't read. Such 32-bit OMFs are imported too (the import reports how many motions had them), and the `.omf` import accepts models with more than the stock 64 bones.

The `.ogf`/`.object` import no longer creates a character set, since the Time Editor can't drive attributes that belong to one. Selecting a character instead of a joint still imports the motions as Trax clips, as before.

## Binaries

See the [Releases](https://github.com/visualcoreX/xray_re-tools/releases) section.

[Visual C++ Redistributable for Visual Studio 2015–2022 (x64)](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist) is required to run the programs.
