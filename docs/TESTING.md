# Validation and known limitations

This is a locally tested source preview, not certification across every Windows configuration.

## Local Windows validation: 2026-10-17

VS2022 x64 Release and Debug all-target builds are clean. The first complete run in each configuration passed 55/57 suites. Accessibility and Customization selected old settings-page names after the navigation redesign. Corrected fixtures passed with Personalization, 3/3 in both builds. All 57 suites therefore have passing coverage across runs, not one uninterrupted 57/57 certification. The full runs included real pointer, keyboard, overview, tool, shell and visible-control overlap checks.

WindowsIntegration, WindowsShellApp and WindowsProviderShutdown each passed three consecutive repeats in both configurations with unchanged deadlines. The final bitmap-rendering correction passed the real tool suite again in both builds (53 Release checks with captures, 41 Debug checks without captures).

Terminal checks cover real CMD and PowerShell execution, file redirection, resizing, Ctrl+C cancellation, all ten live designs without restarting the shell, palette contrast and validated local image paths. WSL launches only if already installed; Aether does not install distributions.

Native recovery is opt-in and separate from CTest. All eight final Release/Debug groups passed: 216 native assertions per configuration across the native guard, actual startup, appbar-only and effects. Independent observers verify original visibility, layered composition, complete native state and physical monitor work areas. A separate outside-job sentinel protects failures. Intentional sentinel calibration is distinguished from normal cases, which must restore without emergency intervention. Machine-specific snapshots, profiles and raw logs are omitted from this repository.

An earlier startup recovery run failed its 500 ms responsiveness check, while exact emergency recovery passed. An unnecessary synchronous durability flush in the generated tools-theme cache was removed. Settings and recovery journals retain durable writes. Earlier failures remain in staged evidence rather than being reclassified as passing. A subsequent 97/98 startup run exposed a logical SHOW during a guarded transition. The final fixture requires independently queried zero alpha on every transition sample, logs logical visibility separately, and still requires strict logical hiding after settlement. Exact restoration checks remain unchanged.

## Taskbar transitions

The visibility-only approach still allowed sampled taskbar presentation during Windows restyling. Recovery journal version 5 also records original layered attributes before applying a zero-alpha guard. A combined startup/theme-switch probe recorded zero presented-taskbar samples across 71 transition sample cycles, followed by ten stable hidden samples and exact native restoration. Explorer still issued logical SHOW events in 27 samples: transparency prevents presentation during these events. Sampling does not certify every frame or every Windows version.

The helper restores original opacity with visibility and work areas. The production five-second UI lease, parent liveness, explicit Stop and emergency restore retain priority during bounded theme settlement. Explorer remains running; no system files are modified.

## Run checks

```powershell
cmake -S . -B build-v03 -A x64
cmake --build build-v03 --config Release --parallel
ctest --test-dir build-v03 -C Release --output-on-failure
```

Use an unlocked input-idle session for the complete native UI suite. Repeat with Debug. Hosted Windows CI builds all targets but runs only headless-safe core/configuration/performance-math checks. Ubuntu CI runs portable core checks. Neither certifies interactive Windows recovery.

For failure scenarios, close Aether and run AetherTaskbarRecoveryTests.exe with --native-acceptance, --app-startup-acceptance, --appbar-only-acceptance or --app-effects-acceptance in a disposable Windows session. These fixtures temporarily hide taskbars and terminate only owned processes. Never substitute another machine's captured native state.

## Remaining limits

- Complex graphemes, double-width terminal text, IME, mouse reporting, full xterm compatibility and complete terminal screen-reader support.
- File mutation and network browsing in Aether Files.
- Broader Windows-version/workspace compatibility, mixed-DPI, GPU/frame-pacing and long-duration resource validation.
- Failure injection for every preset rollback write.
- Icon packs are curated subsets; import accepts flat local PNG manifests, not arbitrary Linux SVG archives. Remote icon feeds and update/uninstall interfaces remain unfinished.
- Windows accent cache regeneration prevents claiming byte-for-byte absence of every Windows-generated cache value.

Short resource observations exclude DWM/GPU and do not certify leaks or long-duration behavior. Stock terminal backgrounds are static, local and demand-painted; they add no continuous animation timer. See [design research and controls](TERMINAL_DESIGNS.md).