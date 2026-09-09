#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin || { echo 'Build this app on macOS 13+ with Xcode command line tools.' >&2; exit 1; }
: "${TEAM_ID:?Set TEAM_ID to your Apple developer team ID}"
: "${BUNDLE_ID:?Set BUNDLE_ID to your registered host bundle identifier}"
: "${SIGN_IDENTITY:?Set SIGN_IDENTITY to your Developer ID Application signing identity}"
: "${HOST_PROFILE:?Set HOST_PROFILE to the host provisioning profile path}"
: "${EXTENSION_PROFILE:?Set EXTENSION_PROFILE to the packet extension provisioning profile path}"
case "$TEAM_ID" in *[!A-Z0-9]*|'') echo 'Invalid team ID' >&2; exit 1;; esac
case "$BUNDLE_ID" in *[!A-Za-z0-9.-]*|'') echo 'Invalid bundle ID' >&2; exit 1;; esac
test -f "$HOST_PROFILE" && test -f "$EXTENSION_PROFILE"
EXTENSION_ID="$BUNDLE_ID.packet-filter"
GROUP_ID="$TEAM_ID.$BUNDLE_ID"
SERVICE="$GROUP_ID.control"
APP="build/macos/Clumsier.app"
EXT="$APP/Contents/Library/SystemExtensions/$EXTENSION_ID.systemextension"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$EXT/Contents/MacOS" build/macos/objects

sh scripts/build-macos-native.sh
cp build/macos/Clumsier-host "$APP/Contents/MacOS/Clumsier"
cp build/macos/Clumsier-provider "$EXT/Contents/MacOS/ClumsierPacketFilter"
cp LICENSE "$APP/Contents/Resources/LICENSE.txt"
cp external/cjson/LICENSE "$APP/Contents/Resources/cJSON-LICENSE.txt"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>$BUNDLE_ID</string><key>CFBundleExecutable</key><string>Clumsier</string><key>CFBundleName</key><string>Clumsier</string><key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string><key>CFBundleShortVersionString</key><string>0.1</string><key>LSMinimumSystemVersion</key><string>13.0</string>
<key>ClumsierTeam</key><string>$TEAM_ID</string><key>ClumsierExtension</key><string>$EXTENSION_ID</string><key>ClumsierService</key><string>$SERVICE</string>
<key>NSSystemExtensionUsageDescription</key><string>Clumsier delays selected network packets only when you start a sequence.</string>
</dict></plist>
EOF
cat > "$EXT/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>$EXTENSION_ID</string><key>CFBundleExecutable</key><string>ClumsierPacketFilter</string><key>CFBundleName</key><string>Clumsier Packet Filter</string><key>CFBundlePackageType</key><string>SYSX</string>
<key>CFBundleVersion</key><string>1</string><key>CFBundleShortVersionString</key><string>0.1</string><key>LSMinimumSystemVersion</key><string>13.0</string>
<key>ClumsierTeam</key><string>$TEAM_ID</string><key>ClumsierHost</key><string>$BUNDLE_ID</string>
<key>NetworkExtension</key><dict><key>NEMachServiceName</key><string>$SERVICE</string><key>NEProviderClasses</key><dict><key>com.apple.networkextension.filter-packet</key><string>ClumsierPacketProvider</string></dict></dict>
<key>NSSystemExtensionUsageDescription</key><string>Delay selected packets for a Clumsier sequence.</string>
</dict></plist>
EOF
for kind in host extension; do
  if [ "$kind" = host ]; then ENTITLEMENT_ID="$BUNDLE_ID"; else ENTITLEMENT_ID="$EXTENSION_ID"; fi
  cat > "build/macos/$kind.entitlements" <<EOF
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict>
<key>com.apple.security.app-sandbox</key><true/>
<key>com.apple.application-identifier</key><string>$TEAM_ID.$ENTITLEMENT_ID</string>
<key>com.apple.developer.team-identifier</key><string>$TEAM_ID</string>
<key>com.apple.security.application-groups</key><array><string>$GROUP_ID</string></array>
<key>com.apple.developer.networking.networkextension</key><array><string>content-filter-provider-systemextension</string></array>
EOF
  if [ "$kind" = host ]; then
    cat >> "build/macos/$kind.entitlements" <<EOF
<key>com.apple.developer.system-extension.install</key><true/><key>com.apple.security.files.user-selected.read-only</key><true/>
EOF
  fi
  printf '%s\n' '</dict></plist>' >> "build/macos/$kind.entitlements"
done
cp "$HOST_PROFILE" "$APP/Contents/embedded.provisionprofile"
cp "$EXTENSION_PROFILE" "$EXT/Contents/embedded.provisionprofile"
codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" --entitlements build/macos/extension.entitlements "$EXT"
codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" --entitlements build/macos/host.entitlements "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
echo "Built and signed $APP. Nothing installed or activated. See docs/MACOS.md for testing and notarization."
