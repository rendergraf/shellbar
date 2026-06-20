#!/bin/bash
# Build ShellBar AppImage
# Requires: cmake, ninja, zig, gtk4, libadwaita, linuxdeploy
set -e

VERSION="${1:-1.9.0}"
APP="shellbar"
OUTDIR="$(pwd)/build"

echo "=== Building ShellBar v${VERSION} AppImage ==="

# Build the binary
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build

# Prepare AppDir
rm -rf AppDir
mkdir -p AppDir/usr/bin AppDir/usr/share/applications AppDir/usr/share/icons/hicolor/256x256/apps

cp build/shellbar AppDir/usr/bin/

cat > AppDir/usr/share/applications/shellbar.desktop << 'EOF'
[Desktop Entry]
Type=Application
Name=ShellBar
Comment=A Ghostty-like terminal with a configurable command toolbar
Exec=shellbar
Icon=shellbar
Categories=Development;TerminalEmulator;GTK;
Terminal=false
EOF

cat > AppDir/AppRun << 'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/shellbar" "$@"
APPRUN
chmod +x AppDir/AppRun

# Copy icon if available
if [ -f assets/icon-256.png ]; then
  cp assets/icon-256.png AppDir/usr/share/icons/hicolor/256x256/apps/shellbar.png
else
  cp assets/logo.png AppDir/shellbar.png 2>/dev/null || true
fi

# Build AppImage with linuxdeploy
if command -v linuxdeploy &>/dev/null; then
  linuxdeploy --appdir AppDir --output appimage
  mv ShellBar-*.AppImage "${OUTDIR}/ShellBar-${VERSION}-x86_64.AppImage" 2>/dev/null || true
  echo "=== AppImage built: ${OUTDIR}/ShellBar-${VERSION}-x86_64.AppImage ==="
else
  echo "linuxdeploy not found. Install it:"
  echo "  wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  echo "  chmod +x linuxdeploy-x86_64.AppImage"
  echo "Then re-run this script."
fi
