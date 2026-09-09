#!/usr/bin/env bash
# build_matrix.sh — configure, build, and test spudlog for every meaningful
# combination of compiler × C++ standard × CMake option on Linux.
#
# Usage:
#   ./scripts/build_matrix.sh [OPTIONS]
#
# Options for what to build:
#   --build-type T   Override build type(s) as a space-separated list
#                    Default: --build-type "Debug Release"
#   --std N          Only use exactly this C++ standard; compilers that do not
#                    support it are skipped entirely (e.g. --std 20).
#   --std-min N      Skip C++ standards older than N (e.g. --std-min 17).
#   --latest-only    Only use the latest available version of each compiler
#                    family (gcc and clang) instead of all installed versions.
#   --asan           Enable address sanitizer for every combination
#                    (defaults --build-type to "Debug"; mutually exclusive with --tsan).
#   --tsan           Enable thread sanitizer for every combination
#                    (defaults --build-type to "Debug"; mutually exclusive with --asan).
#
# Options for how to build:
#   --jobs N         Parallel jobs for make and ctest.  Default: nproc.
#   --rebuild        Wipe each build directory before configuring (preserves
#                    _deps to avoid re-downloading dependencies).
#   --rebuild-deps   Like --rebuild but also removes _deps, forcing all
#                    fetched dependencies to be re-downloaded.
#   --keep-going     Continue to the next combination on failure instead of
#                    stopping.
#
# Other options:
#   --reuse-dir      Reuse a single build directory (build/cmaketools/) for
#                    every combination, wiping it between runs.  Without this
#                    flag each combination gets its own directory under
#                    build/<compiler>-<ver>/<cppstd>/<cmake-tuple>/
#   --dry-run        Print every command that would be run without executing.
#   -h, --help       Show this help and exit.

set -euo pipefail

# Constants
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
readonly script_dir repo_root

# Defaults
reuse_dir=0
rebuild=0
rebuild_deps=0
latest_only=0
build_types=("Debug" "Release")
build_types_explicit=0
jobs=$(nproc 2>/dev/null || echo 4)
std_min=11
std_only=""   # if set, only this standard is tried
sanitizer=""  # "asan" | "tsan" | ""
keep_going=0
dry_run=0

# Argument parsing
while [[ $# -gt 0 ]]; do
    case "$1" in
        --reuse-dir)    reuse_dir=1 ;;
        --rebuild)      rebuild=1 ;;
        --rebuild-deps) rebuild=1; rebuild_deps=1 ;;
        --latest-only)  latest_only=1 ;;
        --std-min)      shift; std_min="$1" ;;
        --std)          shift; std_only="$1" ;;
        --build-type)   shift; read -ra build_types <<< "$1"; build_types_explicit=1 ;;
        --jobs)         shift; jobs="$1" ;;
        --asan)         sanitizer+="asan" ;;
        --tsan)         sanitizer+="tsan" ;;
        --keep-going)   keep_going=1 ;;
        --dry-run)      dry_run=1 ;;
        -h|--help)
            awk '/^#!/{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

case "$sanitizer" in
    ""|asan|tsan) ;;
    *) echo "--asan and --tsan are mutually exclusive" >&2; exit 1 ;;
esac

# Sanitizers only produce meaningful results in Debug builds.
# If the user explicitly set --build-type, respect it but drop the sanitizer
# flag for non-Debug types (cmake will still build them, just without sanitizers).
# If using the default, silently narrow to Debug only.
if [[ -n "$sanitizer" ]]; then
    if [[ $build_types_explicit -eq 1 ]]; then
        if [[ "${build_types[*]}" != *Debug* ]]; then
            echo "Warning: --${sanitizer} has no effect without a Debug build type; sanitizer will not be applied" >&2
        fi
    else
        build_types=("Debug")
    fi
fi

# Helpers
pass=0
fail=0
skip=0
declare -a failures=()

log()  { echo "[build_matrix] $*"; }
run()  {
    if [[ $dry_run -eq 1 ]]; then
        echo "DRY-RUN: $*"
    else
        # Run in a new session so that SIGINT from the terminal (sent to the
        # foreground process group) never reaches cmake/make/git or any of
        # their children.  The worker subshell handles shutdown via the
        # abort_flag; in-flight builds are allowed to run to completion.
        setsid "$@"
    fi
}

# Prints a space-separated list of C++ standards (11–23) the compiler accepts.
probe_cxx_standards() {
    local cxx="$1"
    local supported=()
    for std in 11 14 17 20 23; do
        if $cxx -std=c++${std} -x c++ - /dev/null -o /dev/null &>/dev/null \
               <<< "int main(){}"; then
            supported+=("$std")
        fi
    done
    echo "${supported[*]}"
}

# Prints "gcc <major>" or "clang <major>" for a given compiler binary.
compiler_id() {
    local bin="$1"
    local ver
    if $bin --version 2>/dev/null | grep -qi clang; then
        ver=$($bin --version 2>/dev/null \
              | grep -oE 'version [0-9]+(\.[0-9]+)+' \
              | head -1 | grep -oE '[0-9]+(\.[0-9]+)+' | head -1)
        echo "clang ${ver%%.*}"
    else
        ver=$($bin --version 2>/dev/null \
              | grep -oE '[0-9]+(\.[0-9]+)+' | head -1)
        echo "gcc ${ver%%.*}"
    fi
}

# Keeps only the highest-versioned binary per compiler family (gcc / clang).
filter_latest_compilers() {
    local -n _in=$1   # input array (nameref)
    local -n _out=$2  # output array (nameref)

    declare -A _best_ver=()
    declare -A _best_bin=()

    local cxx family ver
    for cxx in "${_in[@]}"; do
        read -r family ver <<< "$(compiler_id "$cxx")"
        local cur_best="${_best_ver[$family]:-0}"
        if (( ver > cur_best )); then
            _best_ver[$family]="$ver"
            _best_bin[$family]="$cxx"
        fi
    done

    for family in "${!_best_bin[@]}"; do
        _out+=("${_best_bin[$family]}")
    done
}

discover_compilers() {
    local -n _out=$1
    local -a seen=()

    for cxx in $(compgen -c | grep -E '^(g\+\+|clang\+\+)(-[0-9]+)?$' | sort -u); do
        local full
        full=$(command -v "$cxx" 2>/dev/null) || continue

        # Deduplicate by resolving symlinks.
        local real
        real=$(readlink -f "$full" 2>/dev/null || echo "$full")
        if printf '%s\n' "${seen[@]+"${seen[@]}"}" | grep -qxF "$real"; then
            continue
        fi
        seen+=("$real")
        _out+=("$cxx")
    done
}

# ---------------------------------------------------------------------------
# Build one combination
# ---------------------------------------------------------------------------
# Args: cxx  cxx_ver  cxx_std  bld_type  cmake_label  cmake_extra_flags…
build_one() {
    local cxx_bin="$1"; shift
    local cxx_ver="$1"; shift   # e.g. "gcc-12" or "clang-15"
    local cxx_std="$1"; shift
    local bld_type="$1"; shift
    local cmake_label="$1"; shift
    # remaining args are -DFOO=BAR pairs

    local label="${cxx_ver}/cxx${cxx_std}/${bld_type}/${cmake_label}"
    banner "${label}"

    # Choose build directory.
    local build_dir
    if [[ $reuse_dir -eq 1 ]]; then
        build_dir="${repo_root}/build/cmaketools"
    else
        local safe_label="${cmake_label//[^a-zA-Z0-9._-]/_}"
        build_dir="${repo_root}/build/${cxx_ver}/cxx${cxx_std}/${bld_type}/${safe_label}"
    fi

    # Wipe / create build dir.
    if [[ $rebuild -eq 1 ]] && [[ -d "${build_dir}" ]]; then
        if [[ $rebuild_deps -eq 1 ]]; then
            rm -rf "${build_dir}"
        else
            find "${build_dir}" -mindepth 1 -maxdepth 1 -not -name '_deps' -exec rm -rf {} +
        fi
    fi
    run mkdir -p "${build_dir}"

    # Derive CC from CXX (g++ → g, clang++ → clang).
    local CC="${cxx_bin//\+\+/}"
    if ! command -v "${CC}" &>/dev/null; then CC="cc"; fi
    local CXX="${cxx_bin}"

    # Use libc++ for clang when available.
    local extra_cxxflags=""
    if $cxx_bin --version 2>/dev/null | grep -qi clang; then
        local clang_ver
        clang_ver=$(echo "$cxx_ver" | grep -oE '[0-9]+$')
        if [[ -d "/usr/lib/llvm-${clang_ver}/include/c++" ]] || \
           [[ -d "/usr/include/c++/v1" ]]; then
            extra_cxxflags="-stdlib=libc++"
        fi
    fi

    local ccache_flags=()
    if command -v ccache &>/dev/null; then
        ccache_flags+=(
            "-DCMAKE_C_COMPILER_LAUNCHER=ccache"
            "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
        )
    fi

    # Apply sanitizer only for Debug builds.
    local san_flags=()
    if [[ "$bld_type" == "Debug" ]]; then
        if [[ "$sanitizer" == "asan" ]]; then
            san_flags+=("-DSPDLOG_SANITIZE_ADDRESS=ON")
        elif [[ "$sanitizer" == "tsan" ]]; then
            san_flags+=("-DSPDLOG_SANITIZE_THREAD=ON")
        fi
    fi

    local cmake_cmd=(
        cmake
        -S "${repo_root}"
        -B "${build_dir}"
        "-DCMAKE_BUILD_TYPE=${bld_type}"
        "-DCMAKE_CXX_STANDARD=${cxx_std}"
        "-DCMAKE_CXX_COMPILER=${CXX}"
        "-DCMAKE_C_COMPILER=${CC}"
        -DSPDLOG_BUILD_EXAMPLE=ON
        -DSPDLOG_BUILD_EXAMPLE_HO=ON
        -DSPDLOG_BUILD_WARNINGS=ON
        -DSPDLOG_BUILD_BENCH=OFF
        -DSPDLOG_BUILD_TESTS=ON
        -DSPDLOG_BUILD_TESTS_HO=OFF
        "${ccache_flags[@]+"${ccache_flags[@]}"}"
        "${san_flags[@]+"${san_flags[@]}"}"
        "$@"
    )
    if [[ -n "$extra_cxxflags" ]]; then
        cmake_cmd+=("-DCMAKE_CXX_FLAGS=${extra_cxxflags}")
    fi

    log "Configure: ${cmake_cmd[*]}"
    if ! run "${cmake_cmd[@]}"; then
        log "CONFIGURE FAILED: ${label}"
        failures+=("CONFIGURE ${label}")
        (( fail++ )) || true
        [[ $keep_going -eq 1 ]] || exit 1
        return 1
    fi

    # ---------- build ----------
    if ! run cmake --build "${build_dir}" -- -j"${jobs}"; then
        log "BUILD FAILED: ${label}"
        failures+=("BUILD ${label}")
        (( fail++ )) || true
        [[ $keep_going -eq 1 ]] || exit 1
        return 1
    fi

    # ---------- test ----------
    if ! run ctest --test-dir "${build_dir}" -j"${jobs}" --output-on-failure; then
        log "TEST FAILED: ${label}"
        failures+=("TEST ${label}")
        (( fail++ )) || true
        [[ $keep_going -eq 1 ]] || exit 1
        return 1
    fi

    log "PASSED: ${label}"
    (( pass++ )) || true
}

# CMake option combinations (Linux-meaningful)
# Each entry: "label [-DFOO=x ...]"
declare -a option_combos=(
    "baseline"
    "shared -DSPDLOG_BUILD_SHARED=ON"
    "pch -DSPDLOG_ENABLE_PCH=ON"
    "pic -DSPDLOG_BUILD_PIC=ON"
    "std_format -DSPDLOG_USE_STD_FORMAT=ON"         # C++20+; filtered below
    "poly_alloc -DSPDLOG_POLYMORPHIC_ALLOCATORS=ON" # C++17+; filtered below
    "no_exceptions -DSPDLOG_NO_EXCEPTIONS=ON"
    "clock_coarse -DSPDLOG_CLOCK_COARSE=ON"
    "prevent_child_fd -DSPDLOG_PREVENT_CHILD_FD=ON"
    "no_thread_id -DSPDLOG_NO_THREAD_ID=ON"
    "no_tls -DSPDLOG_NO_TLS=ON"
    "no_atomic_levels -DSPDLOG_NO_ATOMIC_LEVELS=ON"
    "no_default_logger -DSPDLOG_DISABLE_DEFAULT_LOGGER=ON"
    "no_fwrite_unlocked -DSPDLOG_FWRITE_UNLOCKED=OFF"
    "no_tz_offset -DSPDLOG_NO_TZ_OFFSET=ON"
)

declare -a compilers=()
discover_compilers compilers
if [[ ${#compilers[@]} -eq 0 ]]; then
    echo "No g++ or clang++ compilers found." >&2
    exit 1
fi

if [[ $latest_only -eq 1 ]]; then
    declare -a _filtered=()
    filter_latest_compilers compilers _filtered
    compilers=("${_filtered[@]}")
    unset _filtered
fi

log "Found compilers: ${compilers[*]}"
log "Build types: ${build_types[*]}"
log "Sanitizer: ${sanitizer:-none}"
log "Reuse build dir: ${reuse_dir}"
log "Parallel jobs: ${jobs}"
echo ""

for cxx in "${compilers[@]}"; do
    # Identify compiler type and major version
    read -r comp_kind comp_majver <<< "$(compiler_id "$cxx")"
    cxx_ver_tag="${comp_kind}-${comp_majver}"

    # Probe supported C++ standards for this compiler
    standards_str=$(probe_cxx_standards "$cxx")
    if [[ -z "$standards_str" ]]; then
        log "SKIP ${cxx}: could not probe C++ standards"
        (( skip++ )) || true
        continue
    fi
    read -ra standards <<< "$standards_str"
    log "${cxx_ver_tag}: C++ standards supported: ${standards[*]}"

    if [[ -n "$std_only" ]]; then
        # Skip the compiler entirely if it doesn't support that standard
        found=0
        for s in "${standards[@]}"; do
            [[ "$s" == "$std_only" ]] && { found=1; break; }
        done
        if [[ $found -eq 0 ]]; then
            log "SKIP ${cxx_ver_tag}: does not support C++${std_only}"
            (( skip++ )) || true
            continue
        fi
        standards=("$std_only")
    fi

    for cxx_std in "${standards[@]}"; do
        if (( cxx_std < std_min )); then
            log "SKIP ${cxx_ver_tag}/cxx${cxx_std}: below --std-min ${std_min}"
            (( skip++ )) || true
            continue
        fi
        for bld_type in "${build_types[@]}"; do
            for combo in "${option_combos[@]}"; do
                # Parse the combo string: first word is label, rest are flags.
                read -ra combo_parts <<< "$combo"
                combo_label="${combo_parts[0]}"
                combo_flags=("${combo_parts[@]:1}")

                # Filter combinations that would be invalid or redundant

                # std::format requires C++20+
                if [[ "$combo_label" == "std_format" ]] && (( cxx_std < 20 )); then
                    log "SKIP ${cxx_ver_tag}/cxx${cxx_std}/${bld_type}/${combo_label}: requires C++20"
                    (( skip++ )) || true
                    continue
                fi

                # Polymorphic allocators require C++17+
                if [[ "$combo_label" == "poly_alloc" ]] && (( cxx_std < 17 )); then
                    log "SKIP ${cxx_ver_tag}/cxx${cxx_std}/${bld_type}/${combo_label}: requires C++17"
                    (( skip++ )) || true
                    continue
                fi

                # ---- Run this combination ----
                build_one \
                    "$cxx" \
                    "$cxx_ver_tag" \
                    "$cxx_std" \
                    "$bld_type" \
                    "$combo_label" \
                    "${combo_flags[@]+"${combo_flags[@]}"}"
            done
        done
    done
done

# Summary
echo ""
echo "=================================================================="
echo "  Build matrix summary"
echo "=================================================================="
echo "  PASSED : ${pass}"
echo "  FAILED : ${fail}"
echo "  SKIPPED: ${skip}"
if [[ ${#failures[@]} -gt 0 ]]; then
    echo ""
    echo "  Failed combinations:"
    for f in "${failures[@]}"; do
        echo "    - ${f}"
    done
fi
echo "=================================================================="
echo ""

# Postprocessing: Deduplicate _deps across build directories
if [[ $dry_run -eq 0 ]] && [[ $reuse_dir -eq 0 ]] && command -v jdupes &>/dev/null; then
    mapfile -t _deps_dirs < <(
        find "${repo_root}/build" -type d -name '_deps' 2>/dev/null
    )
    if [[ ${#_deps_dirs[@]} -gt 1 ]]; then
        log "Deduplicating ${#_deps_dirs[@]} _deps directories with jdupes..."
        jdupes --recurse --one-file-system --dedupe "${_deps_dirs[@]}" >/dev/null \
            || log "Warning: jdupes deduplication failed (non-fatal)"
    fi
    unset _deps_dirs
fi

[[ $fail -eq 0 ]]
