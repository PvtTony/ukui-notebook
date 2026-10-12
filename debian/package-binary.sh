#!/bin/bash
# Run on the Kylin build host. No root or debhelper installation is required.
set -euo pipefail

source_dir=$(cd "$(dirname "$0")/.." && pwd)
output_dir=${1:-"$source_dir/build/package"}
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)
build_dir="$output_dir/cmake"
package_root=$(mktemp -d "$output_dir/package-root.XXXXXX")
trap 'rm -rf -- "$package_root"' EXIT

cd "$source_dir"
version=$(dpkg-parsechangelog -S Version)
architecture=$(dpkg --print-architecture)
filename="ukui-notebook_${version}_${architecture}.deb"

# CMake does not check the exit status of its translation commands.
bash sh/update_translations.sh
bash sh/apply_translations.sh
for translation in translations/ukui-notebook/*.ts; do
    lrelease "$translation" -qm "${translation%.ts}.qm"
done

cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTEST_DEBUG=OFF
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"
DESTDIR="$package_root" cmake --install "$build_dir"
test -s "$package_root/usr/share/ukui-notebook/translations/ukui-notebook_zh_CN.qm"

strip --strip-unneeded "$package_root/usr/bin/ukui-notebook" \
    "$package_root/usr/bin/ukui-notebook-server"
install -d "$package_root/DEBIAN" "$package_root/usr/share/doc/ukui-notebook"
install -m 644 debian/copyright "$package_root/usr/share/doc/ukui-notebook/copyright"
gzip -n -9 -c debian/changelog > "$package_root/usr/share/doc/ukui-notebook/changelog.Debian.gz"
for script in preinst postinst postrm; do
    install -m 755 "debian/$script" "$package_root/DEBIAN/$script"
done
printf '%s\n' /etc/dbus-1/system.d/org.ukui.note.conf > "$package_root/DEBIAN/conffiles"

# Resolve library package names and minimum versions from the actual ELF files.
dpkg-shlibdeps -O -e "$package_root/usr/bin/ukui-notebook" \
    -e "$package_root/usr/bin/ukui-notebook-server" > "$output_dir/substvars"
printf '%s\n' 'misc:Depends=' >> "$output_dir/substvars"
dpkg-gencontrol -pukui-notebook -P"$package_root" \
    -T"$output_dir/substvars" -f"$output_dir/files"

(
    cd "$package_root"
    find etc usr -type f -print0 | LC_ALL=C sort -z | xargs -0 md5sum > DEBIAN/md5sums
)
dpkg-deb --root-owner-group --build "$package_root" "$output_dir/$filename"
dpkg-deb --info "$output_dir/$filename" > "$output_dir/package-info.txt"
dpkg-deb --contents "$output_dir/$filename" > "$output_dir/package-contents.txt"
(
    cd "$output_dir"
    sha256sum "$filename" > "$filename.sha256"
)
printf 'PACKAGE=%s\n' "$output_dir/$filename"
