# Developer tools

Build.ps1 wraps the native build/tests. Measure*.ps1 and Validate*.ps1 provide explicit resource/validation tooling. Native recovery tests are documented in ../docs/TESTING.md.

Icon assets are already bundled. Optional regeneration uses Node.js plus the `sharp` package (`npm install --no-save sharp`), then `node tools/Prepare-IconPacks.cjs` and `node tools/Prepare-IconVariants.cjs` from the repository root. Node and sharp are not application runtime dependencies. The upstream source revisions and licenses remain in the generated asset directories.

Machine-specific recovery snapshots and old local recovery scripts are intentionally not distributed. Never replay a saved work-area snapshot from another machine/session.
