# Project rules

- Keep reusable examples `src/EcsDemo.h` and `src/ReflectionDemo.h`.
- Put test, smoke, benchmark scenario drivers and test-only debug implementations in the optional root `testModule/`. Put test scripts in `utils/tests/`. Production targets must build without either directory and must not link the test module.
- Keep the organized testModule sources in version control. Keep temporary build checks and local test scripts out of version control; add permanent tests as separate tasks. Never move production APIs such as logging, profiling, screenshots or primitive drawing into the test module merely because tests use them.
- The optional out-of-process crash reporter executable lives in `testModule/tools/crashReporter/`; production crash-handling APIs remain in the engine.
- Format changed C/C++ files with `clang-format --style=file` using the repository `.clang-format`. Do not reformat unrelated files, third-party dependencies or generated code.
- Use one declaration per field, descriptive names, explicit initialization and separate files for components and systems. Keep nontrivial implementation out of headers.
- Serialize configuration through typed serdepp descriptors. Resolve names and validate types at load time; use enums and resolved identifiers during execution.
- Do not delete suspected legacy code without the user's explicit selection. Check registrations, reflection, asset references and platform-specific consumers before proposing removal.

## Resources

- The only resource roots are `assets/` and `assets_packs/`. Do not create `production_assets/`, `material_assets/`, `render_assets/`, or `script_assets/`.
- `assets/` is packed for every platform. Put there only what Web, Android, and Quest need to start: `assets/engine` (core renderer, configs, fonts) and the small `assets/game` set.
- A feature pack is `assets_packs/<pack>/` with `pack.json` (`id`, `platforms`, `requires`) and the usual folders `shaders/`, `materials/`, `models/`, `textures/`, `renderPipeline/`. Use a pack for a platform cut and also for a cleaner split of a feature that still ships everywhere (`platforms` includes every target, or `["*"]`).
- A pack is mounted at `/` the same way as `assets/engine`. The pack name is not part of the virtual path and must not appear in C++ strings. Keep the relative path the runtime already opens (`/shaders/...`, `/materials/...`, `/models/...`).
- One relative path lives in one place. A file may `#include` another only when every platform that ships the includer also ships the included file (`requires` pulls that pack in).
- Heavy demos (Bistro, Sponza, third-person, Pica Pica) are desktop packs under `assets_packs/demo_*`. Their bytes stay out of git; `pack.json` stays tracked. `sync_assets.py` writes game content and those demo packs, and does not overwrite `assets/engine` or feature packs.
- Smoke and tests do not write into `assets/` and are not packs. They belong in `utils/tests/` and `testModule/`.
- `ResourcePackManager` `.pack` files are preload lists. They are not these content packs.

## Learning mode

- The primary goal of this project is to learn. Treat the user as a student of engine architecture and modern C++: explain *why*, not only *what*; prefer short teaching asides when a design trade-off matters.
- Challenge the user's statements when they are incomplete or misleading. Do not agree for politeness. Push back with concrete code evidence, industry counter-examples, and a clearer mental model.
- Prefer "here is the usual industry practice, here is why your case differs, here is what I recommend for learning" over absolute dogma.
