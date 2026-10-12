#!/bin/bash
# Run from the extracted bundle on Kylin V10-JICAI arm64.
set -euo pipefail

if (( EUID != 0 )); then
    echo '请使用 sudo bash install-offline.sh 执行安装。' >&2
    exit 1
fi
mode=${1:-}
if [[ -n "$mode" && "$mode" != --simulate ]]; then
    echo '用法：sudo bash install-offline.sh [--simulate]' >&2
    exit 1
fi

bundle_dir=$(cd "$(dirname "$0")" && pwd)
if [[ "$bundle_dir" =~ [[:space:]] ]]; then
    echo '请先将安装目录移到不含空格或换行的路径。' >&2
    exit 1
fi
cd "$bundle_dir"
sha256sum --check SHA256SUMS

applications=(packages/ukui-notebook_*.deb)
[[ ${#applications[@]} == 1 && -f "${applications[0]}" ]]
version=$(dpkg-deb -f "${applications[0]}" Version)
architecture=$(dpkg-deb -f "${applications[0]}" Architecture)
[[ "$(dpkg --print-architecture)" == "$architecture" ]]
. /etc/os-release
if [[ "${ID:-}" != kylin || "${KYLIN_RELEASE_ID:-}" != V10-JICAI ]]; then
    echo '该离线包仅针对麒麟 V10-JICAI；请勿用于其他发行版本。' >&2
    exit 1
fi
current=$(dpkg-query -W -f='${Version}' ukui-notebook 2>/dev/null || true)
if [[ -n "$current" ]] && dpkg --compare-versions "$current" gt "$version"; then
    echo "已安装版本 $current 高于离线包 $version，停止降级。" >&2
    exit 1
fi

apt_dir=$(mktemp -d /tmp/ukui-notebook-offline.XXXXXX)
trap 'rm -rf -- "$apt_dir"' EXIT
chmod 755 "$apt_dir"
mkdir -p "$apt_dir/lists/partial" "$apt_dir/archives/partial"
for package in "$bundle_dir"/packages/*.deb; do
    ln -s "$package" "$apt_dir/archives/$(basename "$package")"
done
printf 'deb [trusted=yes] file:%s ./\n' "$bundle_dir" > "$apt_dir/sources.list"
options=(-o "Dir::Etc::sourcelist=$apt_dir/sources.list"
         -o Dir::Etc::sourceparts=-
         -o "Dir::State::lists=$apt_dir/lists"
         -o "Dir::Cache::archives=$apt_dir/archives"
         -o APT::Install-Recommends=false)

# Only the local file repository is visible; no configured network source is used.
apt-get "${options[@]}" update
arguments=(--no-download --no-remove)
[[ "$mode" != --simulate ]] || arguments+=(--simulate)
apt-get "${options[@]}" "${arguments[@]}" install "ukui-notebook=$version"
