#!/usr/bin/env bash
# Package out/ (produced by `make dist`) into release/nx-discord-rpc-<version>.zip plus SHA256SUMS.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(tr -d '[:space:]' < "$root/VERSION")"
zip_name="nx-discord-rpc-$version.zip"

[ -d "$root/out/atmosphere" ] && [ -d "$root/out/switch" ] || {
  echo "out/ is incomplete: run 'make dist' first" >&2
  exit 1
}

rm -rf "$root/release"
mkdir -p "$root/release"

# Zip from inside out/ so atmosphere/ and switch/ sit at the archive root.
# Sorted entry list and -X (no extra file attributes) keep the layout stable.
(
  cd "$root/out"
  find atmosphere switch | LC_ALL=C sort | zip -X -q "$root/release/$zip_name" -@
)

(
  cd "$root/release"
  sha256sum "$zip_name" > SHA256SUMS
)

echo "wrote release/$zip_name and release/SHA256SUMS"
