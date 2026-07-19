# ScienceEarth G0 v2 macOS 审计策略

## 目的

G0 v2 继续以 `v0.2.0` / `ScienceEarth` 为不可变基线，同时把经过严格验证的科学数据与可执行运行时代码分账。放宽的是科学数据的独立容量，不是运行时代码、依赖隔离或发布质量门槛。

## 四项独立体积门槛

| 项目 | PASS | REVIEW_REQUIRED | STOP |
| --- | ---: | ---: | ---: |
| 运行时增量 | `<= 40 MiB` | `> 40 MiB` 且 `<= 60 MiB` | `> 60 MiB` |
| 已验证科学数据 | `<= 128 MiB` | `> 128 MiB` 且 `<= 160 MiB` | `> 160 MiB` |
| 包体总增量 | `<= 160 MiB` | `> 160 MiB` 且 `<= 192 MiB` | `> 192 MiB` |
| 科学插件闭包 | `<= 40 MiB` | `> 40 MiB` 且 `<= 60 MiB` | `> 60 MiB` |

四项门槛独立判断。任一项进入 `STOP`，体积结论就是 `STOP`；没有 `STOP` 但至少一项需要复核时，结论是 `REVIEW_REQUIRED`。

## 科学数据的认定条件

只有位于 `Contents/misc/science/data-manifest.json` 清单中的文件才可能从运行时增量中分离。每个条目必须同时通过以下验证：

- 清单记录精确相对路径、字节数、SHA-256、数据角色和来源标识；
- 文件位于 `Contents/misc/science/` 内；
- 文件是普通文件，不是符号链接；
- 文件不是 Mach-O、动态库、插件或可执行文件；
- 清单使用规范化 JSON 编码，不能包含未知字段或重复路径；
- 实际路径、字节数和 SHA-256 与清单完全一致。

无效、未声明或无法验证的数据按运行时增量计算，不获得科学数据预算。

G0 v2 does not raise the runtime or science-closure budget. It separates scientific data from executable code only after exact path, size, SHA-256, role, source-id, regular-file, non-symlink, non-Mach-O, and package-boundary validation. Invalid or undeclared data remains in runtime delta.

## 不可豁免项

体积分账不能降低或绕过以下失败：缺失科学插件、GDAL/PROJ/ZSTD 静态或动态隔离失败、未解析依赖、包外依赖、编译路径泄漏、导出符号超界、签名、来源、Science-off 回归或不可变基线校验失败。

`packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json` 及其批准 SHA-256 始终不可改写；任何新增负债必须由当前候选承担，不能写回历史基线。
