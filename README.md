# Aether UI

**A customizable native desktop experience for Windows.** Ubuntu-inspired navigation, a fullscreen window overview, multi-monitor panels, and a personal desktop studio—built with C++20, Win32, Direct2D and DirectWrite.

> Active development / v0.9 preview. Source is available; this is not a finished production release. See [validation and known limitations](docs/TESTING.md).

![Aether Desktop Studio with the native icon-pack browser](docs/images/studio.webp)

## Make the desktop your own

- **Activities:** fullscreen window overview, large previews, monitor filters, app grouping and close controls.
- **Workspaces:** drag-and-drop window organization, renaming, reordering and per-workspace wallpapers on supported Windows versions.
- **Terminal and Files:** real ConPTY PowerShell/CMD/WSL tabs, ten stock terminal designs, local background images and a native local file browser. [Designs and controls](docs/TERMINAL_DESIGNS.md).
- **Windows appearance:** opt-in light/dark and accent sync, supported per-app frame rules, and optional owned Windows Terminal profiles.
- **Desktop Studio:** eleven focused settings sections, separate fonts for UI parts, wallpaper-derived colors, draggable top-bar modules, independent panel/dock styles and six coordinated desktop presets with Apply/Undo.
- **Widgets:** clock, media, notes, resources and optional weather, with locked layouts, grid snapping and explicit resize controls.
- **Icon packs:** eight curated offline packs, 219 mapped PNG assets, native browsing, local import, per-app overrides and native-icon fallback.
- **Backgrounds:** local still images and restrained native animation, including the Woodland Zen scene. Reduced motion, battery and coverage rules pause animation.
- **Recovery:** optional native taskbar hiding with a separate recovery process, emergency restore, and crash/hang recovery. Explorer stays running; no Windows system files are replaced.

![Woodland Zen base artwork: a Japanese cottage overlooking a green pond](docs/images/woodland-zen.webp)
*AI-generated still artwork with native pond/mist effects at runtime; this image is not an animation or a desktop screenshot.*

## Build on Windows

Install Visual Studio 2022 with **Desktop development with C++**, Windows SDK and CMake tools. From a developer shell:

```powershell
cmake -S . -B build-v03 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build-v03 --config Release --parallel 4
```

Close an existing Aether instance before building/running another copy. Run `Start.cmd` after building, or launch `build-v03/Release/AetherUI.exe`. A fresh local profile is created; personal development profiles are not distributed. No Windows login startup entry is installed.

**Ctrl+Alt+E** opens Studio. Terminal customization and the dock-pin action live under **Terminal**. **Windows integration** holds optional OS appearance controls. **Ctrl+Alt+F12** restores the native desktop and disables hiding. Normal Quit restores Windows while retaining the preference for the next launch. Keep the recovery shortcut available before enabling taskbar hiding.

For core-only Linux builds, use standard CMake configure/build/CTest commands. Linux builds the portable model/tests, not the Windows desktop UI.

## Validation

VS2022 x64 Release/Debug builds are clean. All 57 suites have passing coverage across full and corrected focused runs in each configuration; eight native recovery groups passed. Detailed results are recorded in [TESTING.md](docs/TESTING.md). Hosted CI compiles all Windows targets and runs a smaller headless-safe test set; it does not certify interactive desktop recovery.

Explorer remains running. Optional hiding uses a versioned independent recovery journal that includes original layered-opacity attributes, so theme SHOW events can be visually suppressed and original taskbar composition restored. The five-second UI lease and emergency restore remain active. Broader Windows-version and extended pointer/frame-pacing validation remain preview limits.

## Project

Created by [Timothy Jankowski](https://bcts1100.github.io/Portfolio/projects/aether-ui.html), using an iterative design and AI-assisted development workflow. Inspired by Ubuntu/GNOME desktop interaction patterns and the customization demonstrated by [Seelen UI](https://github.com/eythaann/Seelen-UI). Aether uses its own native C++ implementation.

[Controls](docs/DESKTOP_POLISH_USAGE.md) · [Validation](docs/TESTING.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

No project-wide source license has been selected. Bundled third-party assets retain their own license terms and attribution.
