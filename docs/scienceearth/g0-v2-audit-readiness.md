# ScienceEarth G0 v2 审计准备状态

记录日期：2026-07-19

## 结论

G0 v2 已从此前的 `STOP` 转为正式 `PASS`。插件隔离、私有依赖、路径净化、正式 staging
打包和实包审计全部完成；Tier A 与 Tier B 都为 `PASS`，当前 finding、violation、未解析
依赖和根因均为 0，四项体积门槛全部为 `PASS`。

当前可人工验证的隔离候选为：

`build/science_g0_plugin_audit/candidate/osgSol Earth.app`

详细架构、哈希、闭包成员、测试记录和人工验收清单见
`docs/scienceearth/g0-plugin-isolation.md`。

## 从 STOP 到 PASS 的关闭项

此前三个阻塞根因已经逐项关闭：

1. 科学运行时从主程序移入唯一 `osgdb_science.so`，插件只导出
   `_osgsol_science_g0_probe_anchor`；
2. GDAL 使用 `GDAL_AUTOLOAD_PLUGINS=OFF` 的私有静态构建，主程序不再包含科学静态栈；
3. fontconfig/gettext/Lua 的固定安装前缀和两个编译源码根只在 disposable staging 副本中
   以严格白名单、等长方式净化，并在最终签名前验证。

审计规则、不可变 v0.2 reference 和 ratchet 没有被改写或弱化。普通 support dylib 的常规
导出不再被错误当成插件 ABI；通用 ReaderWriter 中的 ZSTD 压缩能力也不再被误判为科学
所有权，但主程序中的 GDAL/OGR/OSR/PROJ 证据和主程序 ZSTD 证据仍会 fail closed。

## 最终审计摘要

- App 内容来源提交：`f8b60341d753d61fa24bec24d6f9d84c40f0d362`
- 版本 / 频道：`0.6.0` / `g0-plugin-audit`
- 候选 fingerprint：`3502dcba3dda8d831b399df0863e2e1f3ba2fc7331135d1c4350a4c38b553416`
- 总结果：`PASS`，退出码 0
- Tier A / Tier B：`PASS` / `PASS`
- finding / violation / unresolved / root cause：`0 / 0 / 0 / 0`
- 相对 ratchet：新增 0，移除 1,086
- 基线 / 候选：542,594,200 / 663,553,961 bytes
- 运行时增量：33,776,401 bytes，`PASS`
- 科学数据：87,183,360 bytes，`PASS`
- 总增量：120,959,761 bytes，`PASS`
- 科学插件闭包：31,734,368 bytes，`PASS`
- 签名：`codesign --verify --deep --strict` 通过

报告：

- `build/science_g0_plugin_audit/audit.json`
- `build/science_g0_plugin_audit/audit.txt`

## 不可变基线

- v0.2 baseline：`build/desktop-backup-pre-scienceearth/osgSol Earth.app`
- 批准 bundle fingerprint：`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`
- reference 文件 SHA-256：`afd80d8ad9419882793926d781affb970246358fbf74c514b950bf0eb924a5a6`
- reference 规范内容哈希：`145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada`
- ratchet 文件 SHA-256：`a732bacfec5b16b6e42ef4b4573827df6edfa73a5d8bf5b2c7b94d0d684c06cc`

批准 reference、ratchet 和保护基线未修改。`/Users/USER/Desktop/osgSol Earth.app` 未启动、
未覆盖、未重新签名；macOS 设置未修改，也没有创建本地 listener。

## 发布前仍必须完成

静态 G0 已关闭，但运行时人工验收不能由离线审计替代。用户需要手动打开精确候选包，验证
普通 Earth 无回退、科学数据与 AI 联动、拍照保持可见视角以及 Quit 后没有新的匹配 `.ips`。
通过后，桌面固定 App 更新、tag 和同步必须作为单独的明确发布动作执行。

## 可选而非 G0 阻塞项

DEM 低起伏区域的视觉对比度可以后续增加局部百分位拉伸和 hillshade 分层设色。包大小也可
按用户决策适当放松，但本候选四项已经全部处于现行 `PASS` 区间，无需靠放宽门槛过关。
