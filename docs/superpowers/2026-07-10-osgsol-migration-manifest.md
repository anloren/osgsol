# osgsol 初始迁移清单 — 2026-07-10

## 迁移结果

- 新本地根目录：`/Users/USER/osgsol`
- 新 GitHub 目标：`https://github.com/anloren/osgsol`
- 可见性：private。原因是初始快照包含尚未修复、此前未发布的安全审查证据；修复后可再显式改为 public。
- Git 分支：`master`
- 历史策略：全新 Git 历史，不复制老目录的 `.git`；本文件所在提交即新仓库的初始快照提交。
- 产品命名策略：只更换仓库/目录名，不在本次机械迁移中重命名 CMake target、namespace、bundle identifier 或 `osgVerse` 产品文案。

## 源快照

- 源目录：`/Users/USER/osgverse`
- 源分支：`feat/v023-quality-batch`
- 源提交：`223f8d42c74718d23549f8e0fbe2cea9096a4598`
- 老远端：`https://github.com/anloren/osgverse.git`
- 老远端公开基线：`origin/master = 2c647d12`
- 源工作树在复制前为 clean；`223f8d42` 相对 `master` 只新增三份交接资料：60 条审查报告、原始 JSON、v0.23 两波设计。

## 复制边界与一致性

- 使用归档语义复制整个工作目录，只排除源 `.git/`。
- 本地保留被 `.gitignore` 排除的 `build/`、`dist/`、缓存、日志和 `.superpowers/`，以便延续本机开发与取证。
- 排除 `.git` 后，源与目标均为 23,964 个普通文件；`rsync -ani --delete --exclude='.git/'` 无差异。
- 目标目录约 4.7 GiB；源目录约 5.2 GiB，差额为约 502 MiB 的老 Git 数据。
- 老仓库 4,477 个 tracked path 的 mode 与 blob id 已逐项对比，新仓库初始索引完全一致。
- 三个“已跟踪但现规则会忽略”的文件使用显式强制加入以保持快照完整：
  - `android/app/src/main/jniLibs/arm64-v8a/libomp.so`
  - `helpers/toolchain_builder/jpeg/change.log`
  - `wasm/sed.exe`
- 新增且仅新增本次 osgsol 审查与迁移文档；构建产物仍不进入 Git。

## 新路径开发基线

- 复制来的 `build/verse_core` 缓存仍记录老绝对路径，作为历史构建产物保留，不再作为新项目的规范构建目录。
- 新规范构建目录：`/Users/USER/osgsol/build/osgsol_core`
- 安装目录：`/Users/USER/osgsol/build/sdk_core`
- 新 CMake cache 已确认：
  - `CMAKE_HOME_DIRECTORY=/Users/USER/osgsol`
  - `osgVerse_SOURCE_DIR=/Users/USER/osgsol`
  - `CMAKE_INSTALL_PREFIX=/Users/USER/osgsol/build/sdk_core`
- 配置命令：

  ```bash
  cmake -S /Users/USER/osgsol \
        -B /Users/USER/osgsol/build/osgsol_core \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/Users/USER/osgsol/build/sdk_core \
        -DOSG_ROOT=/Users/USER/osgsol/build/sdk_core
  ```

- 构建命令：

  ```bash
  cmake --build /Users/USER/osgsol/build/osgsol_core --target install -j4
  ```

## 迁移时验证

- Release 全量 `install` 构建：exit 0。
- 六个离线 Earth 逻辑测试均从新路径运行并通过：`Feeds`、`Ai_Chat`、`Ais`、`Satellite`、`TileOverlay`、`WorldTools`。
- 在显式移除 `EARTH_AI_KEY` 的环境中重新运行 `packaging/package_macos.sh`：exit 0。
- 新打包的 `dist/EarthExplorer.app`：`codesign --verify --deep --strict` exit 0；plist 无嵌入 key，包内无 `imgui.ini`。
- tracked 源码的常见私钥/API token 模式扫描：0 个候选。
- 已知限制：CMake 当前生成 42 个测试/示例二进制，但 CTest 注册数为 0；这不是迁移失败，而是完整复审记录的项目级 finding。

## 后续工作边界

- `docs/superpowers/2026-07-10-master-v022-full-review.md` 的 60 条 confirmed finding 仍是 v0.23 两波设计的 Earth Explorer 基线。
- osgsol 全项目扩展复审增加了引擎核心、通用 I/O/插件、构建/交付层 findings；详见同目录新的全项目报告和 `docs/superpowers/research/` 下的新原始 JSON。
- 本次任务只审查、迁移、构建、验证和发布快照；没有擅自修复 findings，也没有改产品行为。
