#!/bin/bash
# Collect a complete dependency closure without installing on the build host.
set -euo pipefail

source_dir=$(cd "$(dirname "$0")/.." && pwd)
cd "$source_dir"
version=$(dpkg-parsechangelog -S Version)
architecture=$(dpkg --print-architecture)
application=${1:-"$source_dir/build/package/ukui-notebook_${version}_${architecture}.deb"}
application=$(realpath "$application")
version=$(dpkg-deb -f "$application" Version)
architecture=$(dpkg-deb -f "$application" Architecture)
output_dir=${2:-"$source_dir/build/package"}
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)
staging=$(mktemp -d "$output_dir/offline-root.XXXXXX")
trap 'rm -rf -- "$staging"' EXIT
name="ukui-notebook-offline_${version}_${architecture}"
bundle="$staging/$name"
mkdir -p "$bundle/packages/partial" "$staging/log"
touch "$staging/empty-status"
cp "$application" "$bundle/packages/"

# An empty status file prevents dependencies installed on the builder from
# being omitted. Download-only also avoids configuring this artificial system.
apt-get --download-only --yes --no-install-recommends \
    -o Debug::NoLocking=true \
    -o "Dir::State::status=$staging/empty-status" \
    -o "Dir::State::extended_states=$staging/extended_states" \
    -o "Dir::Cache::archives=$bundle/packages" \
    -o "Dir::Log=$staging/log" install "$application"
rmdir "$bundle/packages/partial"
install -m 755 debian/install-offline.sh "$bundle/install-offline.sh"
(
    cd "$bundle"
    dpkg-scanpackages packages /dev/null > Packages
    gzip -n -9 -c Packages > Packages.gz
    for package in packages/*.deb; do
        dpkg-deb -W --showformat='${Package}\t${Version}\t${Architecture}\n' "$package"
    done > package-versions.tsv
)
cat > "$bundle/README.md" <<EOF
# ukui-notebook 离线升级包

版本：$version；架构：$architecture。
适用环境：麒麟 V10 国防／集采版（V10-JICAI）、arm64。

1. 将整个压缩包复制到目标机，解压到不含空格的路径。
2. 完全退出便签程序。建议先备份当前用户的 ~/.config/kylin-note/。
3. 进入解压目录，执行 sudo bash install-offline.sh --simulate 检查安装计划。
4. 执行 sudo bash install-offline.sh，确认 apt 显示的安装计划。
5. 执行 dpkg-query -W ukui-notebook 核对版本，然后重新启动便签。

安装脚本仅使用本目录的 file: 软件源，并禁止下载和删除软件包。
它会保留版本已满足要求的系统依赖，只安装缺失或版本不足的包。
依赖集合按空包数据库解析，所以可能含有目标机已经具备的基础包；
请使用安装脚本，不要对 packages/*.deb 执行一次性 dpkg -i。

新应用包包含两个 Release 程序、9 个应用翻译文件、桌面入口、
D-Bus 文件和用户手册。升级脚本保护旧 postrm 可能误删的
Clock_database.db 和 url.conf，应用数据库路径与格式沿用现有实现。
全部软件包的版本见 package-versions.tsv，校验值见 SHA256SUMS。
此包未在实际目标机运行；目标机有其他版本依赖冲突时安装会停止。
EOF
(
    cd "$bundle"
    find . -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS
)
tar -C "$staging" -czf "$output_dir/$name.tar.gz" "$name"
(
    cd "$output_dir"
    sha256sum "$name.tar.gz" > "$name.tar.gz.sha256"
)
printf 'OFFLINE_BUNDLE=%s\n' "$output_dir/$name.tar.gz"
