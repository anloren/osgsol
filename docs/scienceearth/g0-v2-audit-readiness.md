# ScienceEarth G0 v2 审计准备状态

记录日期：2026-07-19

## 结论

G0 v2 审计工具和体积分账已经可用，当前 v0.6.0 候选的四项体积门槛全部 `PASS`，因此包大小不再是当前 G0 的阻塞项。

当前整体结论仍是 `STOP`。这不是功能回退，也不是数据过大，而是科学运行时尚未形成批准的插件隔离边界：主程序仍静态包含 GDAL、PROJ、ZSTD，包内不存在 `osgdb_science.so`，并且三个运行时组件仍携带 7 条新的本机安装前缀字符串。

特别注意：本次报告中的 Tier A 为 `PASS`，原因是科学插件闭包为空。它不能解释为科学隔离已经完成；“缺失科学插件”由 Tier B 新增 finding 明确拦截。

## 审计对象与来源

- 审计工具提交：`26b58d0a122874aa1027c04934947dcb79798851`
- App 可执行内容来源提交：`8951179be76abf21f85c7bcb07fbce188fc72710`
- App 版本：`0.6.0`
- 构建频道：`g0-v2-audit`
- staging 候选：`build/science_g0_v2_audit/candidate/osgSol Earth.app`
- 候选 bundle fingerprint：`eb1228764ee294e46d3e0a80503faca4b7b349b55c00b965ea05ef7187ab1b8c`
- 主程序 SHA-256：`913b731695afbe8e5457fb0927092371b7481e5d6dff3df4ff66ed782b3d6021`
- AlphaEarth index SHA-256：`15875963d1bf4dd3f35a1f6c3ec6329378fef0fab6549fac677552f1d348f736`
- 数据清单 SHA-256：`d026f04aafdc218380798cbe84bf390d179b483aff1cfc41b0a68ce8ad11d957`

候选只生成在专用 build 目录。`/Users/USER/Desktop/osgSol Earth.app` 未启动、未覆盖、未重新签名，macOS 设置未修改，也没有创建本地 listener。

## 不可变基线证明

- v0.2 baseline：`build/desktop-backup-pre-scienceearth/osgSol Earth.app`
- 批准 bundle fingerprint：`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`
- reference 文件 SHA-256：`afd80d8ad9419882793926d781affb970246358fbf74c514b950bf0eb924a5a6`
- reference 规范内容哈希：`145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada`
- ratchet 文件 SHA-256：`a732bacfec5b16b6e42ef4b4573827df6edfa73a5d8bf5b2c7b94d0d684c06cc`

批准 reference 和 ratchet 文件没有修改或重写。

## 体积结果

| 门槛 | 实测 | 结论 |
| --- | ---: | --- |
| 运行时增量 | 33,635,943 bytes（约 32.08 MiB） | PASS，`<= 40 MiB` |
| 已验证科学数据 | 87,183,360 bytes（约 83.14 MiB） | PASS，`<= 128 MiB` |
| 包体总增量 | 120,819,303 bytes（约 115.22 MiB） | PASS，`<= 160 MiB` |
| 科学插件闭包 | 0 bytes | PASS，但插件缺失，不能视为隔离成功 |

基线为 542,594,200 bytes，候选总大小为 663,413,503 bytes。科学数据只有在包内规范清单、实际字节数、SHA-256、来源、角色、普通文件、非符号链接、非 Mach-O 和 `Info.plist` 清单哈希全部一致时，才从运行时增量中分离。失效或未声明数据仍全部计入运行时。

## Finding 与根因

完整 JSON 保留 2,332 条当前 finding，没有截断原始证据。相对批准基线：

- 新增：1,955 条；
- 已移除：709 条；
- 未解析依赖：0；
- Tier B：`STOP`。

本次真正新增负债可归为三个根因：

1. `science-static-linkage`：1,947 条，全部位于 `Contents/MacOS/osgSol_Earth`，其中 1,928 条静态符号、19 条静态科学字符串。
2. `forbidden-compiled-paths`：7 条，分别影响主程序、`libfontconfig.1.dylib` 和 `libintl.8.dylib`。
3. `missing-science-plugin`：1 条；候选中没有 `osgdb_science.so`。

7 条新增本机前缀证据包括：

- 主程序中的 `/usr/local/lib/gdalplugins`；
- fontconfig 中的 `/opt/homebrew/etc/fonts`、`/opt/homebrew/var/cache/fontconfig`、`/opt/homebrew/Cellar/fontconfig/2.18.1/share/fontconfig/conf.avail`、`/usr/local/share` 和一段内嵌 fontconfig XML；
- gettext/intl 中的 `/opt/homebrew/Cellar/gettext/1.0/share/locale`。

报告中 84 条全部 `forbidden_string` 包含批准基线已有的历史证据；决策时应以 7 条 delta 新增为本次必须清理范围，不能把两者混为一谈。

## 后续必须完成

G0 还不能关闭，必须按以下顺序继续：

1. 建立唯一 `osgdb_science.so` 插件，把 GDAL、PROJ、ZSTD 及 SciencePreview 的运行时实现移出主程序；插件只允许导出 `_osgsol_science_g0_probe_anchor`。
2. 清除 GDAL 编译进主程序的 `/usr/local/lib/gdalplugins` 回退路径，改为包内或显式运行时路径。
3. 使 fontconfig/gettext 运行时资源可重定位，移除上述 Homebrew 和 `/usr/local` 编译前缀。
4. 重新构建 Science-on 与 Science-off，运行全部离线测试、正式打包契约和 G0 v2 审计。
5. 最终门槛必须同时满足：恰好一个科学插件、主程序不再出现静态科学栈、0 个新增 forbidden path、0 个未解析/包外依赖、插件闭包不超过 60 MiB、四项体积不进入 STOP、签名和来源验证通过。

这些隔离修改必须保持已完成的 AlphaEarth、Sentinel-2、Copernicus DEM、64 维分析、AI research/跨数据源联动和普通地球功能不变。不能通过关闭科学能力、删除数据或改写 v0.2 基线来换取审计通过。

## 后续可独立优化但不阻塞 G0

DEM 的视觉对比度属于数据显示体验，不属于本次打包隔离审计。当前绝对高程色带适合全球统一比较，但在东京等低起伏区域会显得颜色浅。G0 关闭后可在不改原始高程值的前提下增加：局部百分位拉伸、hillshade 加分层设色，并明确显示当前模式和图例。

## 验证记录

- Python 审计与基线测试：75/75，约 5.8 秒；
- CTest：`offline` 50/50，明确排除 `network-local`，17.46 秒；
- macOS 正式临时候选打包：通过，约 30 秒；打包契约此前也已通过并跳过运行时 smoke；
- G0 v2 静态审计：约 16.28 秒，按预期返回 `STOP`；
- JSON：`build/science_g0_v2_audit/v0.6.json`，SHA-256 `57962c62cc550ff20363156ca377c892a79c351758255ee9a36c653c763ecfde`；
- 文本：`build/science_g0_v2_audit/v0.6.txt`，SHA-256 `836d5326922ad67e409b7c5b678a5b5dc03249dd270c80e3f795cf68743312df`。

核心命令：

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -q

ctest --test-dir build/science_g3_release \
  -L offline -LE network-local --output-on-failure

env -u EARTH_AI_KEY PYTHONDONTWRITEBYTECODE=1 \
  OSGVERSE_SDK="$PWD/build/science_g3_release/sdk" \
  OSG_RUNTIME_SDK="/Users/USER/osgverse/build/osg_core" \
  OSGSOL_PACKAGE_OUTPUT="$PWD/build/science_g0_v2_audit/candidate/osgSol Earth.app" \
  OSGSOL_PACKAGE_VERSION="0.6.0" \
  OSGSOL_BUILD_CHANNEL="g0-v2-audit" \
  OSGSOL_SOURCE_COMMIT="8951179be76abf21f85c7bcb07fbce188fc72710" \
  OSGSOL_ALPHAEARTH_INDEX="$PWD/build/science-index-full/alphaearth.sqlite" \
  bash packaging/package_macos.sh

PYTHONDONTWRITEBYTECODE=1 python3 packaging/audit_macos_bundle.py \
  --app 'build/science_g0_v2_audit/candidate/osgSol Earth.app' \
  --baseline 'build/desktop-backup-pre-scienceearth/osgSol Earth.app' \
  --reference-manifest \
    packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json \
  --ratchet-manifest \
    packaging/scienceearth/baselines/current-macos-arm64-ratchet.json \
  --source-root /Users/USER/osgverse \
  --source-root /Users/USER/osgsol/.worktrees/v0.2-runtime-safety \
  --json build/science_g0_v2_audit/v0.6.json \
  --text build/science_g0_v2_audit/v0.6.txt
```
