# Production renderer delivery

This versioned overlay supplies renderer shader sources, material descriptors,
pipeline descriptors and small textures independently of `testModule/` and
`utils/tests/`. `manifest.json` records every delivered file's byte size and
SHA-256. Windows CMake verifies the complete source and installs changed files
before building the application, including builds that do not relink it.
Python 3.9 or newer is required; no Python packages or network access are needed.
The repository attributes preserve delivery bytes across Git checkouts, including
machines with `core.autocrlf`, so the same checksum manifest works on all hosts.

Run `python utils/assets/install_production_assets.py --verify` to verify the
delivery, or omit `--verify` to install it into `assets/engine`. `--assets PATH`
selects another destination. Installation replaces only listed overlay files;
unrelated local assets and engine/game configuration remain intact. A missing,
modified or unlisted delivery source fails verification before any installation.
Each changed file is replaced atomically. An I/O failure during installation can
leave an incomplete overlay; rerunning the command safely finishes it.

The RC0 sources were captured from the effective `assets/engine` tree on
2026-11-04, then overlaid with `material_assets/engine`, matching the previous
material build step. They retain the seven shader/pipeline SHA-256 anchors in
`docs/rendering/review-commits-2026-11-04.md`, including VG and temporal GI sources
and `rendering/hybrid-sp.json` / `hybrid-sp-rt.json`. The older `render_assets`
fixture tree is not the delivery authority.

Named test/smoke/check presets and materials, GI comparison presets, compute graph
test presets, shader test fixture directories, generated SPIR-V/cache files and
local shader migration/compiler helper files are omitted. Models and scenes are
not included. Reusable shader implementations remain production assets.

This is a renderer overlay, not a complete game-content release. A normal launch
still needs the external base assets (startup configs, fonts, models, scenes,
scripts and other game content), provisioned by the existing setup/assets sync
workflow. Its public Drive folder is mutable and is not pinned by this manifest.
Game mount overrides still take precedence over engine assets. A runtime baseline
must therefore record the hashes of the actual complete mounted asset tree as
well as this delivery manifest and executable. Do not interpret this overlay's
checksum as a checksum of the external base release.

To change a delivered source, update its manifest size and SHA-256 in the same
change and advance `bundleVersion`. Review the asset and manifest together.
