# osgsol 初始快照全项目复审 — 2026-07-10

> 基线：`osgverse` 提交 `223f8d42c74718d23549f8e0fbe2cea9096a4598`。
> 方法：继承 60 条 Earth Explorer 对抗性审计，扩展到引擎核心、I/O/插件、第三方边界、构建和交付；三路初审后再逐条反方核实。
> 结果：新增候选 32 条，确认 31 条、并回既有 finding 1 条；新增严重度 **High 8 / Medium 17 / Low 6**。

## 0. 总体结论

老报告的 60 条 confirmed finding 在当前提交仍全部成立，因为 `223f8d42` 相对 `master` 只新增审查文档，没有代码修复。扩展复审又确认 31 条项目级问题，因此初始 osgsol 快照合计 **91 条 confirmed finding：High 10 / Medium 45 / Low 36**。

原 `2026-07-10-v023-quality-batch-design.md` 对 Earth Explorer 的两波设计仍然有效，但它只覆盖老基线的 High 2 + Medium 28，不能再代表“全项目质量清零”。新增的 8 High 中，C03（ONNX）和 C10（Bullet）只在可选依赖启用时进入构建；其余 C01/C02/C06、I01/I02、D01 位于默认引擎、通用读取或发布路径，应在公开仓库或正式分发前优先处理。

| 来源 | High | Medium | Low | 合计 |
|---|---:|---:|---:|---:|
| 既有 v0.22 对抗性审计 | 2 | 28 | 30 | 60 |
| 本次全项目扩展复审 | 8 | 17 | 6 | 31 |
| **osgsol 初始总量** | **10** | **45** | **36** | **91** |

## 1. 范围、证据与限制

- 引擎核心：`pipeline/`、`modeling/`、`animation/`、`ai/`、`ui/`、`script/`、`wrappers/`、`VerseCommon.h`；303 个文件、约 50,322 行。
- I/O 与扩展：`readerwriter/`、`plugins/`、第三方的实际构建/调用边界、`assets/shaders/`；不把纯上游 vendored 代码当作逐行自研审计对象。
- 应用与交付：`applications/`、`tests/`、`android/`、`wasm/`、`helpers/`、`cmake/`、`packaging/`、`dockerfiles/` 和根构建文件。
- 4,477 个 tracked path；本次新建目录后，从新源码根完成 Release 全量 `install` 构建并重新运行六个离线逻辑测试。
- 两个标准 pipeline JSON 可解析；环境没有 `glslangValidator`/`glslc`，所以本报告不声称完成全量 shader 离线编译。
- 许可证条目 I09 只确认本地清单与源码声明冲突；具体分发义务需由项目维护者或法律顾问确认。

## 2. 既有 60 条基线复核

- 原报告：`docs/superpowers/2026-07-10-master-v022-full-review.md`。
- 原始数据：`docs/superpowers/research/2026-07-10-master-v022-review-findings.json`。
- 60 个 `file:line` 均指向存在且行号有效的当前文件；确认位置检查 `total=60 bad=0`。
- 既有严重度保持 High 2 / Medium 28 / Low 30；六条 rejected 继续维持驳回，不在本报告复活。
- 2026-07-08 backlog 的 P0 五项仍为 fixed；其余 open/partial 状态没有因文档提交改变。

## 3. 新增引擎核心 finding（C01-C12）

### C01 [High][MCP] SSE worker 自己 join 自己，关闭流程又在停止标志之前持锁 join

- **位置**：`ai/McpServer.cpp:149-162,349-359,451-478,512`。
- **失败场景**：SSE 断开时 worker 调 `closeSession()`，取出自己的 `std::thread` 后 `join()`，异常逃出线程并触发进程终止；`cancel()` 则持 `sseMutex`、在 `running` 仍为 true 时 join，连接未断会确定性卡住。`running` 还是跨线程裸 `bool`。
- **修法**：先原子停止；锁内只摘取 session/thread，锁外由 owner/reaper join；worker 只标记清理，禁止自 join；删除 `release()` 泄漏。工作量 M。

### C02 [High][MCP] 外部 cursor 可负索引或抛异常终止线程池

- **位置**：`ai/McpServer.cpp:484-485,801-808`；异常边界缺失在 `ai/McpServer.h:39-50`。
- **失败场景**：`cursor_-1` 让 `allItems[i]` 以负数索引；非数字或溢出文本让 `std::stoi` 抛异常。线程池直接执行 `task()` 无 catch，匿名远程请求可触发 `std::terminate`。
- **修法**：用 `from_chars` 做完整消费和范围校验，负数/越界返回 JSON-RPC `InvalidParams`；线程池最外层增加异常边界。工作量 S。

### C03 [High][ONNX，可选构建] tensor 拷贝把字节数当成元素偏移

- **位置**：`ai/OnnxRuntimeEngine.cpp:123-139,199-210`。
- **失败场景**：`totalSize` 以 byte 累加，却加到 `T*`/`float*`；普通两行 float 输入第二行就写出 tensor。批量 float image 同错，且没有拒绝 ragged/异构输入。
- **修法**：维护元素偏移或统一使用 `std::byte*`，checked 计算 tensor 元素总数，并验证所有行/图像的尺寸和类型。工作量 S-M。当前本机构建未找到 ONNX Runtime，但支持该依赖的构建会启用此路径。

### C04 [Medium][ONNX，可选构建] 输入跨 run 累积，失败却返回成功

- **位置**：`ai/OnnxRuntimeEngine.cpp:252-268,393-443`。
- **失败场景**：每次 `addInput()` 追加，`run()` 只清输出；同一 inferencer 第二帧开始携带重复输入。同步异常被吞掉，状态未标失败，调用方仍得到 `true`。
- **修法**：明确每轮输入生命周期（按 name 替换或 scope-guard 清理），让 `runSync` 返回/传播失败并一致更新 callback/status。工作量 S-M。

### C05 [Medium][NIS，显式接入组件] UBO 上传的是 config 指针变量地址

- **位置**：`pipeline/NISUpscaler.cpp:341-352`。
- **失败场景**：`_config` 是 `NISConfig*`，却向 GL 传 `&_config`；驱动从成员指针槽开始读取 `sizeof(NISConfig)` 字节，得到垃圾参数并越过该成员读对象内存。仓内没有现成调用点，故从 High 校准为 Medium。
- **修法**：传 `_config` 并判空，优先改为 RAII/by-value；增加布局断言和 mock GL 上传字节测试。工作量 S。

### C06 [High][并发] ImGui event 与 PRE/POST_DRAW 并发改同一 context

- **位置**：`ui/ImGui.cpp:246-308,324-339,391-397`；同型代码 `ui/ImGui3D.cpp:37-68,93-103`。
- **失败场景**：主线程 event traversal 调 `AddKeyEvent`/`AddInputCharacter` 并写鼠标字段，draw 线程同时 `NewFrame`、消费/reset；DrawThreadPerContext 下正常键鼠输入可与 ImGui 内部 event vector 扩容并发，造成丢事件、堆破坏或崩溃。
- **修法**：event 侧只向加锁 POD 队列写入；draw 线程统一 drain 并调用全部 ImGui API；capture 状态用 atomic/锁反向发布，补 threaded-viewer TSAN 压测。工作量 M。

### C07 [Medium][Python] CPython raw storage 中的 C++ 成员未构造/析构

- **位置**：`script/PythonWrapper_osg.cpp:63-90`。
- **失败场景**：`tp_alloc` 不会构造两个 `osg::ref_ptr` 和 `std::string`，代码却直接赋值；`tp_free` 也不析构，导致 UB、引用计数/字符串泄漏。Python 3.14 在本机构建中实际启用。
- **修法**：placement-new 一个 RAII payload 并在 free 前显式析构，或改 capsule/pybind11 holder；覆盖所有分配失败路径。工作量 M。

### C08 [Medium][动画] BlendShape 基准姿态与输出数组别名，形变逐帧累积

- **位置**：`animation/BlendShapeAnimation.cpp:39-49,59-87`。
- **失败场景**：backup 保存 live array 指针而非深拷贝，vertex restore 成为自拷贝，normal/tangent 根本不恢复；持续权重会每帧重复加 delta。空数组和通道长度不一致还有边界风险。
- **修法**：深拷贝三类 base array，每帧先完整恢复；空数组早退，按各通道真实长度验证。补两帧幂等和畸形通道测试。工作量 S-M。

### C09 [Medium][动画] Aim IK 把 chain 位置当 skeleton joint id

- **位置**：`animation/PlayerAnimationInternal.cpp:346-396`。
- **失败场景**：后续关节使用 `_models[i-1]`/`_models[i]` 并写 joint `i`，非连续 spine/neck/head 链会改错骨骼；空链 `back()`、过大 joint id 也未防。
- **修法**：所有访问都用 `chain[i].joint`，验证非空和每个 id 的上下界；补非连续链、空链、越界链测试。工作量 S。

### C10 [High][Bullet，可选构建] removeBody 留下约束引用已释放 rigid body

- **位置**：`animation/PhysicsEngine.cpp:86-98,176-211`。
- **失败场景**：body/shape 被删除前不移除相关 constraint，Bullet world 与 `_constraints` 保留悬垂 A/B 引用，下一步模拟或后续 removeConstraint 形成 UAF；重复 constraint name 还会遗留 active 且失联的旧约束。
- **修法**：按 body 追踪并先移除所有约束，或在受约束时拒绝删除；同名替换必须走 removeConstraint；补两种 API 顺序测试。工作量 M。当前本机构建未找到 Bullet。

### C11 [Low][生命周期] Drawer2D context/worker 所有权不完整

- **位置**：`pipeline/Drawer2D.cpp:14-18,53-59,97-110,192-242`；`pipeline/Drawer2D.h:41-58` 无析构。
- **失败场景**：context 未初始化，`finish()` 删除后不置空；无析构收尾；显式 copy 共享 raw worker/context；worker loop 后又无条件解引用 observer。仓内正常调用目前是一 start/finish 且无 copy，故由 Medium 降为 Low。
- **修法**：context RAII/null 初始化、finish 幂等、析构 cancel/join；worker owner 禁拷贝并在最后回调前重查 observer。工作量 M。

### C12 [Low][UI] SerializerBaseItem::_edited 未初始化

- **位置**：`ui/SerializerInterface.cpp:8-10`、`ui/SerializerInterface.h:28-44`。
- **失败场景**：首次 `checkEdited()` 读取不定值，Scene Editor 可在没有编辑时误报一次刷新。
- **修法**：按声明顺序初始化 `_edited(false)`，优先使用 in-class initializer，并补构造态测试。工作量 XS。

## 4. 新增 I/O、插件与供应链 finding（I01-I10）

### I01 [High][Web plugin] gzip 解压越界写固定 10 倍缓冲

- **位置**：`plugins/osgdb_web/ReaderWriterWeb.cpp:16-33,281-288`。
- **失败场景**：正常 WITH_ZLIB 构建中，`readGZip()` 忽略 `out_size` 和 zlib 状态，把完整结果 `memcpy` 到 `compressed_size*10` 的 vector；合法压缩率超过 10:1 即写越界。
- **修法**：返回有硬上限的动态容器或按剩余容量分块写；限制总解压量/压缩比并检查 `inflateInit2`、每次 inflate 和 `Z_STREAM_END`。工作量 S-M。

### I02 [High][3D Tiles/glTF] B3DM/I3DM 头与 section 长度未校验

- **位置**：`readerwriter/LoadSceneGLTF.cpp:190-227,369-405`。
- **失败场景**：入口只判 `size>4` 就固定 memcpy 28/32 字节；文件控制的有符号长度无 checked-add，offset 随后进入 iterator、memcpy 和 `size-offset`。截断/畸形远程瓦片可越界读或 size_t 下溢。
- **修法**：显式 little-endian unsigned checked parser；先验最小头、magic/version/总长，再逐段验证区间完全落在实际 buffer。工作量 M。

### I03 [Medium][KTX] malloc/realloc 缓冲用 scalar delete 释放

- **位置**：`readerwriter/LoadTextureKTX.cpp:528-542`；分配在 `3rdparty/ktx/memstream.c:193-210`。
- **失败场景**：ostream KTX2 成功路径对 KTX memstream 的 malloc/realloc buffer 执行 `delete buffer`，形成可重复的 allocator mismatch/heap UB。
- **修法**：使用 KTX 指定释放 API或 `free()`，并用 RAII 覆盖异常/早退。工作量 S。

### I04 [Medium][3DGS] LCC index 可构造 mmap 外切片并继续按 numSplats 读取

- **位置**：`plugins/osgdb_3dgs/SplatReaderLCC.cpp:68-84,103-129,245-275,320-341`。
- **失败场景**：`index.bin` 控制 offset/byteSize/numSplats；即使 end 被截断，解析仍按每点 32/64 字节读取，start 和 SH 切片也没独立边界/溢出检查。
- **修法**：checked 验证 start/end、count*recordSize 和 SH 切片；禁止“截断后继续解析”。工作量 S-M。

### I05 [Medium][Terrain] quantized-mesh count 可无界分配并使解析错位

- **位置**：`plugins/osgdb_terrain/ReaderWriterTerrain.cpp:48-103,164-178,215-245`。
- **失败场景**：文件 count 直接驱动多组 resize，未校验剩余字节、上限、乘法或 stream 状态；可造成超大分配、未捕获分配失败、32 位算术回绕和后续解析错位。短读本身未证明必然 OOB，因此采用收紧后的 Medium 表述。
- **修法**：按输入总长逐段消费，count*stride checked arithmetic，设置顶点/三角形上限，每次读取后验证完整字节数。工作量 M。

### I06 [Medium][3D Tiles] transform 数组可抛异常或消费未初始化元素

- **位置**：`plugins/osgdb_3dtiles/ReaderWriter3dTiles.cpp:290-300`。
- **失败场景**：任意 array 都 `at(0..15)`；短数组异常越过 reader 边界。16 项内的非 number 留下 `double m[16]` 未初始化，再构造矩阵。
- **修法**：要求恰好 16 个 finite number，先初始化；任一非法值返回明确解析错误。工作量 S。

### I07 [Medium][glTF] 远程外部资源绕过 tinygltf 大小上限

- **位置**：`readerwriter/LoadSceneGLTF.cpp:93-110,182-187`；guard 在 `3rdparty/tiny_gltf.h:2525-2548`。
- **失败场景**：回调写成 `filesize_out=0` 而非给 pointee 填真实大小，却返回成功；size guard 永远看到 0，随后完整 HTTP body 一次性进 vector。
- **修法**：真实 Content-Length + 接收过程硬上限；未知长度也按累计字节中止，完成后复核实际大小。工作量 M。

### I08 [Medium][并发] FileCache reader-writer map 在多 pager 首次查找时竞态

- **位置**：`readerwriter/FileCache.cpp:25-39`、`readerwriter/FileCache.h:48-56`。
- **失败场景**：共享 FileCache 在 20 database + 16 HTTP pager 线程下无锁 find/insert 同一个 `std::map`，正常首次扩展名解析即可容器竞态和树损坏。
- **修法**：mutex + 锁外 Registry 查找、锁内 double-check/insert；补多扩展首次解析并发测试。工作量 S。

### I09 [Medium][合规元数据] InteractiveMesh 的 MIT 清单遗漏实际编译路径中的 LGPL 声明

- **位置**：`THIRDPARTY_LICENSES.md:45`；`3rdparty/InteractiveMeshBool/implicit_point.h:9-24`、`memPool.h:9-24`、`numerics.h:9-24`；构建边界 `3rdparty/CMakeLists.txt:483-493,568-571`。
- **失败场景**：清单只写 MIT，但 CINOLIB_FOUND 变体包含明确标注 LGPL-3.0-or-later 的 headers；按现清单生成 NOTICE/依赖清单会遗漏 mixed-license 事实。
- **修法**：核实上游文件边界、补许可证文本并准确描述 mixed-license；发布流程加许可证清单漂移检查。工作量 S-M；具体法律义务另行确认。

### I10 [Low][依赖清单] MeshOptimizer 标为 0.26，实际 vendored 为 0.21

- **位置**：`THIRDPARTY_LICENSES.md:58`；`3rdparty/meshoptimizer/meshoptimizer.h:1-15`。
- **失败场景**：依赖/漏洞匹配和升级判断针对错误版本，可能遗漏只影响实际 0.21 的维护结论。
- **修法**：更正清单或完整升级；记录上游 commit/content hash 并在 CI 比对。工作量 S/M。

## 5. 新增构建与交付 finding（D01-D04、D06-D10）

### D01 [High][凭证] macOS 打包把 live AI key 写进可分发 plist

- **位置**：`packaging/package_macos.sh:69-74`。
- **失败场景**：release operator 若导出 `EARTH_AI_KEY`，脚本把明文写入 `Contents/Info.plist/LSEnvironment`；任何 app 接收者都可读取并复用计费凭证。`dist/` 被 Git ignore 不构成分发保护。
- **修法**：禁止 bundle 内嵌 key；使用现有 per-user 配置或 Keychain；打包环境存在 key 时 fail/warn，并做 post-package secret/plist assertion。工作量 S。

### D02 [Medium][签名完整性] 运行时 imgui.ini 可写进 bundle 并破坏 ad-hoc 签名

- **位置**：`pipeline/Utilities.cpp:232-236`、`ui/ImGui.cpp:120-134`、`packaging/package_macos.sh:77-80`。
- **失败场景**：应用把 cwd 设为 executable dir，ImGui 未改默认 ini 路径；当前旧 artifact 已出现比 CodeResources 更新的 `Contents/MacOS/imgui.ini`，严格验签明确报 added file。没有证据证明每次 first-run 或 LaunchServices 后续启动必失败，故从 High 校准为 Medium。
- **修法**：把 ImGui ini 指向 Application Support 或禁用持久化；smoke-run 后再做最终严格验签。工作量 S。

### D03 [Medium][打包] codesign 失败被转成成功

- **位置**：`packaging/package_macos.sh:77-80`。
- **失败场景**：`codesign ... || echo` 吞掉失败，随后仍打印 `Built:` 且 exit 0；自动化可接收未签/部分签名包。
- **修法**：默认 fail closed，脚本结尾执行 `codesign --verify --deep --strict`；只允许显式 `--allow-unsigned` 本地模式。工作量 S。

### D04 [Medium][测试门禁] 42 个测试/示例二进制，CTest 注册数为 0

- **位置**：`tests/CMakeLists.txt:41-49,128-133`；全仓无 `enable_testing()`/`add_test()`。
- **失败场景**：常规 `ctest` 成功但运行 0 项，六个确定性离线 Earth/AI/parser 测试也不会被 CI 自动发现。
- **修法**：启用 CTest，只注册 deterministic/offline 目标并设置 label、timeout、fixture/env；互动渲染 example 保持分离。工作量 M。

### D06 [Low][开发服务器] HTTPS 模式默认监听 0.0.0.0 并服务 cwd

- **位置**：`wasm/run_webserver.py:13-18,78-99`。
- **失败场景**：证书模式向 LAN 暴露 SimpleHTTPRequestHandler、目录列表和 wildcard CORS；README 推荐 cwd 是 WASM build output，未证明常规会暴露 repo/key，因此从 Medium 降为 Low。
- **修法**：默认 loopback；LAN 需显式 `--bind`，强制 `--directory`，可选拒绝 dotfile/wildcard CORS。工作量 S。

### D07 [Medium][可复现构建] Linux Dockerfile 不使用被审查 checkout

- **位置**：`dockerfiles/linux/Dockerfile:23-40`。
- **失败场景**：没有 `COPY`，而是 clone 远端默认分支；从 `223f8d42` 发起的 docker build 可装入未来/不同代码，ZLMediaKit 与 OSG-Data 也使用 mutable head。
- **修法**：COPY 当前 context 或接受并验证 immutable commit；依赖 pin full commit/digest，镜像记录 source revision/SBOM。工作量 M。

### D08 [Medium][可移植性] 默认按 build host 的最高 x86 ISA 全局编译

- **位置**：`CMakeLists.txt:93-150`、`cmake/GetCPUSIMDFeatures/GetX64SIMDFeatures.cmake:23-66`。
- **失败场景**：读取 host `/proc/cpuinfo`/`sysctl` 并全局加 AVX/AVX2/条件性 AVX-512 等 flags；新机器构建的 release 可在老支持机器 illegal instruction，cross build 也受 host 而非 target 决定。
- **修法**：分发默认声明保守 target baseline；native 显式 opt-in；新 ISA 用 runtime dispatch/独立对象。工作量 M-L。

### D09 [Low][Android] release signing 是 tracked placeholder

- **位置**：`android/app/build.gradle:19-29,39-48`、`.gitignore:34-39,58-63`。
- **失败场景**：`assembleRelease` 绑定不存在 keystore 和占位密码；支持文档主要走 assembleDebug，故降为 Low，但仍是 release 模板不可用和把真凭证写进 tracked 文件的诱因。
- **修法**：从 env/ignored `keystore.properties` 读取，仅在完整时配置；否则给明确错误；ignore `*.jks`/`*.keystore`。工作量 S。

### D10 [Low][打包兼容] 插件目录硬编码 osgPlugins-3.6.5

- **位置**：`packaging/package_macos.sh:7,21-23`；动态版本逻辑在 `CMakeLists.txt:518-559`。
- **失败场景**：项目声明支持其他 OSG 版本，但脚本在非 3.6.5 SDK 下 cp 失败或缺插件。
- **修法**：从 install tree/CMake metadata 读取且验证唯一 plugin dir。工作量 S。

## 6. 新增驳回、降级与并项记录

- **D05「新闻正文单响应无 receive cap」不单列**：事实成立，但与既有 F22/F54 是同一 fetch/cache 根因和同一修复面；把 receive-time cap 合并进那两条，避免重复计数。
- **D02 从 High 降 Medium**：严格验签失败已复现，但“首次运行必然”和“后续 LaunchServices 必失败”没有实证。
- **D06 从 Medium 降 Low**：0.0.0.0/cwd 机制属实，但 README 指定 WASM 输出目录且 LAN 证书测试是显式场景，未证明常规暴露源码/密钥。
- **D09 从 Medium 降 Low**：release 模板不可用，但现有支持路径是 assembleDebug，未证明已存在正式 release pipeline。
- **C05 从 High 降 Medium**：指针上传错误确定，但仓内没有 NISUpscaler 使用点，只在显式接入后触发。
- **C11 从 Medium 降 Low**：所有权缺陷确定，但仓内正常 start/finish 成对且无 copy 使用，主要是 public API 误用/异常 teardown。
- **gzip bomb 不再独立列**：无界增长被更严重的 I01 越界写覆盖。
- **Terrain 32-bit edge 的 `sizeof(short)` 候选不列**：当前几何不消费该边界数组，未形成可观察失败。
- **LCC SH 通道赋值疑似错误不列**：缺少格式规范/黄金样本证明语义。
- **NIS GL state 泄漏、多 context UBO、LightGlobalManager race、Gaussian sorter lifetime 不列**：静态路径不足以证明实际调度/生命周期，保留给定向测试。
- **AI photo card 的 shell 注入不列**：path 由本机 HOME/环境和 timestamp 形成，未找到远程/model string 数据流；仍建议以后改 argv API。
- **跨文件版本号不同不列**：库、demo app、bundle 可能是不同版本域，尚无单一版本源契约。

## 7. 构建、测试与发布验证

- 全新源码根 `/Users/USER/osgsol`，规范 build dir `build/osgsol_core`；CMake configure exit 0。
- `cmake --build build/osgsol_core --target install -j4`：Release 全量构建 exit 0，安装库、插件、42 个测试/示例和 EarthExplorer。
- 新构建后的离线测试：`Feeds`、`Ai_Chat`、`Ais`、`Satellite`、`TileOverlay`、`WorldTools` 全部 exit 0。
- `ctest --test-dir build/osgsol_core -N`：`Total Tests: 0`，对应 D04。
- 明确移除 `EARTH_AI_KEY` 后重新打包：package exit 0；strict codesign exit 0；plist 无 key；包内无 `imgui.ini`。
- 旧复制 artifact 曾因新增 `Contents/MacOS/imgui.ini` strict codesign exit 1；重新打包后已恢复有效，证据用于 D02。
- tracked source 常见私钥/API token 模式扫描：0 个候选。

## 8. 建议执行顺序

1. **公开/分发门禁**：先修 D01，并让 D03 的签名失败真正阻断；保持 GitHub private。
2. **默认构建 High**：C01、C02、C06、I01、I02；每条补最小回归测试和异常边界。
3. **可选依赖 High**：C03（ONNX）、C10（Bullet）；在依赖可用的 CI job 中修复和验证。
4. **既有 Earth v0.23 波1/波2**：继续按已批准设计消化原 High 2 + Medium 28，但把 D04 CTest 门禁提前接入。
5. **Medium/Low 收尾**：按模块分批，避免一次把 25 条跨模块债务揉成不可审查的大提交。

本次只完成审查、迁移与验证；没有修改产品实现。新发现原始数据见 `docs/superpowers/research/2026-07-10-osgsol-full-project-review-findings.json`。
