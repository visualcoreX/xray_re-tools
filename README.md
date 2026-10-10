# xray_re-tools

X-Ray unofficial toolset for complex use with official S.T.A.L.K.E.R. MOD SDK. The code to load/save X-Ray files closely follows the GSC one. Regarding the rest source code, you can do whatever you want, just do not say you wrote it.

The toolset includes:

- **converter** — game asset converter (levels, `.ogf`, `.omf`, `.dds`, spawn, etc.), see `docs/tools/converter_en.txt`;
- **aiwrapper** — AI compiler wrapper, see `docs/tools/aiwrapper_en.txt`;
- **xrayMayaTools** — Maya plug-in: import of `.ogf`, `.omf`, `.dm`, `.object`, `.skl`, `.skls` and export of `.object`, `.skl`, `.anm`, see `docs/plugins/plugin_maya_en.txt`.

## What's new in this fork

- Maya 2025 support (SDK in `sources/3rd party/Maya_2025`), Visual Studio 2022 build.
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
2. Copy the MEL scripts (`xray_re_object_translator_options.mel`, `AEXRayMtlTemplate.mel`) to `Documents\maya\2025\scripts\`.
3. Copy `xray_path.ltx` to `%MAYA_LOCATION%\bin\` (e.g. `C:\Program Files\Autodesk\Maya2025\bin\`) and point `$sdk_root$` to your SDK (or game) folder; `$game_data$` must lead to the `gamedata` with textures.
4. Load the plug-in in *Windows → Settings/Preferences → Plug-in Manager*.

To import animations (`.omf`, `.skl`, `.skls`), select the character created by the `.ogf`/`.object` import first. Motions are applied by bone name, so the animation must belong to the same skeleton (e.g. hands animations need the hands model).

## Binaries

See the [Releases](https://github.com/visualcoreX/xray_re-tools/releases) section.

[Visual C++ Redistributable for Visual Studio 2015–2022 (x64)](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist) is required to run the programs.
