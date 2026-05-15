#!/bin/bash
# ShellBar release builder
# Creates a .deb package and optionally publishes to GitHub Releases
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="${1:-1.4.0}"
DEB_NAME="shellbar_${VERSION}_amd64.deb"
BUILD_DIR="$PROJECT_DIR/build"
DEB_PATH="$BUILD_DIR/$DEB_NAME"

echo "=== Building ShellBar v${VERSION} ==="

cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -- -j"$(nproc)"

echo "=== Creating .deb package ==="

PKG_ROOT="$(mktemp -d)"
mkdir -p "$PKG_ROOT/DEBIAN"
mkdir -p "$PKG_ROOT/usr/bin"
mkdir -p "$PKG_ROOT/usr/share/applications"
mkdir -p "$PKG_ROOT/usr/share/doc/shellbar"

for size in 16 24 32 48 64 128 256; do
  mkdir -p "$PKG_ROOT/usr/share/icons/hicolor/${size}x${size}/apps"
  cp "$PROJECT_DIR/assets/icon-${size}.png" \
     "$PKG_ROOT/usr/share/icons/hicolor/${size}x${size}/apps/shellbar.png"
done
mkdir -p "$PKG_ROOT/usr/share/icons/hicolor/scalable/apps"
cp "$PROJECT_DIR/assets/icon.svg" \
   "$PKG_ROOT/usr/share/icons/hicolor/scalable/apps/shellbar.svg"

cp "$BUILD_DIR/shellbar" "$PKG_ROOT/usr/bin/"
cp "$PROJECT_DIR/README.md" "$PKG_ROOT/usr/share/doc/shellbar/"

cat > "$PKG_ROOT/DEBIAN/control" << EOF
Package: shellbar
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: amd64
Depends: libgtk-4-1 (>= 4.12), libadwaita-1-0 (>= 1.5), libpango-1.0-0, libcairo2, libc6 (>= 2.34)
Maintainer: Xavier Araque <xavieraraque@gmail.com>
Homepage: https://rendergraf.github.io/shellbar/
Description: Terminal emulator with configurable command toolbar
 ShellBar is a terminal emulator built on Ghostty's VT engine
 with a configurable toolbar for launching commands with a single click.
 .
 Features:
  - Configurable toolbar buttons from ~/.config/shellbar/config
  - Mouse text selection with copy/paste (Ctrl+Shift+C/V)
  - Multiple tabs with independent shells
  - Configurable keybinds
  - Dark theme matching Ghostty's appearance
EOF

cat > "$PKG_ROOT/DEBIAN/postinst" << 'EOF'
#!/bin/sh
set -e
if command -v gtk4-update-icon-cache >/dev/null 2>&1; then
    gtk4-update-icon-cache /usr/share/icons/hicolor/ || true
elif command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache /usr/share/icons/hicolor/ || true
fi
# KDE icon cache
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental 2>/dev/null || true
elif command -v kbuildsycoca5 >/dev/null 2>&1; then
    kbuildsycoca5 --noincremental 2>/dev/null || true
fi
EOF
chmod +x "$PKG_ROOT/DEBIAN/postinst"

cat > "$PKG_ROOT/usr/share/applications/shellbar.desktop" << EOF
[Desktop Entry]
Name=ShellBar
Comment=Terminal emulator with configurable command toolbar
Exec=shellbar
Icon=shellbar
Terminal=false
Type=Application
Categories=System;TerminalEmulator;
Keywords=terminal;shell;console;command;toolbar;
EOF

dpkg-deb --build "$PKG_ROOT" "$DEB_PATH"
rm -rf "$PKG_ROOT"

echo "=== Package built: $DEB_PATH ==="
echo ""
echo "To publish on GitHub Releases:"
echo "  gh release create v${VERSION} '$DEB_PATH' \\"
echo "    --title 'ShellBar v${VERSION}' \\"
echo "    --notes 'See https://rendergraf.github.io/shellbar/'"
echo ""
echo "Or create manually at:"
echo "  https://github.com/rendergraf/shellbar/releases/new?tag=v${VERSION}"
