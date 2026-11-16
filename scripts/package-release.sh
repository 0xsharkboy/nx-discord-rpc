#!/usr/bin/env bash
# Package the expected files from out/ (produced by `make dist`) into
# release/nx-discord-rpc-<version>.zip plus SHA256SUMS. Anything else in out/ is ignored.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(tr -d '[:space:]' < "$root/VERSION")"
zip_name="nx-discord-rpc-$version.zip"

files=(
  atmosphere/contents/4200000000D15C0D/exefs.nsp
  atmosphere/contents/4200000000D15C0D/toolbox.json
  atmosphere/contents/4200000000D15C0D/flags/boot2.flag
  switch/nx-discord-rpc/nx-discord-rpc.nro
  switch/.overlays/nx-discord-rpc.ovl
)

# Licence notices that must accompany the binaries, shipped at the zip root.
notices=THIRD_PARTY_NOTICES.md

missing=0
for f in "${files[@]}"; do
  if [ ! -f "$root/out/$f" ]; then
    echo "missing out/$f: run 'make dist' first" >&2
    missing=1
  fi
done
if [ ! -f "$root/$notices" ]; then
  echo "missing $notices" >&2
  missing=1
fi
[ "$missing" -eq 0 ] || exit 1

rm -rf "$root/release"
mkdir -p "$root/release"

# Zip from inside out/ so atmosphere/ and switch/ sit at the archive root.
# Entries are the expected files plus their parent directories, sorted, then the notices file at the
# root; -X drops extra attributes.
(
  cd "$root/out"
  {
    for f in "${files[@]}"; do
      d="$f"
      while d="$(dirname "$d")"; [ "$d" != "." ]; do echo "$d"; done
      echo "$f"
    done
  } | LC_ALL=C sort -u | zip -X -q "$root/release/$zip_name" -@
  zip -X -q "$root/release/$zip_name" -j "$root/$notices"
)

(
  cd "$root/release"
  sha256sum "$zip_name" > SHA256SUMS
)

echo "wrote release/$zip_name (with $notices) and release/SHA256SUMS"
