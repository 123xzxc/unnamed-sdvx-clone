#!/usr/bin/env bash
#
# Builds unnamed-sdvx-clone for iPadOS / iOS.
#
# The script has to run on macOS with Xcode and the iOS SDK installed; there is no
# way to build an iOS binary anywhere else. It wraps the three steps that are
# otherwise easy to get wrong:
#
#   deps       build the third party libraries for the iOS triplets with vcpkg
#   configure  generate the Xcode project with CMake's built in iOS support
#   build      compile the app bundle
#
# Typical use:
#
#   ./build.sh deps                     # once, and after pulling new third party code
#   ./build.sh all                      # configure + build (simulator, arm64)
#   ./build.sh all --device             # configure + build for a physical iPad
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IOS_DIR="$(dirname "${SCRIPT_DIR}")"
REPO_ROOT="$(dirname "$(dirname "${IOS_DIR}")")"

# arm64 device build by default; --simulator switches to the Apple silicon simulator.
PLATFORM="OS64"
ARCH="arm64"
DEPLOYMENT_TARGET="${USC_IOS_DEPLOYMENT_TARGET:-15.0}"
TARGET="device"
CONFIG="Release"

BUILD_DIR="${REPO_ROOT}/build-ios"
DEPS_DIR="${BUILD_DIR}/vcpkg-installed"

usage() {
    sed -n '3,20p' "${BASH_SOURCE[0]}"
    cat <<'EOF'

Options:
  --simulator          build for the iOS simulator instead of a device
  --platform OS64|SIMULATORARM64|SIMULATOR64
                       override the CMake iOS platform (advanced)
  --debug              build the Debug configuration
  --clean              remove the build directory first
  --gamedir DIR        only used by the game, ignored here

Environment:
  VCPKG_ROOT                    path to a vcpkg checkout (required for "deps")
  USC_IOS_DEPLOYMENT_TARGET     minimum iOS version (default 15.0)
  USC_IOS_BUNDLE_ID             bundle identifier
  USC_IOS_TEAM_ID               Apple development team id for code signing
  USC_IOS_HTTP=OFF              build without libcurl (no Internet Ranking)
EOF
}

STEP="${1:-all}"
shift || true

while [[ $# -gt 0 ]]; do
    case "$1" in
        --simulator) PLATFORM="SIMULATORARM64"; ARCH="arm64"; TARGET="simulator" ;;
        --platform)
            PLATFORM="$2"
            if [[ "${PLATFORM}" == "SIMULATOR64" ]]; then ARCH="x86_64"; TARGET="simulator"; fi
            if [[ "${PLATFORM}" == SIMULATOR* ]]; then TARGET="simulator"; fi
            shift ;;
        --debug) CONFIG="Debug" ;;
        --clean) rm -rf "${BUILD_DIR}" ;;
        --gamedir) shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

# vcpkg triplet matching the CMake platform/architecture pair.
case "${PLATFORM}:${ARCH}" in
    OS64:arm64) TRIPLET="arm64-ios" ;;
    SIMULATORARM64:arm64) TRIPLET="arm64-ios-simulator" ;;
    SIMULATOR64:x86_64) TRIPLET="x64-ios" ;;
    *) TRIPLET="arm64-ios" ;;
esac

require_macos() {
    if [[ "$(uname -s)" != "Darwin" ]]; then
        echo "error: an iOS build needs macOS with Xcode installed." >&2
        exit 1
    fi
    if ! xcode-select -p >/dev/null 2>&1; then
        echo "error: Xcode command line tools are not configured (xcode-select -p failed)." >&2
        exit 1
    fi
}

cmd_deps() {
    require_macos
    if [[ -z "${VCPKG_ROOT:-}" ]]; then
        echo "error: set VCPKG_ROOT to a vcpkg checkout, e.g." >&2
        echo "       git clone https://github.com/microsoft/vcpkg && ./vcpkg/bootstrap-vcpkg.sh" >&2
        exit 1
    fi

    echo "==> building third party libraries for ${TRIPLET}"
    "${VCPKG_ROOT}/vcpkg" install \
        --triplet "${TRIPLET}" \
        --x-manifest-root="${IOS_DIR}" \
        --x-install-root="${DEPS_DIR}/${TRIPLET}"
}

cmd_configure() {
    require_macos
    if [[ ! -d "${DEPS_DIR}/${TRIPLET}" ]]; then
        echo "error: dependencies for ${TRIPLET} are missing, run: $0 deps" >&2
        exit 1
    fi

    local extra_defs=()
    if [[ "${USC_IOS_HTTP:-ON}" == "OFF" ]]; then
        extra_defs+=("-DUSC_IOS_HTTP=OFF")
    fi
    if [[ -n "${USC_IOS_BUNDLE_ID:-}" ]]; then
        extra_defs+=("-DUSC_IOS_BUNDLE_ID=${USC_IOS_BUNDLE_ID}")
    fi
    if [[ -n "${USC_IOS_TEAM_ID:-}" ]]; then
        extra_defs+=("-DUSC_IOS_TEAM_ID=${USC_IOS_TEAM_ID}")
    fi

    echo "==> configuring (${TARGET}, ${TRIPLET}, ${CONFIG})"
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}/project" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES="${ARCH}" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="${TRIPLET}" \
        -DVCPKG_INSTALLED_DIR="${DEPS_DIR}" \
        -DVCPKG_MANIFEST_MODE=OFF \
        "${extra_defs[@]}"
}

cmd_build() {
    require_macos
    echo "==> building"
    cmake --build "${BUILD_DIR}/project" --config "${CONFIG}" --target usc-game

    # The repository keeps its build output in bin/ (see the output directory setup
    # in the top level CMakeLists.txt), so look there as well as in the build tree.
    local app_path
    app_path="$(find "${BUILD_DIR}/project" "${REPO_ROOT}/bin" -maxdepth 4 -name 'usc-game.app' -print -quit 2>/dev/null || true)"
    if [[ -n "${app_path}" ]]; then
        echo "==> built ${app_path}"
        if [[ "${TARGET}" == "device" ]]; then
            echo "    install on a connected iPad with:"
            echo "    xcrun devicectl device install app --device <UDID> '${app_path}'"
        fi
    fi
}

case "${STEP}" in
    deps) cmd_deps ;;
    configure) cmd_configure ;;
    build) cmd_build ;;
    all) cmd_configure; cmd_build ;;
    open) open "${BUILD_DIR}/project/usc-game.xcodeproj" ;;
    *) usage; exit 1 ;;
esac
