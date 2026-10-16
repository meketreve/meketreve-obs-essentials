#!/bin/bash
# Cloud sessions only: what it takes to build the plugin and run its tests
# (the same packages as .github/scripts/utils.zsh/setup_ubuntu, plus the
# formatters at the versions check-format.yaml uses).
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"
export DEBIAN_FRONTEND=noninteractive
# Never wait on a prompt: a hook has no one to answer it.
exec </dev/null
APT=($SUDO apt-get -y -qq --no-install-recommends -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold)

$SUDO apt-get update -qq
"${APT[@]}" install \
  build-essential cmake ninja-build pkg-config jq software-properties-common \
  qt6-base-dev qt6-base-private-dev libqt6svg6-dev libgles2-mesa-dev libsimde-dev >/dev/null

# libobs and its CMake files come with the obs-studio package of the OBS PPA.
# The environment's network must allow ppa.launchpadcontent.net; without it
# nothing that links libobs builds (see the message below).
have_obs=0
if dpkg -s obs-studio >/dev/null 2>&1; then
  have_obs=1
elif $SUDO add-apt-repository --yes ppa:obsproject/obs-studio >/dev/null 2>&1 &&
  $SUDO apt-get update -qq &&
  "${APT[@]}" install obs-studio >/dev/null; then
  have_obs=1
else
  # A failed PPA would break every later apt-get update.
  $SUDO add-apt-repository --yes --remove ppa:obsproject/obs-studio >/dev/null 2>&1 || true
fi

python3 -m pip install -q --break-system-packages clang-format==19.1.1 gersemi==0.21.0 2>/dev/null ||
  python3 -m pip install -q clang-format==19.1.1 gersemi==0.21.0

cd "$CLAUDE_PROJECT_DIR"
if [ "$have_obs" = 1 ]; then
  cmake -S . -B build_dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_FRONTEND_API=ON -DENABLE_QT=ON -DENABLE_DEV_TOOLS=ON >/dev/null
  echo "session-start: OBS found; build with: cmake --build build_dev && (cd build_dev && ctest --output-on-failure)"
else
  echo "session-start: the OBS PPA is not reachable (allow ppa.launchpadcontent.net in the environment's network);" \
    "so the plugin cannot be built here, only code that needs Qt alone." >&2
fi
