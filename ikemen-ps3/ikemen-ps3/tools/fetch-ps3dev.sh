#!/bin/sh
# Download the newest pre-built ps3dev (PS3 compiler + PSL1GHT) for Linux x64 and unpack it into DEST.
#   sh tools/fetch-ps3dev.sh [DEST]      (default DEST = $HOME; creates DEST/ps3dev)
set -e
DEST=${1:-$HOME}

url=$(curl -fsSL "https://api.github.com/repos/ps3dev/ps3dev/releases?per_page=30" | python3 -c '
import json, sys
best = None
for rel in json.load(sys.stdin):
    if rel.get("draft"):
        continue
    for a in rel.get("assets", []):
        if a["name"] == "ps3dev-linux-X64.tar.gz" and (best is None or a["created_at"] > best["created_at"]):
            best = a
print(best["browser_download_url"] if best else "")
')
[ -n "$url" ] || { echo "No ps3dev-linux-X64.tar.gz release asset found" >&2; exit 1; }
echo "Downloading $url"
mkdir -p "$DEST"
curl -fsSL "$url" | tar xz -C "$DEST"
echo "Installed to $DEST/ps3dev"
