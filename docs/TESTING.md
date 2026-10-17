# Validation and known limitations

This is a locally tested source preview, not certification across every Windows configuration.

## Local Windows validation: 2026-10-17

VS2022 x64 Release and Debug all-target builds are clean. The latest complete local Windows runs each passed **58/58 suites**, including pointer, keyboard, overview, tools, visible settings-control overlap, provider shutdown and the new asynchronous status checks.

The first Release attempt passed 57/58: its provider-shutdown fixture still assumed at most three helpers after status became a fourth provider. Actual shutdown was already within its original three-second deadline. The fixture now explicitly waits for the status provider to enter its blocked operation and accounts for that owned helper. The deadline is unchanged; both complete suites were then rerun successfully. Earlier evidence remains retained in the staged project.

Terminal checks cover real CMD and PowerShell execution, file redirection, resizing, Ctrl+C cancellation, all ten live designs without restarting the shell, palette contrast and validated local image paths. WSL launches only if already installed; Aether does not install distributions.

The native guard, startup and appbar-only groups passed in both configurations, and Release effects passed. Debug effects initially passed 37/38, failing the four-second per-monitor wallpaper-paint readiness condition. Two unchanged repeats passed 38/38 each; a third failed the same readiness condition (23/24 recorded before the incomplete run ended). Taskbar assertions passed in those retained runs, but effects readiness is **not fully accepted**. There are 228 passing native assertions per configuration across selected runs, not an uninterrupted native-matrix certification. Independent strict post-checks after every run, including the failures, confirmed exact native restoration with zero corrective writes.

Native recovery is opt-in and separate from CTest. Independent observers verify original visibility, layered composition, complete native state and physical monitor work areas. A separate outside-job sentinel protects failures. Intentional sentinel calibration is distinguished from normal cases, which must restore without emergency intervention. Machine-specific snapshots, profiles and raw logs are omitted from this repository.

An earlier startup recovery run failed its 500 ms responsiveness check, while exact emergency recovery passed. An unnecessary synchronous durability flush in the generated tools-theme cache was removed. Settings and recovery journals retain durable writes. Earlier failures remain in staged evidence rather than being reclassified as passing. A subsequent 97/98 startup run exposed a logical SHOW during a guarded transition. The final fixture requires independently queried zero alpha on every transition sample, logs logical visibility separately, and still requires strict logical hiding after settlement. Exact restoration checks remain unchanged.

## Normal-use taskbar regression

A recorded controller lease expiry correctly restored Windows, but the failure handler also saved hiding as disabled. Restarting Aether therefore left the Windows taskbar visible. Runtime recovery and the saved startup preference are now separate. Automatic recovery retains the startup choice, including through unrelated settings saves. **Desktop Studio > Behavior & recovery > Resume taskbar hiding** starts a fresh validated watchdog after recovery. An explicit off action or emergency restore still disables the startup choice. A genuine UI hang restores Windows without silently rearming.

Audio endpoint and network-status COM queries now run in a bounded, cancellable owned provider; volume and mute controls use the same asynchronous path. The new self-provider fixture checks coalesced operations, malformed replies, blocked-provider timeout, UI dispatch and shutdown cancellation without changing actual audio. New watchdog diagnostics record the active UI dispatch at any future lease expiry. The original blocking call was not captured, so status COM is not claimed as the proven trigger of that old stall.

## Taskbar transitions

The visibility-only approach still allowed sampled taskbar presentation during Windows restyling. Recovery journal version 5 also records original layered attributes before applying a zero-alpha guard. A combined startup/theme-switch probe recorded zero presented-taskbar samples across 71 transition sample cycles, followed by ten stable hidden samples and exact native restoration. Explorer still issued logical SHOW events in 27 samples: transparency prevents presentation during these events. Sampling does not certify every frame or every Windows version.

The helper restores original opacity with visibility and work areas. The production five-second UI lease, parent liveness, explicit Stop and emergency restore retain priority during bounded theme settlement. Explorer remains running; no system files are modified.

## Run checks

```powershell
cmake -S . -B build-v03 -A x64
cmake --build build-v03 --config Release --parallel
ctest --test-dir build-v03 -C Release --output-on-failure
```

Use an unlocked input-idle session for the complete native UI suite. Repeat with Debug. Hosted Windows CI builds all targets but runs only headless-safe core/configuration/async-status/performance-math checks. Ubuntu CI runs portable core checks. Neither certifies interactive Windows recovery.

For failure scenarios, close Aether and run AetherTaskbarRecoveryTests.exe with --native-acceptance, --app-startup-acceptance, --appbar-only-acceptance or --app-effects-acceptance in a disposable Windows session. These fixtures temporarily hide taskbars and terminate only owned processes. Never substitute another machine's captured native state.

## Remaining limits

- Complex graphemes, double-width terminal text, IME, mouse reporting, full xterm compatibility and complete terminal screen-reader support.
- File mutation and network browsing in Aether Files.
- Broader Windows-version/workspace compatibility, mixed-DPI, GPU/frame-pacing and long-duration resource validation.
- Failure injection for every preset rollback write.
- Icon packs are curated subsets; import accepts flat local PNG manifests, not arbitrary Linux SVG archives. Remote icon feeds and update/uninstall interfaces remain unfinished.
- Windows accent cache regeneration prevents claiming byte-for-byte absence of every Windows-generated cache value.

Short resource observations exclude DWM/GPU and do not certify leaks or long-duration behavior. Stock terminal backgrounds are static, local and demand-painted; they add no continuous animation timer. See [design research and controls](TERMINAL_DESIGNS.md).
Latest post-test saved-profile observation covered 16.07 seconds: 0.0972% machine CPU (0.778% of one logical CPU) and 113.14 MiB average private memory across the root and verified direct Aether helpers. Input was uncontrolled. DWM/GPU and processes entirely between samples are excluded; this is not an idle or long-duration benchmark.
