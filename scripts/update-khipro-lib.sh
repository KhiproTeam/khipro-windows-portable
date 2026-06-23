#!/usr/bin/env bash
# Update the vendored khipro library artifact in resources/khipro/.
#
# Always fetches the latest release from KhiproTeam/library and overwrites:
#   resources/khipro/include/khipro/khipro.h
#   resources/khipro/lib/libkhipro.a
#   resources/khipro/.tag                  (the library tag, e.g. "v35.0.1-1")
#
# The DLL and import lib (.dll.a) are skipped — this portable is static-only.
#
# Use cases:
#   - Manual: developer runs `make update-lib` to pull a new library release
#   - CI:     daily poll runs this when resources/khipro/.tag is behind latest
set -euo pipefail

OWNER_REPO="KhiproTeam/library"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/resources/khipro"

if ! command -v gh >/dev/null 2>&1; then
  echo "ERROR: gh (GitHub CLI) is required" >&2
  exit 1
fi
if ! command -v curl >/dev/null 2>&1; then
  echo "ERROR: curl is required" >&2
  exit 1
fi
if ! command -v unzip >/dev/null 2>&1; then
  echo "ERROR: unzip is required" >&2
  exit 1
fi

latest_tag=$(gh api "repos/${OWNER_REPO}/releases/latest" --jq '.tag_name')
if [[ -z "$latest_tag" ]]; then
  echo "ERROR: could not determine latest release tag" >&2
  exit 1
fi

asset_url=$(gh api "repos/${OWNER_REPO}/releases/latest" \
  --jq '.assets[] | select(.name | endswith("-windows-x86_64.zip")) | .browser_download_url')
if [[ -z "$asset_url" ]]; then
  echo "ERROR: latest release has no windows-x86_64.zip asset" >&2
  exit 1
fi

echo "Updating to ${latest_tag}"
echo "  from ${asset_url}"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

curl -fsSL "$asset_url" -o "${tmp_dir}/khipro.zip"
unzip -q "${tmp_dir}/khipro.zip" -d "${tmp_dir}/unpack"

src_include="${tmp_dir}/unpack/windows/x86_64/include/khipro/khipro.h"
src_lib="${tmp_dir}/unpack/windows/x86_64/lib/libkhipro.a"
if [[ ! -f "$src_include" ]] || [[ ! -f "$src_lib" ]]; then
  echo "ERROR: archive layout unexpected" >&2
  exit 1
fi

mkdir -p "${DEST}/include/khipro" "${DEST}/lib"
cp "$src_include" "${DEST}/include/khipro/khipro.h"
cp "$src_lib"     "${DEST}/lib/libkhipro.a"
echo "$latest_tag" > "${DEST}/.tag"

echo "Updated resources/khipro/ to ${latest_tag}"
echo "$latest_tag"
