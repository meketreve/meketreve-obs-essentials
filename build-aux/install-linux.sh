#!/usr/bin/env bash
#
# Build the plugin and install it into a local OBS Studio plugin directory.
#
# Usage:
#   ./build-aux/install-linux.sh              # build + install for native OBS
#   ./build-aux/install-linux.sh --flatpak    # install for Flatpak OBS
#   ./build-aux/install-linux.sh --no-build   # install an existing build only
#   ./build-aux/install-linux.sh --clean      # wipe the build dir first
#
set -euo pipefail

PLUGIN_NAME="meketreve-obs-essentials"
BUILD_DIR="build_x86_64"
CONFIG="RelWithDebInfo"

SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SOURCE_DIR"

do_build=1
do_clean=0
target_root="${HOME}/.config/obs-studio/plugins"

for arg in "$@"; do
  case "$arg" in
    --flatpak) target_root="${HOME}/.var/app/com.obsproject.Studio/config/obs-studio/plugins" ;;
    --no-build) do_build=0 ;;
    --clean) do_clean=1 ;;
    -h | --help)
      sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      echo "error: unknown option '$arg' (try --help)" >&2
      exit 1
      ;;
  esac
done

if ((do_build)); then
  if ! pkg-config --exists libobs; then
    echo "error: libobs development files not found." >&2
    echo "  Debian/Ubuntu/Mint: sudo apt install libobs-dev qt6-base-dev cmake build-essential" >&2
    exit 1
  fi

  # The bundled CMake presets require Ninja; fall back to Makefiles when absent.
  if command -v ninja >/dev/null 2>&1; then
    generator="Ninja"
  else
    generator="Unix Makefiles"
  fi

  ((do_clean)) && rm -rf "$BUILD_DIR"

  cmake -S . -B "$BUILD_DIR" -G "$generator" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_INSTALL_LIBDIR=lib/x86_64-linux-gnu \
    -DENABLE_FRONTEND_API=TRUE \
    -DENABLE_QT=TRUE

  cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

# The rundir layout differs per generator: multi-config tools nest under $CONFIG.
rundir="${BUILD_DIR}/rundir/${CONFIG}"
[[ -d "$rundir" ]] || rundir="${BUILD_DIR}/rundir"

module="${rundir}/${PLUGIN_NAME}.so"
data="${rundir}/${PLUGIN_NAME}"

if [[ ! -f "$module" ]]; then
  echo "error: ${module} not found — build first (omit --no-build)." >&2
  exit 1
fi

dest="${target_root}/${PLUGIN_NAME}"
install -Dm755 "$module" "${dest}/bin/64bit/${PLUGIN_NAME}.so"

rm -rf "${dest}/data"
if [[ -d "$data" ]]; then
  mkdir -p "${dest}/data"
  cp -r "${data}/." "${dest}/data/"
fi

echo "installed -> ${dest}"
echo "restart OBS Studio to load it."
