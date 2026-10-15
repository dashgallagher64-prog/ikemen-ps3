#!/bin/sh
# One-command PKG build using Docker.
#   ./build.sh            builds ikemen-ps3.pkg (and the .gnpdrm.pkg) in this folder
#
# The first run builds the PS3 toolchain image from https://github.com/ps3dev/ps3dev-docker.
# That compiles gcc for the Cell CPU and takes a long time (expect 30-90+ min). It is cached
# by Docker afterwards, so later builds take seconds.
set -e
cd "$(dirname "$0")"

IMAGE=${IKEMEN_PS3_IMAGE:-ps3dev-docker}

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required: https://docs.docker.com/get-docker/" >&2
    echo "No Docker? Push this folder to GitHub and let the included workflow build it (see README)." >&2
    exit 1
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "==> Building the PS3 toolchain image '$IMAGE' (one-time, slow)..."
    tmp=$(mktemp -d)
    git clone --depth 1 https://github.com/ps3dev/ps3dev-docker "$tmp/ps3dev-docker"
    docker build -t "$IMAGE" "$tmp/ps3dev-docker"
    rm -rf "$tmp"
fi

# On Linux, run as the current user so the build output isn't owned by root.
USER_ARGS=""
[ "$(uname -s)" = "Linux" ] && USER_ARGS="-u $(id -u):$(id -g)"

echo "==> Building the package..."
docker run --rm $USER_ARGS -v "$PWD":/build -w /build "$IMAGE" sh -c "make clean && make pkg"

echo
ls -l ./*.pkg
echo "Copy the .pkg to a USB stick (or FTP) and install it from the XMB: Game > Install Package Files."
