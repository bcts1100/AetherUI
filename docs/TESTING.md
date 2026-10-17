# Validation and known limitations

This is a source preview, not a certified production desktop replacement.

## Last local Windows validation

Visual Studio 2022 x64 Release and Debug builds were warning-free. Each configuration passed 50 registered suites without real pointer/keyboard injection, 207 independent native recovery assertions, and six real preset Apply/Undo cycles. All eight recovery fixtures restored the captured taskbar visibility, complete native taskbar state and both physical monitor work areas, with zero corrective writes and no remaining owners.

The final native fixture uses the production five-second lease. An earlier two-second test override expired during Shell admission; that retained failure was not counted as passing. Primary and secondary taskbars are discovered through live enumeration plus bounded class lookups, because Windows can omit a valid taskbar from EnumWindows.

Before the last taskbar enumeration correction, Debug passed all 54 suites in one invocation. Release passed 53/54, then its corrected Studio keyboard fixture passed five consecutive runs in both builds. The fixture needed updated Tab counts after adding the sixth navigation page. Those earlier results are not final-binary input certification.

## Remaining validation

- Final input-sensitive suites: WindowsDockHover, WindowsKeyHook, WindowsOverview and WindowsPersonalization.
- Extended real-pointer widget stability and repeated live resize/capture gestures. One owned gesture run passed; a later resize assertion failed. No repeated gesture acceptance is claimed.
- Mixed-DPI, broader Windows-version compatibility, GPU/frame pacing and long-duration resource behavior.
- Failure-injection testing of preset rollback writes.

Real final preset cycles observed brief native taskbar visibility followed by automatic re-hide in 62–406 ms. Continuous invisibility through every Shell transition is not certified. The separate recovery process must remain enabled.

Workspace integration is Windows-version dependent; the Windows 11 workspace backend is not certified/supported by this preview. Icon packs are curated subsets; import expects a flat local PNG manifest, not an arbitrary Linux SVG archive. No remote icon feed or update/uninstall interface is provided.

## Run tests

```powershell
# No synthetic pointer or keyboard input; some tests still open owned UI windows.
ctest --test-dir build-v03 -C Release --output-on-failure -E "^(WindowsDockHover|WindowsKeyHook|WindowsOverview|WindowsPersonalization)$"

# Full interactive suite: use an unlocked, input-idle Windows session.
ctest --test-dir build-v03 -C Release --output-on-failure
```

Native recovery scenarios are deliberately opt-in, outside the normal CTest run. On a disposable Windows test session, close Aether and launch `AetherTaskbarRecoveryTests.exe` with one of `--native-acceptance`, `--app-startup-acceptance`, `--appbar-only-acceptance`, or `--app-effects-acceptance`. These fixtures capture their own native state and use an independent sentinel; they can temporarily hide taskbars and terminate only their owned test processes. Do not substitute another machine's captured state. Machine-specific development baselines, profiles, raw logs and recovery snapshots are intentionally omitted from the public repository.

## Resource observations

Short saved-profile Release observations measured approximately 0.4101% machine CPU / 166.45 MiB private with Studio open, and 0.0466% / 168.58 MiB private after closing Studio, over about 20 seconds each. Input was not controlled. Scope was the root plus directly named Aether helpers; DWM/GPU/frame pacing and missed between-sample processes were excluded. These are not idle benchmarks, leak certification or causal comparisons to an older build.

## Hosted CI

Windows CI compiles all targets and runs core/configuration/performance-math checks. Ubuntu CI runs the portable core tests. Interactive GUI, real-pointer, taskbar and crash/hang acceptance require a real Windows desktop and are not represented by a green hosted build.
