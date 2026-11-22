#!/usr/bin/env bash
# build-linux.sh -- build Phantom Hourglass from your own ROM on Linux (NX/Nix or a normal distro).
#
# Inside the flake:  nix develop -c ./scripts/build-linux.sh --rom /roms/your-dump.nds
# Outside Nix:       ./scripts/build-linux.sh --rom /roms/your-dump.nds
#                    (needs CMake >= 3.28, Ninja, clang, git; SDL3 is fetched by CMake)
#
# Options: --rom PATH --region usa|eur --jobs N --game-dir DIR --tests --clean --dry-run --help
# Environment: PH_ROM, PH_REGION, PH_ROMS_DIR, PH_GAME_DIR, and the usual Nix CMake variables.
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$SCRIPT_DIR/.." && pwd)
ROOT=$(cd -- "$REPO/.." && pwd)
BUILD_DIR="$REPO/build/linux-release"
GAME_DIR="${PH_GAME_DIR:-$ROOT/games/PhantomHourglass}"

rom_arg="${PH_ROM:-}"
region="${PH_REGION:-}"
jobs=""
game_dir_arg=""
tests=0
clean=0
dry_run=0

usage() {
  sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}
step() { printf '\033[36m[build-linux] %s\033[0m\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
run() {
  if (( dry_run )); then printf '[dry-run]'; printf ' %q' "$@"; printf '\n'; return 0; fi
  step "$*"
  "$@"
}

while (( $# )); do
  case "$1" in
    --rom)        rom_arg="${2:?--rom needs a path}"; shift 2 ;;
    --region)     region="${2:?--region needs usa or eur}"; shift 2 ;;
    --jobs|-j)    jobs="${2:?--jobs needs a number}"; shift 2 ;;
    --game-dir)   game_dir_arg="${2:?--game-dir needs a path}"; shift 2 ;;
    --tests)      tests=1; shift ;;
    --clean)      clean=1; shift ;;
    --dry-run)    dry_run=1; shift ;;
    -h|--help)    usage ;;
    *) fail "unknown option: $1 (try --help)" ;;
  esac
done
[[ -n "$game_dir_arg" ]] && GAME_DIR="$game_dir_arg"

# SHA-1 -> (region code, human name, priority). The known dumps are listed in the README.
declare -A ROM_CODE ROM_NAME ROM_SCORE
ROM_CODE[4c8f52dd719918bbcd46e73a8bae8628139c1b85]=azee; ROM_NAME[4c8f52dd719918bbcd46e73a8bae8628139c1b85]='USA (clean)'          ; ROM_SCORE[4c8f52dd719918bbcd46e73a8bae8628139c1b85]=4
ROM_CODE[7e0d26d143992f1eee182b24f1abb9e7f038eacf]=azee; ROM_NAME[7e0d26d143992f1eee182b24f1abb9e7f038eacf]='USA (D-pad patch)'    ; ROM_SCORE[7e0d26d143992f1eee182b24f1abb9e7f038eacf]=2
ROM_CODE[02be55db55cf254bd064d2b3eb368b92a5b4156d]=azep; ROM_NAME[02be55db55cf254bd064d2b3eb368b92a5b4156d]='Europe (clean)'       ; ROM_SCORE[02be55db55cf254bd064d2b3eb368b92a5b4156d]=3
ROM_CODE[8e3bbda6056c47f06fecf1a06d9f144a19283d88]=azep; ROM_NAME[8e3bbda6056c47f06fecf1a06d9f144a19283d88]='Europe (D-pad patch)'; ROM_SCORE[8e3bbda6056c47f06fecf1a06d9f144a19283d88]=1

want_code=""
case "${region,,}" in
  ''|auto) ;;
  usa|us|azee) want_code=azee ;;
  eur|eu|europe|azep) want_code=azep ;;
  *) fail "unknown --region '$region' (expected usa or eur)" ;;
esac

hash_of() { sha1sum -- "$1" | cut -d' ' -f1; }

find_rom() {
  local candidates=() dirs=() h best="" best_score=-1 c candidate_hash
  if [[ -n "$rom_arg" ]]; then
    [[ -f "$rom_arg" ]] || fail "ROM not found: $rom_arg"
    candidates+=("$rom_arg")
  else
    IFS=: read -ra dirs <<< "${PH_ROMS_DIR:-/roms:$REPO/roms:$ROOT/roms:$ROOT/roms/The_Legend_of_Zelda_Phantom_Hourglass_USA_DPAD-Patch}"
    for dir in "${dirs[@]}"; do
      [[ -d "$dir" ]] || continue
      while IFS= read -r -d '' c; do candidates+=("$c"); done < <(find "$dir" -maxdepth 1 -type f -iname '*.nds' -print0 2>/dev/null)
    done
    (( ${#candidates[@]} )) || fail "no .nds files found in: ${dirs[*]} (pass --rom PATH or set PH_ROM)"
  fi
  for c in "${candidates[@]}"; do
    candidate_hash=$(hash_of "$c")
    [[ -n ${ROM_CODE[$candidate_hash]:-} ]] || continue
    [[ -z "$want_code" || ${ROM_CODE[$candidate_hash]} == "$want_code" ]] || continue
    if (( ROM_SCORE[$candidate_hash] > best_score )); then
      best=$c; best_score=${ROM_SCORE[$candidate_hash]}
    fi
  done
  [[ -n "$best" ]] || fail "no supported Phantom Hourglass ROM${want_code:+ for region $want_code} found (checked ${#candidates[@]} file(s))"
  ROM_PATH=$(cd -- "$(dirname -- "$best")" && pwd)/$(basename -- "$best")
  ROM_HASH=$(hash_of "$ROM_PATH")
  ROM_CODE_OUT=${ROM_CODE[$ROM_HASH]}
  step "ROM: $ROM_PATH (${ROM_NAME[$ROM_HASH]}, $ROM_CODE_OUT)"
}

check_tools() {
  local c v
  for c in cmake ninja sha1sum git "${CC:-gcc}" "${CXX:-g++}"; do
    command -v "$c" >/dev/null || fail "$c is not installed (run 'nix develop' first, or install CMake >= 3.28, Ninja and GCC)"
  done
  v=$(cmake --version | head -1 | grep -Eo '[0-9]+\.[0-9]+' | head -1)
  if ! printf '%s\n%s\n' '3.28' "$v" | sort -V -C; then fail "CMake 3.28+ is required (found $v)"; fi
}

prepare_sources() {
  if [[ -n "${MELONDS_DIR:-}" ]]; then
    step "melonDS source: $MELONDS_DIR"
  elif [[ -f "$REPO/third_party/melonDS/src/NDS.cpp" ]]; then
    step "melonDS submodule: present"
  else
    run git -C "$REPO" submodule update --init --depth 1
    [[ -f "$REPO/third_party/melonDS/src/NDS.cpp" ]] || fail "melonDS submodule is still missing"
  fi
}

find_rom
check_tools
prepare_sources

if (( clean )); then run rm -rf -- "$BUILD_DIR"; fi

configure=(--preset linux-release "-DPH_ROM=$ROM_PATH")
[[ -n "${FETCHCONTENT_SOURCE_DIR_SDL3:-}" ]] && configure+=("-DFETCHCONTENT_SOURCE_DIR_SDL3=$FETCHCONTENT_SOURCE_DIR_SDL3")
[[ -n "${MELONDS_DIR:-}" ]] && configure+=("-DMELONDS_DIR=$MELONDS_DIR")
run cmake "${configure[@]}"

build=(cmake --build --preset linux-release)
[[ -n "$jobs" ]] && build+=(--parallel "$jobs")
run "${build[@]}"

if (( tests )); then run ctest --preset linux-release --output-on-failure; fi

exe="$BUILD_DIR/frontend/PhantomHourglass"
lib="$BUILD_DIR/frontend/libph_game.so"
if (( dry_run )); then step "would produce $exe and $lib"; step 'DRY RUN OK'; exit 0; fi
[[ -f "$exe" && -f "$lib" ]] || fail "build finished but $exe / $lib are missing"

# A ready-to-play folder, mirroring the Windows scripts' games/PhantomHourglass.
mkdir -p "$GAME_DIR/rom" "$GAME_DIR/data/saves"
cp -f -- "$exe" "$lib" "$GAME_DIR/"
cp -f -- "$ROM_PATH" "$GAME_DIR/rom/$ROM_CODE_OUT.nds"
: > "$GAME_DIR/portable.txt"   # keep settings and saves next to the game
cat > "$GAME_DIR/play.sh" <<EOF
#!/bin/sh
cd "\$(dirname "\$0")" || exit 1
exec ./PhantomHourglass "rom/${ROM_CODE_OUT}.nds" "\$@"
EOF
chmod +x "$GAME_DIR/play.sh"
step "BUILT: $GAME_DIR/play.sh"
