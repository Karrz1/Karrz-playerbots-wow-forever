#!/usr/bin/env bash
# Cloud compile check and unit tests for any thread (Linux, GCC 13, Ninja, ccache). Run from anywhere inside the checkout.
#
#   scripts/cloud-check.sh setup            install the packages (apt) and configure build-cloud/ once
#   scripts/cloud-check.sh compile [BASE]   compile ONLY the files changed against BASE (default origin/trunk) plus uncommitted edits
#   scripts/cloud-check.sh tests [-R re]    build the 'tests' target (everything it links) and run it through ctest
#   scripts/cloud-check.sh all              compile, then tests
#   scripts/cloud-check.sh stats            ccache hit rate
#
# Exit code 0 = everything requested compiled and passed. A compile error prints the compiler output; a failed test prints its output.
# This catches "does not compile" and unit-test regressions before a build reaches the PC. It cannot run the server or the scenarios.
# GCC accepts a few things MSVC rejects and vice versa, so a green run here is strong evidence, not proof, that MSVC builds.
#
# Notes: the first 'tests' build compiles the whole game library (long on 4 cores); ccache makes every later run fast, so keep
# CCACHE_DIR between runs when the container allows it. New .cpp files: re-run `setup` (the source glob is not CONFIGURE_DEPENDS).
set -euo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
BUILD="${CLOUD_BUILD_DIR:-$ROOT/build-cloud}"   # build*/ is already gitignored
JOBS="${CLOUD_JOBS:-$(nproc)}"
export CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-10G}"
export CCACHE_COMPILERCHECK=content

cmd="${1:-all}"; shift || true

setup() {
    if ! command -v ccache >/dev/null || ! command -v ninja >/dev/null || ! dpkg -s libboost-all-dev >/dev/null 2>&1; then
        echo "[cloud-check] installing packages"
        apt-get update -qq
        apt-get install -y -qq ccache ninja-build cmake g++-13 libboost-all-dev libmysqlclient-dev libreadline-dev \
            zlib1g-dev libbz2-dev libssl-dev git >/dev/null
    fi
    # TOOLS=1 is what builds tools_tests (needs extractor_common); set CLOUD_TOOLS=0 to skip it
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DSERVERS=1 -DSCRIPTS=static -DTOOLS="${CLOUD_TOOLS:-1}" -DBUILD_PLAYERBOT_V2=1 -DBUILD_TESTING=1 -DWITH_WARNINGS=0
}

ensure_configured() { [ -f "$BUILD/build.ninja" ] || setup; }

changed_files() {
    local base="${1:-origin/trunk}"
    { git -C "$ROOT" diff --name-only --diff-filter=ACMR "$base"...HEAD 2>/dev/null || true
      git -C "$ROOT" diff --name-only --diff-filter=ACMR HEAD
      git -C "$ROOT" ls-files --others --exclude-standard; } | sort -u
}

compile() {
    ensure_configured
    local base="${1:-origin/trunk}" files targets=() hdrs=()
    files="$(changed_files "$base")"
    # new .cpp files are not in build.ninja until the next configure
    if echo "$files" | grep -E '\.(cpp|c)$' | while read -r f; do grep -q "$f" "$BUILD/compile_commands.json" || exit 1; done; then :; else
        echo "[cloud-check] a changed .cpp is not in the build yet: reconfiguring"; setup
    fi
    while read -r f; do
        [ -z "$f" ] && continue
        case "$f" in
            *.cpp|*.c) obj="$(jq -r --arg f "$ROOT/$f" '.[] | select(.file==$f) | .output' "$BUILD/compile_commands.json" | head -1)"
                       [ -n "$obj" ] && targets+=("$obj") ;;
            *.h|*.hpp) hdrs+=("$f") ;;
        esac
    done <<<"$files"
    # a changed header: rebuild every object that includes it (ninja's depfile log; empty on the very first build)
    if [ ${#hdrs[@]} -gt 0 ]; then
        ninja -C "$BUILD" -t deps 2>/dev/null > "$BUILD/.deps.txt" || true
        for h in "${hdrs[@]}"; do
            awk -v h="/$h" 'BEGIN{o=""} /^[^ ]/{o=$1; sub(/:$/,"",o)} $1 ~ h"$" {print o}' "$BUILD/.deps.txt" >> "$BUILD/.hdr_targets" 2>/dev/null || true
        done
        if [ -s "$BUILD/.hdr_targets" ]; then mapfile -t more < <(sort -u "$BUILD/.hdr_targets"); targets+=("${more[@]}"); rm -f "$BUILD/.hdr_targets"
        else
            # no depfile data yet: build the libraries that can include it (the module's sub-libraries; 'game' for a core header).
            # 'playerbot-v2' itself is only an INTERFACE target, so it cannot be built.
            echo "[cloud-check] header changed and no dependency data yet: building the owning libraries instead"
            for h in "${hdrs[@]}"; do
                case "$h" in
                    src/modules/PlayerbotV2/*) mapfile -t libs < <(ninja -C "$BUILD" -t targets all 2>/dev/null | grep -oE '^playerbot-v2-[a-z]+:' | tr -d ':'); targets+=("${libs[@]}") ;;
                    *) targets+=(game) ;;
                esac
            done
        fi
    fi
    if [ ${#targets[@]} -eq 0 ]; then echo "[cloud-check] no C++ files changed against $base"; return 0; fi
    echo "[cloud-check] compiling ${#targets[@]} object(s)"
    ninja -C "$BUILD" -j "$JOBS" "${targets[@]}"
    echo "[cloud-check] compile OK"
}

run_tests() {
    ensure_configured
    local targets=(tests); grep -q 'tools_tests' "$BUILD/build.ninja" && targets+=(tools_tests)
    ninja -C "$BUILD" -j "$JOBS" "${targets[@]}"
    ( cd "$BUILD" && ctest --output-on-failure -j "$JOBS" "$@" )
}

case "$cmd" in
    setup) setup ;;
    compile) compile "$@" ;;
    tests) run_tests "$@" ;;
    all) compile "$@"; run_tests ;;
    stats) ccache -s ;;
    *) sed -n 2,12p "${BASH_SOURCE[0]}"; exit 2 ;;
esac
