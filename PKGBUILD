# Maintainer: Xavier Araque <xavieraraque@gmail.com>
pkgname=shellbar
pkgver=1.9.0
pkgrel=1
pkgdesc="Terminal emulator with configurable command toolbar"
arch=('x86_64')
url="https://rendergraf.github.io/shellbar/"
license=('MIT')
depends=('gtk4' 'libadwaita' 'pango' 'cairo')
makedepends=('cmake' 'ninja' 'git' 'wget')
source=()
sha256sums=()

build() {
  cd "$startdir"
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build -- -j$(nproc)
}

package() {
  cd "$startdir"
  install -Dm755 build/shellbar "$pkgdir/usr/bin/shellbar"

  for size in 16 24 32 48 64 128 256; do
    install -Dm644 "assets/icon-${size}.png" \
      "$pkgdir/usr/share/icons/hicolor/${size}x${size}/apps/shellbar.png"
  done
  install -Dm644 assets/icon.svg \
    "$pkgdir/usr/share/icons/hicolor/scalable/apps/shellbar.svg"

  install -Dm644 /dev/stdin "$pkgdir/usr/share/applications/shellbar.desktop" << 'DESKTOP'
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

  install -Dm644 README.md \
    "$pkgdir/usr/share/doc/shellbar/README.md"
}
