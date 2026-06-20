#!/bin/bash
# ShellBar release builder
# Creates .deb (Debian/Ubuntu), .pkg.tar.zst (Arch Linux), and .rpm (Fedora) packages
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="${1:-1.9.0}"
BUILD_DIR="$PROJECT_DIR/build"
DEB_NAME="shellbar_${VERSION}_amd64.deb"
DEB_PATH="$BUILD_DIR/$DEB_NAME"
ARCH_NAME="shellbar-${VERSION}-1-x86_64.pkg.tar.zst"
RPM_NAME="shellbar-${VERSION}-1.x86_64.rpm"
RPM_PATH="$BUILD_DIR/x86_64/$RPM_NAME"

# Parse arguments
BUILD_DEB=true
BUILD_ARCH=true
BUILD_RPM=true

for arg in "$@"; do
  case "$arg" in
    --deb-only) BUILD_ARCH=false; BUILD_RPM=false ;;
    --arch-only) BUILD_DEB=false; BUILD_RPM=false ;;
    --rpm-only) BUILD_DEB=false; BUILD_ARCH=false ;;
  esac
done

ZIG_VERSION="0.15.2"
ZIG_TARBALL="zig-x86_64-linux-${ZIG_VERSION}.tar.xz"
ZIG_URL="https://ziglang.org/download/${ZIG_VERSION}/${ZIG_TARBALL}"

echo "=== Building ShellBar v${VERSION} ==="

# Download Zig if not present
if [ ! -f "$BUILD_DIR/zig-x86_64-linux-${ZIG_VERSION}/zig" ]; then
  echo "--- Downloading Zig ${ZIG_VERSION} ---"
  mkdir -p "$BUILD_DIR"
  wget -q --show-progress -O "$BUILD_DIR/$ZIG_TARBALL" "$ZIG_URL"
  tar -xf "$BUILD_DIR/$ZIG_TARBALL" -C "$BUILD_DIR"
  rm -f "$BUILD_DIR/$ZIG_TARBALL"
  echo "--- Zig installed: $BUILD_DIR/zig-x86_64-linux-${ZIG_VERSION}/zig ---"
else
  echo "--- Zig ${ZIG_VERSION} already present ---"
fi

export PATH="$BUILD_DIR/zig-x86_64-linux-${ZIG_VERSION}:$PATH"

# Clean CMake cache if Zig path changed
if [ -f "$BUILD_DIR/CMakeCache.txt" ] && grep -q "/tmp/zig" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
  echo "--- Cleaning stale CMake cache ---"
  rm -f "$BUILD_DIR/CMakeCache.txt"
fi

cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -- -j"$(nproc)"

# --- .deb package (Debian/Ubuntu) ---
if [ "$BUILD_DEB" = true ]; then
  if ! command -v dpkg-deb &>/dev/null; then
    echo "=== Skipping .deb package: dpkg-deb not found (not a Debian system) ==="
    BUILD_DEB=false
  else
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

    echo "=== .deb package built: $DEB_PATH ==="
  fi
fi

# --- .pkg.tar.zst package (Arch Linux) ---
if [ "$BUILD_ARCH" = true ]; then
  if ! command -v makepkg &>/dev/null; then
    echo "=== Skipping Arch package: makepkg not found (not an Arch system) ==="
    BUILD_ARCH=false
  else
    echo "=== Creating Arch Linux package ==="

    ARCH_BUILD_DIR="$(mktemp -d)"

    cat > "$ARCH_BUILD_DIR/PKGBUILD" << EOF
# Maintainer: Xavier Araque <xavieraraque@gmail.com>
pkgname=shellbar
pkgver=${VERSION}
pkgrel=1
pkgdesc="Terminal emulator with configurable command toolbar"
arch=('x86_64')
url="https://rendergraf.github.io/shellbar/"
license=('MIT')
depends=('gtk4' 'libadwaita' 'pango' 'cairo')
source=()
sha256sums=()

package() {
  install -Dm755 "$BUILD_DIR/shellbar" "\$pkgdir/usr/bin/shellbar"

  for size in 16 24 32 48 64 128 256; do
    install -Dm644 "$PROJECT_DIR/assets/icon-\${size}.png" \\
      "\$pkgdir/usr/share/icons/hicolor/\${size}x\${size}/apps/shellbar.png"
  done
  install -Dm644 "$PROJECT_DIR/assets/icon.svg" \\
    "\$pkgdir/usr/share/icons/hicolor/scalable/apps/shellbar.svg"

  install -Dm644 /dev/stdin "\$pkgdir/usr/share/applications/shellbar.desktop" << 'DESKTOP'
[Desktop Entry]
Name=ShellBar
Comment=Terminal emulator with configurable command toolbar
Exec=shellbar
Icon=shellbar
Terminal=false
Type=Application
Categories=System;TerminalEmulator;
Keywords=terminal;shell;console;command;toolbar;
DESKTOP

  install -Dm644 "$PROJECT_DIR/README.md" \\
    "\$pkgdir/usr/share/doc/shellbar/README.md"
}
EOF

    makepkg -D "$ARCH_BUILD_DIR" --dir "$ARCH_BUILD_DIR" --nocheck --skippgpcheck 2>/dev/null || \
      (cd "$ARCH_BUILD_DIR" && makepkg --nocheck --skippgpcheck)

    mv "$ARCH_BUILD_DIR"/shellbar-*.pkg.tar.zst "$BUILD_DIR/$ARCH_NAME"
    rm -rf "$ARCH_BUILD_DIR"

    echo "=== Arch package built: $BUILD_DIR/$ARCH_NAME ==="
  fi
fi

# --- .rpm package (Fedora/RHEL) ---
if [ "$BUILD_RPM" = true ]; then
  if ! command -v rpmbuild &>/dev/null; then
    echo "=== Skipping .rpm package: rpmbuild not found ==="
    BUILD_RPM=false
  else
    echo "=== Creating Fedora/RHEL .rpm package ==="

    RPM_BUILD_DIR="$(mktemp -d)"
    mkdir -p "$RPM_BUILD_DIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

    cat > "$RPM_BUILD_DIR/SPECS/shellbar.spec" << EOF
Name:           shellbar
Version:        ${VERSION}
Release:        1%{?dist}
Summary:        Terminal emulator with configurable command toolbar

License:        MIT
URL:            https://rendergraf.github.io/shellbar/
Source0:        shellbar-${VERSION}.tar.gz

Requires:       gtk4 >= 4.12
Requires:       libadwaita >= 1.5
Requires:       pango
Requires:       cairo

%description
ShellBar is a terminal emulator built on Ghostty's VT engine with a
configurable toolbar for launching commands with a single click.

Features:
 - Configurable toolbar buttons from ~/.config/shellbar/config
 - Mouse text selection with copy/paste (Ctrl+Shift+C/V)
 - Multiple tabs with independent shells
 - Configurable keybinds
 - Dark theme matching Ghostty's appearance
 - Utility bar with auto-detected TUI tools
 - URL detection with Ctrl+Click to open

%prep
%setup -q -n shellbar

%build
# Download Zig for libghostty-vt build
ZIG_VERSION="0.15.2"
if [ ! -f build/zig-x86_64-linux-\${ZIG_VERSION}/zig ]; then
  mkdir -p build
  wget -q -O build/zig.tar.xz https://ziglang.org/download/\${ZIG_VERSION}/zig-x86_64-linux-\${ZIG_VERSION}.tar.xz
  tar -xf build/zig.tar.xz -C build
  rm -f build/zig.tar.xz
fi
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j\$(nproc)

%install
rm -rf %{buildroot}
install -Dm755 build/shellbar %{buildroot}%{_bindir}/shellbar

for size in 16 24 32 48 64 128 256; do
  install -Dm644 assets/icon-\${size}.png \\
    %{buildroot}%{_datadir}/icons/hicolor/\${size}x\${size}/apps/shellbar.png
done
install -Dm644 assets/icon.svg \\
  %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/shellbar.svg

install -Dm644 /dev/stdin %{buildroot}%{_datadir}/applications/shellbar.desktop << 'DESKTOP'
[Desktop Entry]
Name=ShellBar
Comment=Terminal emulator with configurable command toolbar
Exec=shellbar
Icon=shellbar
Terminal=false
Type=Application
Categories=System;TerminalEmulator;
Keywords=terminal;shell;console;command;toolbar;
DESKTOP

%post
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
if [ -x /usr/bin/gtk4-update-icon-cache ]; then
  gtk4-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
elif [ -x /usr/bin/gtk-update-icon-cache ]; then
  gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi

%postun
if [ \$1 -eq 0 ]; then
  /bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
  if [ -x /usr/bin/gtk4-update-icon-cache ]; then
    gtk4-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
  elif [ -x /usr/bin/gtk-update-icon-cache ]; then
    gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
  fi
fi

%files
%license LICENSE
%doc README.md
%{_bindir}/shellbar
%{_datadir}/applications/shellbar.desktop
%{_datadir}/icons/hicolor/*/apps/shellbar.*

%changelog
* $(date '+%a %b %d %Y') Xavier Araque <xavieraraque@gmail.com> - ${VERSION}-1
- Release v${VERSION}
EOF

    # Create source tarball
    TARBALL_DIR="$(mktemp -d)/shellbar"
    mkdir -p "$TARBALL_DIR"
    rsync -a --exclude='.git' --exclude='build' "$PROJECT_DIR/" "$TARBALL_DIR/"
    tar czf "$RPM_BUILD_DIR/SOURCES/shellbar-${VERSION}.tar.gz" -C "$(dirname "$TARBALL_DIR")" shellbar
    rm -rf "$(dirname "$TARBALL_DIR")"

    rpmbuild --define "_topdir $RPM_BUILD_DIR" -bb "$RPM_BUILD_DIR/SPECS/shellbar.spec" 2>/dev/null

    RPM_OUTPUT="$RPM_BUILD_DIR/RPMS/x86_64/$RPM_NAME"
    if [ -f "$RPM_OUTPUT" ]; then
      mkdir -p "$BUILD_DIR"
      cp "$RPM_OUTPUT" "$BUILD_DIR/$RPM_NAME"
      echo "=== RPM package built: $BUILD_DIR/$RPM_NAME ==="
    else
      echo "=== RPM build failed ==="
      BUILD_RPM=false
    fi

    rm -rf "$RPM_BUILD_DIR"
  fi
fi

echo ""
echo "=== Build complete ==="
[ "$BUILD_DEB" = true ] && echo "  Debian/Ubuntu: $DEB_PATH"
[ "$BUILD_ARCH" = true ] && echo "  Arch Linux:    $BUILD_DIR/$ARCH_NAME"
[ "$BUILD_RPM" = true ] && echo "  Fedora/RHEL:   $BUILD_DIR/$RPM_NAME"
echo ""
echo "To publish on GitHub Releases:"
RELEASE_FILES=""
[ "$BUILD_DEB" = true ] && RELEASE_FILES="$RELEASE_FILES '$DEB_PATH'"
[ "$BUILD_ARCH" = true ] && RELEASE_FILES="$RELEASE_FILES '$BUILD_DIR/$ARCH_NAME'"
[ "$BUILD_RPM" = true ] && RELEASE_FILES="$RELEASE_FILES '$BUILD_DIR/$RPM_NAME'"
if [ -n "$RELEASE_FILES" ]; then
  echo "  gh release create v${VERSION} \\"
  for f in $RELEASE_FILES; do
    echo "    $f \\"
  done
  echo "    --title 'ShellBar v${VERSION}' \\"
  echo "    --notes 'See https://rendergraf.github.io/shellbar/'"
fi
echo ""
echo "Or create manually at:"
echo "  https://github.com/rendergraf/shellbar/releases/new?tag=v${VERSION}"
echo ""
echo "Install locally:"
if command -v pacman &>/dev/null; then
  echo "  sudo pacman -U $BUILD_DIR/$ARCH_NAME"
elif command -v rpm &>/dev/null; then
  echo "  sudo rpm -i $BUILD_DIR/$RPM_NAME"
elif command -v dpkg &>/dev/null; then
  echo "  sudo dpkg -i $DEB_PATH && sudo apt-get install -f"
fi
