#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-release}"
STAGE_DIR="${STAGE_DIR:-/tmp/auxlab2-stage}"
DMG_STAGE_DIR="${DMG_STAGE_DIR:-/tmp/auxlab2-dmg}"
KEYCHAIN_PROFILE="${KEYCHAIN_PROFILE:-auxlab2-notary}"
VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")"
APP_NAME="auxlab2.app"
APP_PATH="$STAGE_DIR/$APP_NAME"
DMG_BASENAME="${DMG_BASENAME:-auxlab2-${VERSION}-macos-arm64}"
DMG_PATH="$BUILD_DIR/${DMG_BASENAME}.dmg"

if [[ -z "${AUXLAB_CERT:-}" ]]; then
  echo "error: set AUXLAB_CERT to your Developer ID Application certificate name" >&2
  echo 'example: export AUXLAB_CERT="Developer ID Application: Your Name (TEAMID)"' >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "error: cmake not found" >&2
  exit 1
fi

if ! command -v xcrun >/dev/null 2>&1; then
  echo "error: xcrun not found" >&2
  exit 1
fi

if ! security find-identity -v -p codesigning | grep -F "$AUXLAB_CERT" >/dev/null 2>&1; then
  echo "error: signing identity not visible to security find-identity:" >&2
  echo "  $AUXLAB_CERT" >&2
  exit 1
fi

echo "==> Configuring Release build"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> Building Release"
cmake --build "$BUILD_DIR" --config Release -j

echo "==> Installing to staging directory"
rm -rf "$STAGE_DIR"
cmake --install "$BUILD_DIR" --config Release --prefix "$STAGE_DIR"

if [[ ! -d "$APP_PATH" ]]; then
  echo "error: staged app not found at $APP_PATH" >&2
  exit 1
fi

echo "==> Pruning stale versioned app binaries"
while IFS= read -r -d '' path; do
  echo "removing stale binary: $path"
  rm -f "$path"
done < <(
  find "$APP_PATH/Contents/MacOS" \
    -maxdepth 1 \
    -type f \
    -name 'auxlab2-*' \
    ! -name "auxlab2-${VERSION}" \
    -print0
)

echo "==> Signing standalone executables and dylibs"
while IFS= read -r -d '' path; do
  codesign --force --timestamp --options runtime --sign "$AUXLAB_CERT" "$path"
done < <(
  find "$APP_PATH/Contents/MacOS" "$APP_PATH/Contents/Frameworks" "$APP_PATH/Contents/PlugIns" \
    -type f \
    \( -path "$APP_PATH/Contents/MacOS/*" -o -name "*.dylib" \) \
    -print0
)

echo "==> Signing framework bundles"
while IFS= read -r -d '' path; do
  codesign --force --timestamp --options runtime --sign "$AUXLAB_CERT" "$path"
done < <(
  find "$APP_PATH/Contents/Frameworks" -type d -name "*.framework" -print0
)

echo "==> Signing app bundle"
codesign --force --timestamp --options runtime --sign "$AUXLAB_CERT" "$APP_PATH"

echo "==> Verifying app bundle"
codesign --verify --deep --strict --verbose=2 "$APP_PATH"
spctl --assess --type execute --verbose=4 "$APP_PATH" || true

echo "==> Building DMG payload"
rm -rf "$DMG_STAGE_DIR"
mkdir -p "$DMG_STAGE_DIR"
cp -R "$APP_PATH" "$DMG_STAGE_DIR/"
ln -s /Applications "$DMG_STAGE_DIR/Applications"

echo "==> Creating DMG"
rm -f "$DMG_PATH"
hdiutil create \
  -volname "auxlab2" \
  -srcfolder "$DMG_STAGE_DIR" \
  -ov \
  -format UDZO \
  "$DMG_PATH"

echo "==> Signing DMG"
codesign --force --timestamp --sign "$AUXLAB_CERT" "$DMG_PATH"
codesign --verify --verbose=2 "$DMG_PATH"

echo "==> Submitting DMG for notarization"
notary_json="$(xcrun notarytool submit "$DMG_PATH" \
  --keychain-profile "$KEYCHAIN_PROFILE" \
  --wait \
  --output-format json)"
printf '%s\n' "$notary_json"

notary_id="$(printf '%s\n' "$notary_json" | sed -n 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
notary_status="$(printf '%s\n' "$notary_json" | sed -n 's/.*"status"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"

if [[ -z "$notary_id" || -z "$notary_status" ]]; then
  echo "error: unable to parse notarytool submission result" >&2
  exit 1
fi

if [[ "$notary_status" != "Accepted" ]]; then
  echo "==> Notarization log"
  xcrun notarytool log "$notary_id" --keychain-profile "$KEYCHAIN_PROFILE"
  echo "error: notarization failed with status $notary_status" >&2
  exit 1
fi

echo "==> Stapling notarization ticket"
xcrun stapler staple "$DMG_PATH"

echo "==> Final validation"
xcrun stapler validate "$DMG_PATH"

MOUNT_POINT="$(mktemp -d /tmp/auxlab2-mount.XXXXXX)"
cleanup_mount() {
  if mount | grep -F "on $MOUNT_POINT " >/dev/null 2>&1; then
    hdiutil detach "$MOUNT_POINT" >/dev/null 2>&1 || true
  fi
  rm -rf "$MOUNT_POINT"
}
trap cleanup_mount EXIT

hdiutil attach "$DMG_PATH" -mountpoint "$MOUNT_POINT" -nobrowse >/dev/null
spctl --assess --type execute --verbose=4 "$MOUNT_POINT/$APP_NAME"
cleanup_mount
trap - EXIT

echo
echo "Release DMG ready:"
echo "  $DMG_PATH"
