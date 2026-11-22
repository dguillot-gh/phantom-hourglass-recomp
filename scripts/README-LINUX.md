# Building Phantom Hourglass on Linux

The Nix flake at the repository root provides the complete toolchain; the melonDS submodule and SDL3 are pinned in
`flake.nix` and `flake.lock`, so the build does not depend on whatever is installed on the machine.

## Quick start

With Nix flakes enabled (`experimental-features = nix-command flakes`) and your legally dumped ROM in `/roms/`
(any file name; it is identified by its SHA-1), from the repository root:

```sh
nix develop --command ./scripts/build-linux.sh
```

That drops you straight into the build. To stay in the shell and run steps yourself:

```sh
nix develop
./scripts/build-linux.sh --tests
```

The finished, playable folder is written to `games/PhantomHourglass/` next to the repository:

```
games/PhantomHourglass/
  PhantomHourglass      the program
  libph_game.so         the recompiled game code from your ROM
  rom/azee.nds          your ROM copy (azep.nds for a European dump)
  play.sh               start here
  portable.txt          keeps settings and saves in this folder
  data/saves/           your save and save states
```

Run `./play.sh` from that folder, or pass the ROM path directly:
`./PhantomHourglass rom/azee.nds`. The controls are the same as on Windows (see the main README).

## Options and environment

`./scripts/build-linux.sh [options]`

| Option | What it does |
|---|---|
| `--rom PATH` | Use a specific dump (also `PH_ROM`). |
| `--region usa\|eur` | Pick a dump when both regions are present (also `PH_REGION`). |
| `--jobs N` | Limit build parallelism. |
| `--game-dir DIR` | Put the playable folder somewhere else (also `PH_GAME_DIR`). |
| `--tests` | Run the C++ unit tests with CTest after building. |
| `--clean` | Remove `build/linux-release` before configuring. |
| `--dry-run` | Check the ROM, tools and commands without building. |

ROM search order: `--rom`/`PH_ROM`, then `PH_ROMS_DIR` (colon-separated), `/roms/`, the repository's `roms/`, and the
workspace's `roms/`. Supported SHA-1s are the four listed in the main README; unsupported dumps are rejected. The
`.nds` files are never committed.

The dev shell exports `FETCHCONTENT_SOURCE_DIR_SDL3` and `MELONDS_DIR` so CMake uses the pinned sources. To use your own
submodule checkout instead, unset `MELONDS_DIR` (the script will run `git submodule update --init` if needed). SDL3 is
fetched from GitHub only if you are building without Nix and without that variable set.

## Nix build

- `nix build .#program` — pure build of the host program and unit tests (no ROM; not playable on its own).
- `PH_ROM=/roms/your-dump.nds nix build .#game --impure` — full game. The ROM has to be copied into the Nix store
  during evaluation, hence `--impure`; the recompiled game is not cached between machines because it depends on your
  ROM. `result/bin/play-phantom-hourglass` launches it, with saves in `~/.local/share/phantom-hourglass`.

## Building without Nix

On a distro with CMake 3.28+, Ninja, GCC, Git and SDL3's Linux headers (X11/Wayland/audio) installed:

```sh
git submodule update --init --depth 1
./scripts/build-linux.sh --rom /roms/your-dump.nds
```

CMake fetches the pinned SDL3 3.4.18 release into `build/linux-release/_deps` the first time.
