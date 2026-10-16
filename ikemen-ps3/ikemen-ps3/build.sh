#!/bin/sh
# Build ikemen-ps3.pkg inside Docker (no PS3 tools to install on your machine).
#   ./build.sh
# The image downloads the pre-built ps3dev toolchain (a ~170 MB download), so the first run takes a few minutes.
set -e
cd "$(dirname "$0")"

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required: https://docs.docker.com/get-docker/" >&2
    echo "No Docker? Push this folder to GitHub and let the included workflow build it (see README)." >&2
    exit 1
fi

docker build --platform linux/amd64 -t ikemen-ps3-build .

# On Linux, run as the current user so the build output isn't owned by root.
USER_ARGS=""
[ "$(uname -s)" = "Linux" ] && USER_ARGS="-u $(id -u):$(id -g)"

docker run --rm --platform linux/amd64 $USER_ARGS -v "$PWD":/build -w /build ikemen-ps3-build \
    sh -c "make clean && make pkg"

echo
ls -l ./*.pkg
echo "Copy the .pkg to a USB stick (or FTP) and install it from the XMB: Game > Install Package Files."
