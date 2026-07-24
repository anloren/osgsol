# osgSol Earth 全产品 UI/UE Goal 状态

更新时间：2026-07-24

## 已冻结基线

- 代码基线：`2d8efc6 fix(scienceearth): separate UI sync from user events`
- 分支：`codex/scienceearth-agro-climate-v1`
- 正式 App：`/Users/USER/Desktop/osgSol Earth.app`
- 当前截图：`/Users/USER/Desktop/Screenshot 2026-07-24 at 11.21.07 AM.png`
- 设计方向：`Obsidian Survey / 深夜测绘仪`
- 自动化边界：不启动、不前置桌面 App，不修改 macOS 安全设置

## Goal 阶段

1. 全产品表面、状态和控件审计；
2. 统一设计系统与布局契约；
3. 底层 token、停靠、滚动、控件、窗口、图表整改；
4. 科学工作台、报告和 AI 主流程整改；
5. 探索、图层、实时、卫星、三维、任务、设置和状态区迁移；
6. 多分辨率视觉/交互验收、离线回归、包体审计；
7. 更新唯一桌面候选供用户手测；
8. 只有用户明确验收后才 Tag 与同步。

## 当前必须完成

- [x] 修复科学报告最终地图快照采集时序；
- [x] 消除模块轨、抽屉和滚动条旁透明缝；
- [x] 删除伪装成滑杆的年份装饰，改成明确年度摘要；
- [x] 图表补齐指标、单位、X/Y 坐标、选中和缺测语义；
- [x] 统一 ImGui/RmlUi 颜色、边框、圆角与状态；
- [x] 消除固定宽度引发的卡片、菜单、按钮和文本溢出；
- [x] AI 命令栏与顶部动作响应式布局；
- [x] 其余所有正式模块逐项迁移与审计；
- [x] 1024×576、1440×900、2048×1152 自动边界测试；
- [x] 80/80 离线全回归；
- [x] 签名和包体审计、唯一桌面候选更新；
- [ ] 用户正式包可视与 Quit 验收。

## 自动化验收记录

- 构建树：`build/science_g3_release`
- 安装树：`build/science_g3_release/sdk`
- 离线回归：80/80 通过，0 失败
- UI 运行时：八个主模块、三档视口、科学菜单、滚轮双向、分析提交、
  报告关闭边界和图表坐标均通过
- 科学源切换：ERA5 农业气候、AlphaEarth、Sentinel-2、
  Copernicus DEM、ERA5-Land 均通过
- 地图与退出保护：地形科学融合合同、3D Tiles、相机、正常退出守卫均通过
- 正式打包契约：通过
- G0-v2 桌面包审计：PASS，根因 0、未解析依赖 0、四项包体门槛均 PASS
- 严格深度签名：PASS
- 唯一桌面候选：`/Users/USER/Desktop/osgSol Earth.app`
- 候选版本：`0.6.1`，阶段 `G3.1`，来源提交
  `7ff1263816713e3005aea98c3280ce6b94b98551`
- 上一桌面候选备份：
  `build/desktop-backups/pre-ui-ux-goal-20260724-7ff1263/osgSol Earth.previous.app`
- 自动化没有启动、前置或操作桌面候选

## 仍需用户手测

- 唯一桌面候选在真实 Retina 屏幕上的字体、密度、边缘和地图占比；
- 地形起伏上的科学覆盖、香港三维、卫星影像与移动视角无闪烁、陷地和错位；
- 自然语言、照片、视频和跨数据源研究的真实服务调用；
- 使用应用内 Quit 后无 macOS 异常退出提示，且无新的匹配 `.ips` 报告。

## 当前不得回退

- 地球相机、导航、点击选择和可见视角拍照；
- 地形、影像、海洋、科学层融合和高程比例；
- 香港三维数据的坐标、LOD、加载和所有权；
- AlphaEarth、Sentinel-2、ERA5、DEM 等科学计算与证据；
- AI agent、research、工具调用和跨数据源联动；
- 插件隔离、包标识、正常退出和 macOS 安全边界。
