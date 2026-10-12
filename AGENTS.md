# 仓库工作指南

本文件适用于整个仓库。修改前先阅读相关源码和构建配置；若子目录以后增加 `AGENTS.md`，同时遵循其局部约定。默认使用中文说明变更、验证结果和环境限制。

## 项目定位与技术栈

- `ukui-notebook` 是麒麟/UKUI Linux 桌面便签应用，提供列表/图标视图、搜索、多便签窗口、富文本编辑、颜色设置和自动保存。
- 使用 C++11、Qt Widgets、CMake、SQLite、Qt D-Bus，以及麒麟 SDK、GSettings 和 KF5 WindowSystem；通过 Debian 打包发布。
- 以 Qt 5 为当前兼容基线：`debian/control` 和 `debian/rules` 指向 Qt 5，公共 CMake 函数仍引用 `Qt5::Widgets`。配置中出现 Qt 6 查找逻辑不等于已完成 Qt 6 支持。
- 有两个可执行目标：GUI `ukui-notebook` 和会话总线服务 `ukui-notebook-server`，以及静态库 `common`。

## 代码导航

| 路径 | 职责 |
| --- | --- |
| `CMakeLists.txt`、`cmake/common.cmake` | 子项目组织和 SDK、GSettings、KF5 等公共链接逻辑 |
| `src/main.cpp`、`src/utils/` | 应用入口、单实例处理、命令行选项 |
| `src/widget.*` | 主窗口、搜索/排序、便签窗口同步、设置、数据库线程及旧数据迁移 |
| `src/noteData.*`、`src/noteModel.*`、`src/noteView.*` | 便签数据、列表模型、自定义视图；列表/图标 delegate 位于同目录 |
| `src/dbManager.*` | SQLite 表结构及新增、更新、删除、导入/导出、迁移操作 |
| `src/editPage.*`、`src/noteHead*`、字体/调色板相关文件 | 编辑窗口、富文本操作、窗口菜单和外观设置 |
| `src/common/` | 日志、公共工具、UKUI/Wayland 窗口管理封装 |
| `src/*.ui`、`src/*.qrc`、`src/image/` | Qt Designer 界面、资源清单和图片 |
| `uNoteDbusServer/`、`data/org.ukui.note.*` | 搜索服务实现、D-Bus 接口契约及激活/策略文件 |
| `translations/ukui-notebook/`、`sh/`、`po/POTFILES.in` | Qt 翻译源文件、翻译脚本和桌面入口的翻译清单 |
| `data/ukui-notebook/`、`man/`、`debian/` | 中英文用户手册、命令手册、安装与打包元数据 |

## 构建与环境

本机仅用于源码分析、编辑和同步；不在本机执行项目编译、测试或应用。编译、测试及执行统一通过 SSH 在 `yst@192.168.65.9:~/ukui-notebook` 进行。

目标环境为麒麟 V10，架构为 aarch64，使用 `apt` 管理依赖；内核信息为 `Linux yst-kylin 5.4.18-77.66.jc-generic #02 SMP Mon Sep 18 15:58:15 CST 2023 aarch64 aarch64 aarch64 GNU/Linux`。以实际 SSH 检查结果判断依赖和桌面会话是否可用。

已验证的工具链为 GCC 9.3.0、CMake 3.16.3、Qt 5.12.8，GUI 与服务均可编译为 aarch64 可执行文件。

依赖以 `debian/control`、各级 `CMakeLists.txt` 和 `cmake/common.cmake` 为准：

- C/C++ 编译器、CMake、pkg-config/pkgconf；CMake 文件声明最低版本 3.5，但使用的 `target_link_directories` 要求实际版本至少 3.13。
- Qt 5 开发库与工具：Widgets、LinguistTools、Network、DBus、Sql、Concurrent，`qdbusxml2cpp`、`lupdate`、`lrelease`、`lconvert`；运行时需要 QSQLITE 驱动。
- `gsettings-qt`、KF5 WindowSystem、GLib，以及 `kysdk-qtwidgets`、`kysdk-log`、`kysdk-config`、`kysdk-utils`、`kysdk-waylandhelper`、`kysdk-alm`。项目已移除 Telemetry 调用和 diagnostics 专用构建依赖，保留其他 SDK 功能。
- 翻译脚本还需要 Bash 4+（使用关联数组）、`realpath`、gettext、intltool。
- GUI 验证需要桌面会话、会话 D-Bus，以及对应的 UKUI 主题、图标和 GSettings schema。

同步前检查远端目录，保留远端已有的工作和测试数据；源码从本机同步到远端，不回传构建产物。首次完整同步可在本机仓库根目录执行（远端需有 rsync）：

```sh
rsync -az --exclude='.git/' --exclude='build/' --exclude='build-*/' \
  --exclude='*.qm' --exclude='*.desktop' --exclude='note_adaptor.*' \
  --exclude='note_interface.*' --exclude='.vscode/' \
  ./ yst@192.168.65.9:~/ukui-notebook/
```

不要添加 `--delete`，除非明确需要删除远端文件并已确认其内容；源文件删除时应单独检查远端残留。仅同步相关源文件，不覆盖远端数据库、日志或构建目录。

通过 SSH 在远端进行目录外构建：

```sh
ssh yst@192.168.65.9 'cd ~/ukui-notebook && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build --parallel 2'
```

成功后可执行文件通常位于 `build/src/ukui-notebook` 和 `build/uNoteDbusServer/ukui-notebook-server`。只编译某个目标可加 `--target ukui-notebook` 或 `--target ukui-notebook-server`。

`-DTEST_DEBUG=ON` 将应用的 Qt 日志处理切换为控制台调试输出；它不是测试套件，也不会绕过 Qt 或 SDK 依赖。项目不再使用该宏控制埋点，也没有启用 Telemetry 的开关。

依赖缺失时在远端通过 `apt` 安装对应开发包，包名参考 `debian/control`；先检查软件源中的可用版本和安装计划。Debian 打包可在依赖齐全的远端使用 `dpkg-buildpackage -us -uc -b`，也可使用下述不依赖 debhelper 的打包脚本。安装规则使用绝对的 `/usr`、`/etc` 路径；验证安装布局时使用临时 `DESTDIR` 暂存目录，不要仅靠修改 `CMAKE_INSTALL_PREFIX`。

缺少依赖时，记录失败阶段、命令和实际错误。Qt 查找失败后可能落入旧版 `FindQt` 并显示 Qt3 错误；应先检查 Qt 5 配置和查找路径，不要把该报错视为项目需要 Qt3。不要为了让当前主机配置通过而移除 Linux/SDK 依赖。

## Release 安装包与离线交付

`debian/package-binary.sh` 在麒麟主机生成 Release `.deb`：编译两个程序、生成全部 `.ts` 对应的翻译、使用 `DESTDIR` 暂存安装文件，并用 `dpkg-shlibdeps` 和 `dpkg-gencontrol` 计算依赖及包元数据。需要已有构建依赖、dpkg-dev 和 binutils，不需要 sudo 或 debhelper；默认输出目录为 `build/package/`，也可传入其他输出目录。

```sh
ssh yst@192.168.65.9 'cd ~/ukui-notebook && bash debian/package-binary.sh'
ssh yst@192.168.65.9 'cd ~/ukui-notebook && bash debian/package-offline.sh'
```

第二条命令用临时空包数据库解析完整依赖，以仅下载方式收集 `.deb`，生成本地 APT 软件源、安装脚本、版本清单及 SHA256 校验值，输出 `ukui-notebook-offline_<版本>_<架构>.tar.gz`。它不会安装或升级编译机的软件包。依赖集合包含基础包，不能直接对全部 `.deb` 执行 `dpkg -i`，以免把已满足要求的目标机依赖强制覆盖为所收集的版本。

当前离线安装脚本针对麒麟 V10-JICAI、arm64；目标机必须核对发行版本和架构。解压后先完全退出便签，建议备份用户的 `~/.config/kylin-note/`，然后执行 `sudo bash install-offline.sh --simulate` 查看计划，再执行 `sudo bash install-offline.sh` 安装。脚本只使用本地 `file:` 软件源，并设置 `--no-download`、`--no-remove`，保留版本已经满足要求的系统依赖；其他发行版或有冲突的包状态需要重新验证。

版本以 `debian/changelog` 为准。当前本地发行版为 `3.2.0-0k9+local1`，可升级已安装的 `3.1.1-1`。这两次功能修改未引入额外库，但当前源码相对旧 `3.1.1-1` 二进制已经使用麒麟 SDK，不能据此假定旧包的运行依赖足够；以生成包的 `Depends` 为准。SQLite 驱动 `libqt5sql5-sqlite` 是原有运行要求，现在显式声明。

旧版 `postrm` 会在升级时误删 `/home/*/.config/ukui/Clock_database.db` 和 `url.conf`。当前 `preinst` 为旧版本备份这两类文件，`postinst` 恢复缺失文件，`postrm` 在升级失败回退时也恢复备份；保留文件属性及符号链接，不覆盖升级期间重新生成的文件。新的卸载和清除操作不删除用户便签数据。维护这些脚本时，在隔离根目录验证真实 dpkg 调用顺序、重试、失败恢复、配置文件保留和卸载；不要在真实用户目录模拟旧脚本的删除逻辑。

离线交付应验证包内容、翻译、依赖缓存完整性、断网依赖解析及隔离升级，再运行打包后的程序进行冒烟验证。明确区分依赖模拟与实际安装，不把编译机上的成功等同于另一台机器已验证。用户要求交付安装包时，可把 `.deb`、离线压缩包和校验文件回传到本机 `build/package/`，不要提交这些产物。

## 构建副作用与生成文件

- CMake 配置阶段会执行 `sh/update_translations.sh` 和 `sh/apply_translations.sh`，更新源码树中的 `.ts`、生成 `.qm` 和 `data/ukui-notebook.desktop`。即使采用目录外构建，源码树也可能发生变化。
- 服务配置阶段根据 `data/org.ukui.note.xml` 生成 `uNoteDbusServer/note_adaptor.*` 和 `note_interface.*`；接口变更应编辑 XML 与手写实现，再重新配置生成。
- 不手工编辑或提交 `ui_*.h`、`moc_*`、`qrc_*`、`.qm`、生成的 `.desktop`、D-Bus 生成代码、构建目录及本地 IDE 文件；现有 `.gitignore` 已包含相应规则。
- `.ui`、`.qrc`、`.desktop.in`、`.ts` 和 D-Bus XML 是需要维护的源文件。新增或改名资源时同步 `.qrc`，保留文件名大小写与空格。
- 源文件通过 `file(GLOB ...)` 收集；增加或删除源文件后重新运行 CMake 配置。
- 构建和翻译后检查 `git status --short`、`git diff`，区分预期修改和工具产生的翻译变化，只保留与任务有关的变更。

## 修改约定与兼容要求

- 遵循附近代码的命名、缩进、花括号、Qt 信号/槽及 QObject 生命周期方式，保持 C++11 兼容；不做无关的全仓格式化或类名重命名。
- 保留已有版权及 GPL 声明。部分源码（例如 `src/editPage.cpp`）使用 CRLF，编辑时保留原换行风格，避免整文件差异。
- UI 修改优先维护 `.ui` 和对应手写代码。可翻译文本使用 `tr()`/`QObject::tr()`，避免把用户可见文本硬编码进样式或逻辑。
- `Widget` 将 `DBManager` 移入独立 `QThread`，部分请求使用 `Qt::BlockingQueuedConnection`。修改数据库或退出逻辑时检查连接线程、阻塞关系、对象所有权与销毁次序；QSqlDatabase 连接应在所属线程使用。
- 列表通过 `QSortFilterProxyModel` 映射源模型。搜索、排序、新建/删除或编辑更新后，不要把代理行号当作稳定便签 ID；同时验证打开的编辑窗口与列表同步。
- `content` 保存编辑器 HTML，`md_content` 实际保存纯文本并用于搜索；不能因字段名而将其当作 Markdown。保留富文本、图片、纯文本摘要与标题之间的关系。
- 时间字段按毫秒时间戳读写。修改表结构或字段读取顺序时，同时检查 GUI、D-Bus 服务、序列化及迁移代码，提供已有数据的兼容方案。
- GUI 使用 `QSettings` 的 `kylin-note/Settings` 配置，并在其目录保存 `kyNotes.db`；服务硬编码读取 `~/.config/kylin-note/kyNotes.db`。变更路径时必须协调两者，单独设置 `XDG_CONFIG_HOME` 不能保证服务也读取同一数据库。
- 现有表为 `active_notes` 和 `deleted_notes`；旧数据涉及 `Notes.ini`、`Trash.ini` 迁移。数据相关验证使用专用测试用户或虚拟机内的样本，避免自动保存、删除和迁移操作影响真实便签。
- 新增或修改 SQL 查询优先使用 `prepare()` 与绑定参数，并检查查询错误、空输入、引号及 Unicode；不要扩展现有手工拼接用户输入的模式。
- D-Bus 公共契约：会话总线服务 `org.ukui.note`，对象 `/org/ukui/note`，接口 `org.ukui.note.interface`，方法 `keywordMatch(QStringList)` 返回 `QVariantMap`。接口、返回结构、激活文件及调用方应协调修改。
- 窗口操作同时涉及 `src/common/windowmanage.hpp` 和 `EditPage` 中的 KF5 调用。修改置顶、激活或缩放时检查 X11 与 Wayland，保留桌面入口的绝对 `Exec` 路径及 `X-KDE-Wayland-Interfaces` 声明。
- `debian/control` 的描述和仓库链接已修正为便签项目；对旧 `ukui-sidebar` 的冲突声明沿用现有兼容约定，修改前核对是否会造成目标机文件冲突或删除其他桌面组件。

## 翻译流程

### 语言判断与翻译加载

`Widget` 初始化时使用 `QLocale::system().name()` 获取语言名称，例如 `zh_CN`。目标机 Qt 5.12.8 实测使用的环境变量优先级为 `LANGUAGE → LC_ALL → LC_MESSAGES → LANG`；这是该目标环境的 Qt 行为，不应与其他程序的 locale 优先级混为一谈。通过 SSH 排查时检查启动程序的实际环境变量和 Qt 识别结果。

应用依次加载三类翻译：

- 应用界面：`/usr/share/ukui-notebook/translations/ukui-notebook_<locale>.qm`。
- Qt 标准控件/菜单：`QLibraryInfo::TranslationsPath` 中的 `qt_<locale>.qm`。
- SDK 控件：Qt 资源路径 `:/translations/gui_<locale>.qm`。

应用翻译加载路径目前固定为安装目录，不会自动查找源码或构建目录。远端源码目录中已经生成 `.qm`，不代表直接运行 `build/src/ukui-notebook` 就能加载这些文件。翻译加载失败时界面使用源码中的英文；先确认 Qt 识别到的语言，再确认相应翻译文件是否存在于加载目录，避免只修改 `LANG`。

需要让直接运行的构建产物加载现有翻译时，在麒麟主机执行以下命令（需要 sudo 权限）：

```sh
sudo install -d /usr/share/ukui-notebook/translations
sudo install -m 644 ~/ukui-notebook/translations/ukui-notebook/*.qm \
  /usr/share/ukui-notebook/translations/
```

修改语言环境或安装翻译后，完全退出已有应用再重新启动。翻译在窗口初始化时安装；由于单实例机制，再次执行程序可能只唤起原实例，不能据此验证新的语言环境。语言相关代码修改应同时验证应用、Qt 标准控件和 SDK 控件的翻译。

### 更新翻译源文件

需要更新翻译时，通过 SSH 在远端项目根目录运行：

```sh
ssh yst@192.168.65.9 'cd ~/ukui-notebook && bash sh/update_translations.sh && bash sh/apply_translations.sh'
```

`sh/common.sh` 当前处理 `en`、`zh_CN`、`bo_CN`、`kk`、`ky`、`ug`、`ru`；仓库还存在 `fr`、`tr` 的 `.ts`，不要假定脚本会覆盖所有文件。增加语言时同步脚本列表和对应翻译源文件。

`generate_translations.sh` 用于初始化流程；仓库已有 `po/POTFILES.in`，不要把它当作日常更新命令。脚本会清理临时 `.po`、`.pot`、`.ts` 等文件，运行前检查相关目录中的工作，运行后审阅翻译差异。CMake 的 `execute_process` 没有检查脚本结果，配置成功也不能证明翻译生成成功。

## 验证与交付

当前仓库没有已配置的自动化测试、CTest 目标或 CI；源码中的 `friend class tst_*` 和 `.gitignore` 的测试条目不代表存在可运行测试。不要把没有执行测试的 `ctest` 结果报告为测试通过。

通过 SSH 做 GUI 冒烟验证时，可使用 Xvfb、`dbus-run-session` 和临时 `XDG_CONFIG_HOME`；不要修改当前用户的 `HOME`。临时测试脚本、配置和日志放在远端 `build/` 中。仅验证 D-Bus 注册和接口签名不需要访问数据库；测试 `keywordMatch` 的实际查询时，先处理服务固定数据库路径的隔离问题。保留退出状态和测试结果，清理本次创建的进程，区分虚拟会话中的外部服务告警与应用功能失败。

按改动范围完成验证，不要求为文档或简单外观变更额外建立测试框架：

- 所有改动：检查差异与 `git diff --check`；确认没有无关生成文件或格式变化。
- C++/CMake：通过 SSH 在指定麒麟主机配置并编译受影响目标；公共依赖变更检查两个可执行目标。所有应用启动和功能测试也在该主机执行。
- 编辑/数据：新建、输入中英文与引号、修改格式和颜色、关闭/重开及重启后持久化；检查立即保存、排序和多窗口同步。图片相关改动覆盖仅含图片及删除最后一张图片的便签。
- 列表/搜索/删除：检查列表与图标模式、过滤后的编辑和删除、选中项变化、关闭空便签、清空后的状态。
- 系统集成：按范围检查主题/字体变化、高 DPI、单实例唤起、置顶及 X11/Wayland 行为。
- D-Bus：在专用测试会话验证服务注册、接口签名及关键字查询，覆盖无结果、多个关键字、空输入、Unicode 和引号。
- 翻译/安装：检查 `.qm`、桌面入口、服务文件和用户手册的生成及安装位置；应用从 `/usr/share/ukui-notebook/translations` 加载翻译，直接运行构建产物不保证加载本次生成内容。

交付时简要说明修改目的、涉及文件、实际执行的验证及未验证项。如果环境缺少 Qt/SDK 或桌面会话，明确说明限制，不把静态检查等同于编译或运行验证。需要提交时可沿用仓库已有的 `fix(widget): ...`、`feat(wayland): ...`、`build(debian): ...` 风格，提交范围聚焦当前任务。
