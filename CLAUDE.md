# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A mod for **The Legend of Zelda: Majora's Mask Recompiled** (a static recompilation of the N64 game, built with the [N64Recomp](https://github.com/N64Recomp/N64Recomp) framework). The mod's goal is to bring "BEN" (from the Ben Drowned creepypasta) to life as an in-game AI that uses the BEN statue as an avatar and manipulates the game's code to haunt the player.

The repo is forked from the [MMRecompModTemplate](https://github.com/Zelda64Recomp/MMRecompModTemplate). The code under `src/` (currently `always_spin_attack.c`) and `mod.toml`'s metadata are still the upstream template's example content — replace them as the BEN mod is built out. `TEMPLATE.md` is the original template README and documents the toolchain setup in detail.

## Build

This is a **two-stage build**. The `make` step cross-compiles mod C code into a MIPS ELF; `RecompModTool` then packages that ELF into the loadable `.nrm` mod file.

```sh
make                                 # compile src/*.c -> build/mod.elf (cross-compiled for MIPS)
RecompModTool mod.toml build         # package build/mod.elf -> build/<mod_filename>.nrm
make clean                           # rm -rf build/
make -j8                             # parallel compile
```

- `RecompModTool` is a separate prebuilt utility from [N64Recomp releases](https://github.com/N64Recomp/N64Recomp) — it is **not** in this repo and must be on PATH or invoked by path.
- The compiler **must** be a real `clang` + `ld.lld` with MIPS target support. Apple Clang does **not** work. On macOS install LLVM via Homebrew and, if `clang`/`ld.lld` aren't the default, pass them explicitly: `make CC=/opt/homebrew/opt/llvm/bin/clang LD=/opt/homebrew/opt/llvm/bin/ld.lld`. The same `CC`/`LD` may need to be passed to `RecompModTool`.
- LLVM 19.1.0 release binaries (and the chocolatey `llvm` package) have broken MIPS support — use 18.1.8.

The Makefile recursively globs `src/**/*.c`, so new source files and subfolders are picked up automatically with no Makefile edits.

## Submodules

Clone with `--recursive`, or run `git submodule update --init --recursive`.

- `mm-decomp` → [zeldaret/mm](https://github.com/zeldaret/mm): the Majora's Mask decompilation. Provides all game headers (`global.h`, actor structs, etc.) that mod code includes.
- `Zelda64RecompSyms` → reference symbol files (`mm.us.rev1.syms.toml`, `.datasyms.toml`, `.datasyms_static.toml`) that let the mod tool resolve base-game function/data addresses by name. Referenced from `mod.toml` under `[inputs]`.

## Architecture / how modding works

Mod code is ordinary C compiled against the decomp headers, but it never links against the game. Instead, special **section attributes** (defined in `include/modding.h`) mark symbols for the recomp mod tool, which wires them into the recompiled game at load time:

- `RECOMP_PATCH` — replace a base-game function with your definition. The function name must exactly match an existing function in the original ROM (resolved via the syms files). Example: `RECOMP_PATCH s32 Player_CanSpinAttack(Player* this)` in `src/always_spin_attack.c`.
- `RECOMP_HOOK(func)` / `RECOMP_HOOK_RETURN(func)` — run code at the entry / return of a base-game function without replacing it.
- `RECOMP_IMPORT(mod, func)` — call a function exported by the recomp runtime or another mod. The recomp API headers in `include/` (`recomputils.h`, `recompconfig.h`, `recompdata.h`, `z64recomp_api.h`, `recompui.h`) are all collections of `RECOMP_IMPORT` declarations.
- `RECOMP_EXPORT` — expose your function to other mods.
- `RECOMP_DECLARE_EVENT` / `RECOMP_CALLBACK(mod, event)` — declare/subscribe to events for inter-mod communication.

Because the mod tool finds these by section, IPO/inlining is deliberately suppressed on the relevant attributes — do not "optimize" the macros away.

### Available runtime APIs (all in `include/`)
- `recomputils.h` — `recomp_alloc`/`recomp_free`, `recomp_printf`, return-value getters for return hooks.
- `recompconfig.h` — read this mod's config options (`recomp_get_config_u32/double/string`), save-file path/swap helpers. Config options are declared in `mod.toml` under `[[manifest.config_options]]`.
- `recompdata.h` — runtime hashmap/hashset/slotmap collections (handles, not raw pointers).
- `z64recomp_api.h` — game-specific helpers, notably **actor data extensions** (`z64recomp_extend_actor*` + `z64recomp_get_extended_actor_data`) for attaching custom per-actor state. Register extensions before any actors spawn (e.g. from a `recomp_on_init` callback).
- `recompui.h` — in-game UI via the recompui API (see the template's `ui-example` branch).

### Manifest (`mod.toml`)
Defines mod id, version, display metadata, dependencies, native libraries, config option schema, and the `[inputs]` paths (`elf_path`, output `mod_filename`, and the reference syms files). The `id` must be globally unique.

## Updating the decomp target (advanced)

To target a newer `mm-decomp` commit, regenerate the symbol files: build N64Recomp (with `KEEP_MDEBUG=1` from a clean build) and the desired decomp `.elf`, copy both to the repo root, bump the `mm-decomp` submodule, then run `N64Recomp generate_symbols.toml --dump-context` and rename `dump.toml`/`data_dump.toml` into `Zelda64RecompSyms/mm.us.rev1.syms.toml` / `.datasyms.toml`. Full step-by-step (including handling missing-header and renamed-patch-function failures) is in `TEMPLATE.md`.

### Dummy headers
The decomp `#include`s generated asset headers that aren't present here. When a build fails on a missing asset header, create an **empty** file at the mirrored path under `include/dummy_headers/` (e.g. missing `assets/objects/object_cow/object_cow.h` → create empty `include/dummy_headers/objects/object_cow.h`). Several such stubs already exist there.
</content>
