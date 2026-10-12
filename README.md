# xray_re-tools

X-Ray unofficial toolset for complex use with official S.T.A.L.K.E.R. MOD SDK. The code to load/save X-Ray files closely follows the GSC one. Regarding the rest source code, you can do whatever you want, just do not say you wrote it.

- **converter** — game asset converter, see `docs/tools/converter_en.txt`;
- **aiwrapper** — AI compiler wrapper, see `docs/tools/aiwrapper_en.txt`;
- **xrayMayaTools** — Maya plug-in for `.ogf`, `.omf`, `.object`, `.skl`, `.skls`, `.anm`, `.dm`, see `docs/plugins/plugin_maya_en.txt`.

## What's new in this fork

- Maya 2025 and Visual Studio 2022.
- Motions are imported as Time Editor animation sources, a namespace per skeleton.
- `.omf` export like the SDK: key precision (SDK / 16 bit / 32-bit float), motion parameters, bone parts, motion marks.
- Motion references, `.anm` camera import.
- Files the original tools crashed on: Gunslinger-protected `.omf`, nameless bones, broken chunk flags; a broken file no longer takes Maya down.

## Build

Open `bld-vs2022\EngineToolset_vs2022.sln` in Visual Studio 2022 and build **Release / x64**. Projects that still declare toolset v142 can be retargeted, or built with:

```
msbuild bld-vs2022\EngineToolset_vs2022.sln -t:plugins\maya_tools -p:Configuration=Release -p:Platform=x64 -p:PlatformToolset=v143
```

## Maya plug-in

1. Copy `xrayMayaTools.mll` to `Documents\maya\2025\plug-ins\` and the `.mel` scripts to `Documents\maya\2025\scripts\`.
2. Copy `xray_path.ltx` to `Maya2025\bin\` and point `$sdk_root$` to your SDK/game folder.
3. Enable the plug-in in the Plug-in Manager.

Select a joint of the skeleton before importing motions. For `.omf` export select the skinned mesh: the export options hold the motion list and their parameters.

## Binaries

See [Releases](https://github.com/visualcoreX/xray_re-tools/releases). Requires the [Visual C++ Redistributable 2015–2022 (x64)](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist).
