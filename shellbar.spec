Name:           shellbar
Version:        1.7.0
Release:        1%{?dist}
Summary:        Terminal emulator with configurable command toolbar

License:        MIT
URL:            https://rendergraf.github.io/shellbar/
Source0:        shellbar-%{version}.tar.gz

BuildRequires:  cmake >= 3.19
BuildRequires:  ninja-build
BuildRequires:  gcc
BuildRequires:  git
BuildRequires:  gtk4-devel >= 4.12
BuildRequires:  libadwaita-devel >= 1.5
BuildRequires:  pango-devel
BuildRequires:  cairo-devel
BuildRequires:  wget

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
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)

%install
rm -rf %{buildroot}
install -Dm755 build/shellbar %{buildroot}%{_bindir}/shellbar

for size in 16 24 32 48 64 128 256; do
  install -Dm644 assets/icon-${size}.png \
    %{buildroot}%{_datadir}/icons/hicolor/${size}x${size}/apps/shellbar.png
done
install -Dm644 assets/icon.svg \
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
if [ $1 -eq 0 ]; then
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
* Thu Jun 05 2026 Xavier Araque <xavieraraque@gmail.com> - 1.7.0-1
- Release v1.7.0
