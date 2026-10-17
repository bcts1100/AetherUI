# Desktop polish usage

Use the staged build in `build-v03/Release/AetherUI.exe`, not the preserved original project.

- Desktop Studio > Personalize: widget layout lock and snap grid. Widgets start locked. Unlock to drag their headers or resize their bottom-right grips; lock again when finished. Escape, lost capture, and a lost mouse button cancel unfinished gestures.
- Desktop Studio > Icons & presets: six coordinated desktop presets, optional widget arrangement, explicit Apply and Undo. Undo is refused if relevant settings changed since Apply.
- Scroll below the preset cards for the icon browser. Choose a bundled pack or restore original app icons. Per-app custom icons retain priority; unmatched apps use their normal Windows icons.
- Import expects a folder containing a `pack.aethericons` manifest and flat local PNG assets. This release does not directly import Linux SVG theme archives. Bundled source images and license/provenance notices are retained.

The eight bundled packs are curated subsets, not complete upstream distributions: Papirus, Tela, Fluent, WhiteSur, Qogir, Yaru, Papirus Mono, and Papirus Neon. Together they map 219 PNG assets with aliases for common apps.

Hovering an auto-hide dock no longer recreates animated wallpaper when only the usable work area changes. Widget native window clipping now follows the selected corner radius and refreshes after resize/DPI changes. Studio's main content has an opaque backing for readable text over bright windows.

Taskbar hiding remains enabled by the saved/default settings. Explorer remains running. Normal quit and the independent recovery helper are responsible for restoring the native taskbars; no Windows system files are changed.

Limits: no remote icon feed, icon-pack update/uninstall interface, arbitrary Windows application skins, or raw Linux SVG archive importer. Frame-pacing/GPU and long-duration resource results must not be inferred from short CPU/memory samples. See TESTING.md for actual validated scope.