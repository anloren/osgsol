# ScienceEarth G0 插件隔离完成记录

记录日期：2026-07-19

## 当前结论

ScienceEarth G0 的代码隔离、正式 staging 打包和 v2 静态审计已经完成。候选包总结果为
`PASS`，Tier A 与 Tier B 均为 `PASS`，当前 finding、violation、未解析依赖和根因均为
0。四项体积门槛全部为 `PASS`。

这次工作只生成了构建目录中的隔离候选包：

`build/science_g0_plugin_audit/candidate/osgSol Earth.app`

没有启动、覆盖或重新签名 `/Users/USER/Desktop/osgSol Earth.app`，没有修改 macOS
设置，没有创建本地 listener，也没有运行 `network-local` 测试。

“G0 静态审计通过”不等于“运行时人工验收已经完成”。当前候选已经到达可由人手测的
状态；视觉、交互、拍照视角和正常退出仍需用户手动验证，见文末清单。

## 隔离边界

主程序现在只保留一个小型 `SciencePluginRuntime` 门面。完整科学对象图全部位于唯一的
`osgdb_science.so` 中，包括：

- AlphaEarth、Sentinel-2 和 Copernicus DEM provider；
- `ScienceQueryService`、预览图层、左右科学面板；
- 64 维分析、证据与 provenance 语义；
- 科学 AI research 工具及其跨数据源联动入口。

插件只有一个版本化 C ABI 锚点：`_osgsol_science_g0_probe_anchor`。当前 ABI v2 除原有
session 函数外，还要求主程序在每次科学面板绘制前传入当前 ImGui context 和对应的内存
分配器。主程序不再直接链接
GDAL、PROJ、ZSTD 或 SciencePreview 的科学实现。插件 session 销毁时会释放服务和工作
线程，但模块句柄有意保持到进程退出，避免 OSG 延迟销毁的节点或回调跳入已经卸载的代码。

缺失插件、缺失锚点、ABI 不兼容或 session 创建失败时，门面会 fail closed：只隐藏
ScienceEarth 并记录一次明确警告，普通地球、相机、地图、图层、照片和 Quit 不依赖科学
插件。`OSGSOL_BUILD_SCIENCE=OFF` 的反向构建中没有生产 `osgdb_science.so`。

## 私有科学依赖与包内路径

GDAL 3.13.1、PROJ 9.8.1 和 ZSTD 1.5.7 使用仓库脚本固定归档、校验 SHA-256 并构建到
独立静态前缀。GDAL 的动态驱动自动加载关闭，但产品批准的 GTiff、VRT、MEM、
`/vsicurl/`、ZSTD、warp 和 embedded PROJ 能力保留。

fontconfig 使用包内确定性配置，同时继续尊重用户已经设置的 `FONTCONFIG_FILE`。打包过程
只对 disposable staging 副本执行严格白名单、等长的运行时前缀替换。编译进 Mach-O 的
两个源码根路径也在签名前替换为等长稳定 token；源文件、运行时 SDK、批准基线和桌面 App
都不会被修改。所有 staging Mach-O 在变更前统一去掉上游临时签名，最后再按既有身份签名
并执行 `codesign --verify --deep --strict`。

## 候选与审计证据

- 候选版本：`0.6.0`
- 构建频道：`g0-plugin-audit`
- App 内容来源提交：`4252e75ec817ca820c628f97a4404728b7a12758`
- 候选 bundle fingerprint：`039e93463c630fe17cdd3a8e98c3c551caec5be8d7f5ab211aad397859378367`
- 主程序 SHA-256：`fa44e16b0881964076091e0f93cd3abb1866cc8d0b6a2217f6281ebe6d20d5a7`
- 插件 SHA-256：`cdc1962e589e292d8922967052d256164001da130eb4cddfcb7e1916a18402bd`
- AlphaEarth index SHA-256：`15875963d1bf4dd3f35a1f6c3ec6329378fef0fab6549fac677552f1d348f736`
- 数据清单 SHA-256：`d026f04aafdc218380798cbe84bf390d179b483aff1cfc41b0a68ce8ad11d957`
- 审计 JSON SHA-256：`5fc3530847b943c2244927187fb1cb5e033f84a83c19a741e7c956909810cbc5`
- 审计文本 SHA-256：`8bc5db133b2b9c8ace96f118d98551ff2dab8d64fc7dda89bc41c8995466cfac`

插件实际位置为
`Contents/lib/osgPlugins-3.6.5/osgdb_science.so`，候选中恰好一份。`nm -gU` 只输出：

```text
0000000000005110 T _osgsol_science_g0_probe_anchor
```

科学插件专属依赖闭包由以下五个文件组成：

- `Contents/lib/libfontconfig.1.dylib`
- `Contents/lib/libfreetype.6.dylib`
- `Contents/lib/libintl.8.dylib`
- `Contents/lib/libpng16.16.dylib`
- `Contents/lib/osgPlugins-3.6.5/osgdb_science.so`

## 体积结果

| 门槛 | 实测 | 结论 |
| --- | ---: | --- |
| 运行时增量 | 33,776,769 bytes（约 32.21 MiB） | PASS |
| 已验证科学数据 | 87,183,360 bytes（约 83.14 MiB） | PASS |
| 包体总增量 | 120,960,129 bytes（约 115.36 MiB） | PASS |
| 科学插件闭包 | 31,734,624 bytes（约 30.26 MiB） | PASS |

批准基线为 542,594,200 bytes，候选总大小为 663,554,329 bytes。本轮没有依靠放宽包
大小标准过关；即使按用户允许适度放松的方向评估，当前值也已在既有 PASS 范围内。

## 验证记录

- Science-on 离线 CTest：54/54，通过；明确排除 `network-local`。
- Python manifest、审计、运行时前缀和编译路径测试：92/92，通过。
- 私有依赖验证：固定归档校验、GDAL independent embed capability、静态前缀与 manifest，
  全部通过。
- macOS 正式打包契约：身份、来源、依赖闭包、路径净化和深度签名全部通过；runtime smoke
  明确跳过，没有启动 App。
- Science-off：Earth 主程序重建通过，隔离契约 2/2 通过，生产科学插件不存在。
- G0 v2 实包审计：总结果 `PASS`；Tier A `PASS`；Tier B `PASS`；0 finding、0 violation、
  0 unresolved、0 root cause；相对 ratchet 新增 0、移除 1,086。
- 候选 `codesign --verify --deep --strict`：通过。

完整报告：

- `build/science_g0_plugin_audit/audit.json`
- `build/science_g0_plugin_audit/audit.txt`

## 已完成能力的保护结论

现有 AlphaEarth、Sentinel-2、Copernicus DEM、64 维分析、AI research、预览覆盖范围、
面板入口和普通 Earth 主程序没有被删减或改成占位实现。现有科学单元测试仍直接覆盖内部
库；产品主程序只是把同一套对象的所有权移到插件内部。相机、地图、图层、照片、Quit 和
面板布局的既有回归均包含在 54 个离线测试中并保持通过。

## 2026-07-19 首次启动崩溃与修复

用户手动打开来源提交 `f8b6034` 的首个插件候选后，程序在约 2.2 秒内退出。对应报告为
`~/Library/Logs/DiagnosticReports/osgSol_Earth-2026-07-19-141458.ips`，候选主程序和插件
UUID 与报告完全匹配。异常为 `EXC_BAD_ACCESS / SIGSEGV`，非法地址 `0x1470`；崩溃线程
位于首次科学面板绘制，栈顶业务函数是
`ScienceEarthPanel::drawOperations(...) + 104`。

根因不是签名或 macOS 安全机制，而是主程序和插件分别静态链接了 Dear ImGui。二者各自有
一个 `GImGui` 全局：主程序副本已有 context，插件副本仍为空。Dear ImGui 自身头文件明确
要求跨静态库/DLL 边界调用时同时执行 `SetCurrentContext()` 和
`SetAllocatorFunctions()`；旧 ABI 没有提供这条桥，因此插件第一次调用
`ImGui::CollapsingHeader()` 就解引用空 context。

提交 `4252e75` 将插件 ABI 升至 v2，增加 context 与 allocator 桥。主程序在左右科学面板
每次绘制前传递当前 context、allocate、deallocate 和 user data；插件绑定后才绘制。任一
必要项为空时，门面跳过绘制而不进入插件。失败测试先证明旧 ABI 缺失该能力，随后聚焦测试
验证每次绘制都先绑定、参数逐项一致、缺失 context 不调用绘制、旧 ABI 和缺函数表继续
fail closed。

旧崩溃候选及原审计报告保存在
`build/science_g0_plugin_audit/broken-f8b6034/`，不能再用于手测。固定候选路径已替换为来源
提交 `4252e75` 的修复包。本代理没有启动修复包，因此“能够实际启动且退出不产生新 `.ips`”
仍须由用户手动确认。

## 后续必须补做

G0 代码和离线审计已关闭，但发布前仍必须由用户手动打开上述精确候选包，至少验证：

1. 普通地球、相机、地图、图层、鼠标操作和既有 UI 无回退；
2. 三个科学数据源可用，64 维分析、年份切换、图例、左右面板和 AI research 能正常联动；
3. 拍照保持用户当时可见视角，高空视角不会生成低空语义；
4. Quit 正常退出，并确认没有新生成与该候选对应的 `.ips` 报告；
5. 若人工验收通过，再按用户明确指令更新固定桌面 App、打 tag 和同步。当前任务没有执行
   这三项发布动作。

## 后续可选优化

DEM 的低起伏区域对比度可独立增加“局部百分位拉伸”和“hillshade + 分层设色”模式，
但必须保留原始高程值、明确模式与图例；这不是 G0 插件隔离的阻塞项。
