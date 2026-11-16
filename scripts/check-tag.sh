#!/usr/bin/env bash
# Fail unless the given tag is v<VERSION>, so a release never publishes a mismatched build.
set -euo pipefail

tag="${1:?usage: check-tag.sh <tag>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(tr -d '[:space:]' < "$root/VERSION")"

if [ "$tag" != "v$version" ]; then
  echo "tag $tag does not match VERSION $version" >&2
  exit 1
fi
