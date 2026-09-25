#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(tr -d '[:space:]' < "$ROOT/VERSION")"
make -C "$ROOT" -j"$(nproc)"
strip "$ROOT/bin/ACS-cpp"
PKG="/tmp/acs-cpp-deb/acs-cpp_${VER}_amd64"
rm -rf /tmp/acs-cpp-deb
mkdir -p "$PKG/DEBIAN" "$PKG/usr/bin" "$PKG/usr/share/applications" \
  "$PKG/usr/share/pixmaps" "$PKG/usr/share/icons/hicolor/64x64/apps" \
  "$PKG/usr/share/doc/acs-cpp"
install -m 0755 "$ROOT/bin/ACS-cpp" "$PKG/usr/bin/ACS-cpp"
install -m 0644 "$ROOT/acs-cpp.png" "$PKG/usr/share/pixmaps/acs-cpp.png"
install -m 0644 "$ROOT/acs-cpp.png" "$PKG/usr/share/icons/hicolor/64x64/apps/acs-cpp.png"
cat > "$PKG/usr/share/applications/acs-cpp.desktop" << 'EOF'
[Desktop Entry]
Name=ACS
GenericName=Air Crack Station
Comment=ACS C++ desktop app. Made by Pakun & iinze0.
Exec=pkexec /usr/bin/ACS-cpp
Icon=acs-cpp
Terminal=false
Type=Application
Categories=System;Security;Network;
Keywords=acs;kali;aircrack;pentest;pakun;iinze0;
EOF
cat > "$PKG/usr/share/doc/acs-cpp/copyright" << EOF
ACS C++ — Air Crack Station
Made by Pakun & iinze0
https://github.com/iinze0/ACS-cpp
EOF
sed "s/^Version:.*/Version: ${VER}/" "$ROOT/packaging/DEBIAN/control" > "$PKG/DEBIAN/control"
install -m 0755 "$ROOT/packaging/DEBIAN/postinst" "$PKG/DEBIAN/postinst"
install -m 0755 "$ROOT/packaging/DEBIAN/prerm" "$PKG/DEBIAN/prerm"
install -m 0755 "$ROOT/packaging/DEBIAN/postrm" "$PKG/DEBIAN/postrm"
SIZE="$(du -sk "$PKG" | awk '{print $1}')"
sed -i "s/^Installed-Size:.*/Installed-Size: ${SIZE}/" "$PKG/DEBIAN/control"
mkdir -p "$ROOT/apt"
dpkg-deb --root-owner-group --build "$PKG" "$ROOT/apt/acs-cpp_${VER}_amd64.deb"
cp -f "$ROOT/apt/acs-cpp_${VER}_amd64.deb" "$ROOT/acs-cpp_${VER}_amd64.deb"
echo "built $ROOT/apt/acs-cpp_${VER}_amd64.deb"
