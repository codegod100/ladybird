#!/usr/bin/env bash
# Incremental Ladybird builds inside `nix develop .#ladybird`.
#
# Default: reuse outputs/build (ninja only — patch source and rebuild quickly).
# First run (or --clean): unpack + configure + full build + install.
#
# Usage (from repo root):
#   ./scripts/ladybird-devshell-build.sh              # incremental
#   ./scripts/ladybird-devshell-build.sh --clean      # wipe workdir, full rebuild
#   ./scripts/ladybird-devshell-build.sh --configure  # re-run cmake, keep objects
#   ./scripts/ladybird-devshell-build.sh --install    # ninja + cmake --install
#   ./scripts/ladybird-devshell-build.sh --shell      # interactive develop shell
#   ./scripts/ladybird-devshell-build.sh -- target    # ninja <target>
#
# Layout:
#   outputs/build/source/        writable Ladybird tree (edit/patch here)
#   outputs/build/source/build/  cmake/ninja build dir (object cache)
#   outputs/out/                 install prefix ($out)
#
# Prefer `nix build .#ladybird` for a store/Cachix build.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

mode=incremental
do_install=0
shell=0
ninja_args=()

usage() {
  sed -n '2,20p' "$0" | sed 's/^# \?//'
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h | --help) usage 0 ;;
    --clean) mode=clean; shift ;;
    --configure) mode=configure; shift ;;
    --install) do_install=1; shift ;;
    --shell) shell=1; shift ;;
    --) shift; ninja_args+=("$@"); break ;;
    -*)
      echo "unknown option: $1" >&2
      usage 2
      ;;
    *)
      ninja_args+=("$1")
      shift
      ;;
  esac
done

# Host toolchains (e.g. rustup under /usr/local/cargo) must not shadow Nix's.
scrub_path() {
  echo "$1" | tr ':' '\n' | grep -v '/usr/local/cargo' | grep -v '/.cargo/' | paste -sd: -
}

export PATH
PATH="$(scrub_path "$PATH")"
unset CARGO_HOME || true
unset RUSTUP_HOME || true

out_dir="${OUT_DIR:-$root/outputs/out}"
build_dir="${BUILD_DIR:-$root/outputs/build}"
src_dir="$build_dir/source"
cmake_dir="$src_dir/build"
mkdir -p "$out_dir" "$build_dir"

ninja_q="$(printf '%q ' "${ninja_args[@]+"${ninja_args[@]}"}")"
out_q="$(printf '%q' "$out_dir")"
work_q="$(printf '%q' "$build_dir")"
src_q="$(printf '%q' "$src_dir")"
cmake_q="$(printf '%q' "$cmake_dir")"

develop() {
  # Note: do not enable `set -u` inside the develop shell. nixpkgs'
  # cargoSetupHook does `if [ -z $cargoVendorDir ]` (unquoted, no default),
  # which aborts under nounset when using cargoDeps = fetchCargoVendor.
  nix develop .#ladybird -c bash --norc --noprofile -lc "$1"
}

setup_env_snippet="
  set -eo pipefail
  export PATH=\"\$(echo \"\$PATH\" | tr ':' '\\n' | grep -v /usr/local/cargo | grep -v /.cargo/ | paste -sd: -)\"
  unset CARGO_HOME RUSTUP_HOME
  : \"\${src:?missing src}\" \"\${cmakeFlags:?missing cmakeFlags}\"
  source \"\$stdenv/setup\"
  # gcc-wrapper purity drops -I paths outside /nix/store; needed for workspace builds.
  export NIX_ENFORCE_PURITY=0
  export out=$out_q
  work=$work_q
  src_tree=$src_q
  cmake_tree=$cmake_q
  mkdir -p \"\$out\"
"

if [[ "$shell" -eq 1 ]]; then
  echo "ladybird develop shell (patch under $src_dir; build in $cmake_dir)" >&2
  develop "
    $setup_env_snippet
    if [ -d \"\$cmake_tree\" ]; then
      cd \"\$cmake_tree\"
    elif [ -d \"\$src_tree\" ]; then
      cd \"\$src_tree\"
    else
      cd \"\$work\"
    fi
    echo \"NIX_ENFORCE_PURITY=\$NIX_ENFORCE_PURITY cwd=\$PWD\" >&2
    echo \"Edit sources in \$src_tree; rebuild with: ninja -j\${NIX_BUILD_CORES:-4}\" >&2
    exec bash --norc --noprofile
  "
  exit 0
fi

configured=0
if [[ -f "$cmake_dir/build.ninja" ]]; then
  configured=1
fi

if [[ "$mode" == "clean" ]]; then
  echo "ladybird develop-shell CLEAN build → $out_dir (workdir $build_dir)" >&2
  develop "
    $setup_env_snippet
    if [ -e \"\$work\" ]; then chmod -R u+w \"\$work\" || true; fi
    rm -rf \"\$work\"
    mkdir -p \"\$out\" \"\$work\"
    cd \"\$work\"
    echo \"NIX_BUILD_CORES=\${NIX_BUILD_CORES:-} NIX_ENFORCE_PURITY=\$NIX_ENFORCE_PURITY cwd=\$PWD\" >&2
    genericBuild
    echo \"installed:\" >&2
    ls -la \"\$out/bin\" >&2 || ls -la \"\$out\" >&2
  "
  exit 0
fi

if [[ "$configured" -eq 0 ]]; then
  echo "ladybird develop-shell FIRST build (unpack+configure+ninja) → $out_dir" >&2
  echo "sources will live at $src_dir (edit there for incremental rebuilds)" >&2
  develop "
    $setup_env_snippet
    mkdir -p \"\$work\"
    cd \"\$work\"
    echo \"NIX_BUILD_CORES=\${NIX_BUILD_CORES:-} NIX_ENFORCE_PURITY=\$NIX_ENFORCE_PURITY cwd=\$PWD\" >&2
    # Full phase run once; later invocations only ninja against this tree.
    genericBuild
    echo \"installed:\" >&2
    ls -la \"\$out/bin\" >&2 || ls -la \"\$out\" >&2
    echo \"next: edit \$src_tree then re-run $0 (incremental ninja)\" >&2
  "
  exit 0
fi

# Incremental path: keep object cache; optional cmake reconfigure.
echo "ladybird develop-shell INCREMENTAL → $cmake_dir" >&2
develop "
  $setup_env_snippet
  cd \"\$cmake_tree\"
  echo \"NIX_BUILD_CORES=\${NIX_BUILD_CORES:-} NIX_ENFORCE_PURITY=\$NIX_ENFORCE_PURITY cwd=\$PWD\" >&2
  if [ \"$mode\" = configure ]; then
    echo 're-running cmake...' >&2
    cmake .
  fi
  ninja -j\"\${NIX_BUILD_CORES:-4}\" $ninja_q
  if [ \"$do_install\" -eq 1 ]; then
    cmake --install .
    echo \"installed:\" >&2
    ls -la \"\$out/bin\" >&2 || ls -la \"\$out\" >&2
  else
    echo \"ninja done (object cache kept). install with: $0 --install\" >&2
  fi
"
