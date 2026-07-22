# ScienceEarth scientific workbench product contract

Date: 2026-07-22

Status: implementation contract

## Purpose

ScienceEarth analysis is one continuous task: choose an analysis, bind it to a
geographic target, set time and method, run it, and inspect evidence. The formal
product UI must represent that task as one connected workbench rather than a
stream of source, cost, status, and result text split across unrelated panels.

This contract is grounded in the supplied current-state capture:

![Current agricultural climate analysis](2026-07-22-current-science-analysis.png)

The current map does not identify the analyzed point or cell. The operation
drawer begins midway through a numbered process, while the narrow result column
stacks charts without target, source, method, or report-window controls. This
contract fixes those presentation and interaction failures without changing the
science providers or Earth renderer.

## Product surfaces

### Analysis composer

- Location: Science module drawer beside the permanent module rail.
- Width: 340–380 logical pixels.
- Internal order: analysis template, target, time, method, resource review,
  run/progress footer.
- The footer remains visible while the drawer scrolls.
- Help, technical metadata, and evidence are progressive disclosures. Permanent
  explanatory paragraphs do not occupy the normal composer.

### Map target overlay

- Requested geometry is cyan and dashed.
- Provider-returned actual coverage is warm yellow and solid.
- A point target uses a center marker plus provider cell outline when available.
- The label contains the dataset, spatial unit, and years.
- The overlay is rendered after world geometry with depth testing disabled. It
  does not alter terrain, imagery, elevation, or camera state.

### Scientific report

- Default size: 900 × 680 logical pixels.
- Minimum size: 720 × 520 logical pixels.
- Maximum size: 80 percent of the viewport in each dimension.
- It is centered on first open, movable by its title bar, resizable from right,
  bottom, and bottom-right handles, minimizable, and closable.
- Close and minimize preserve the artifact. Deletion is a separate destructive
  action with confirmation.
- Sections: Overview, Trends, Spatial Range, Methods & Evidence.

## Fixed acceptance views

All design comparison captures use a 2048 × 1152 viewport and the existing
Carbon Spectrum dark visual language. The normal UI language is Chinese;
official dataset names, units, and proper nouns retain their source spelling.

### View A: empty composer

State: Science module opened, no target locked.

- Header: `科学分析` and a single help control.
- Analysis template: `农业气象年度分析` selected.
- One-sentence output: `比较所选位置各年度的温度、降水、蒸散、辐射和湿度。`
- Target card: `尚未锁定分析位置` with actions `锁定地图中心` and
  `使用当前视野`.
- Time range shows provider limits `1940–2025`; its values remain disabled until
  a target is locked.
- Sticky footer action `开始分析` is disabled and explains `请先锁定分析位置`.
- No result panel is visible.

### View B: locked ERA5 point

State: requested point locked at map center; no query submitted.

- Target card shows `地图中心点`, requested latitude/longitude, provider spatial
  support, and `镜头移动不会改变已锁定范围`.
- Actions: `更新为地图中心`, `改用当前视野`, `回到分析区域`.
- Map shows the requested point and expected grid support.
- Time appears as one grouped 2017–2025 control with start, end, and one range
  strip.
- Resource estimate is collapsed and labeled `提交前检查`.
- Sticky primary action is cyan/teal and reads `开始分析`.

### View C: running analysis

State: immutable request submitted; source is fetching or analyzing.

- Composer controls that would mutate the submitted request are disabled.
- The sticky footer occupies the same position and shows phase, elapsed time,
  determinate progress when available, and `取消`.
- The map target remains visible.
- An earlier successful report remains reachable as `上一份结果`.
- No modal blocks map navigation or Quit.

### View D: ready overview report

State: artifact ready and report opened once.

- Report title: `农业气象年度分析`.
- Subtitle: dataset, target label, and years.
- Title bar controls: minimize, close, and overflow menu with destructive delete.
- Overview contains a frozen 512 × 288 maximum map-context image with requested
  and actual coverage highlighted.
- Summary facts include target, provider-returned grid center, actual cell bounds,
  source, period, completeness, and execution time.
- Four summary values are selected by scientific relevance and labeled with
  units; the report does not imply that a point-grid series is a regional mean.

### View E: ready trends report

State: ready report, Trends selected.

- A metric list selects one primary chart.
- The chart title, unit, and aggregation method are visible together.
- Axis labels use normal human-readable formatting rather than scientific
  notation for ERA5-scale values.
- Hover synchronizes year and value; click pins a year; Escape clears it.
- Summary contains minimum, maximum, mean, first-to-last change, and linear trend.
- Different units are never combined on one axis.
- `ET₀`, `MJ/m²`, Chinese text, and unit symbols render through the packaged font
  fallback.

## Interaction matrix

| Control | Pointer | Keyboard | Result |
|---|---|---|---|
| Lock target | Click | Enter/Space | Freeze requested geometry |
| Year range | Drag/click | Arrow keys, Page Up/Down | Update visible values and draft |
| Start | Click | Enter/Space | Submit frozen draft once |
| Cancel | Click | Enter/Space | Request cancellation and retain older report |
| Report title | Drag | — | Move within viewport |
| Report edge | Drag | — | Resize within min/max bounds |
| Minimize | Click | Enter/Space | Add one report-shelf entry |
| Close | Click | Escape when report is focused | Hide without deleting |
| Map focus | Click | Enter/Space | Explicitly request camera focus |
| Metric | Click | Arrow keys and Enter | Replace the primary chart metric |
| Chart year | Hover/click | Left/Right and Enter | Preview or pin a year |

## State and copy rules

- Draft, target-locked, ready-to-run, queued, fetching, analyzing, ready, failed,
  and cancelled are distinct states.
- `结果就绪` cannot coexist with `可见结果：无` when a report is visible.
- Failures appear beside the failed operation and in the report title state when
  relevant. They do not replace an earlier valid report.
- Red is reserved for failure and deletion. The primary run action is cyan/teal.
- Default product copy does not duplicate every label in Chinese and English.
- Help provides definitions for spatial support, provider cell, aggregation,
  completeness, uncertainty, and resource estimates.
- If a scientific fact is unavailable, display `未提供` rather than inferring it.

## Report information architecture

### Overview

- frozen context map and highlighted target;
- request target and actual provider support;
- source, period, method, completeness, execution timing;
- concise metric summaries;
- explicit `回到实时地图` and `回到分析区域` actions.

### Trends

- metric selector;
- one large unit-aware chart;
- synchronized/pinned year;
- min, max, mean, change, trend;
- missing-value and completeness cues.

### Spatial Range

- requested geometry;
- returned grid center;
- actual coverage bounds and resolution;
- offset between requested and returned center when non-zero;
- context image with the same geometry styling used on the live map.

### Methods & Evidence

- aggregation method and temporal definition;
- variable units and provider resolution;
- missing-data policy and limitations;
- source references, attribution, license, and documentation links;
- artifact identifier, processing version, and export actions.

## Responsive behavior

- At 2048 × 1152, use the default 900 × 680 report.
- At 1440 × 900, preserve at least 32 logical pixels of map around the report.
- At 1024 × 576, use compact mode: 16-pixel map margin, maximum report content
  680 × 500, shortened metadata labels, and the same functional controls.
- The AI composer and report do not overlap. When necessary, the report shifts up
  or the AI transcript collapses; neither control becomes unreachable.
- Every independently scrolling region has a visible scrollbar and supports
  scrolling down and back up after reaching either edge.

## Preservation contract

The redesign must not change:

- Earth camera navigation, selection, orbit, pitch, zoom, or focus semantics;
- terrain height, terrain mesh, elevation scale, material fusion, or imagery;
- Hong Kong 3D Tiles loading, LOD, placement, and ownership;
- photo/video visible-view capture and prompt independence;
- AI agent/tool routing, request preparation, or natural-language semantics;
- AlphaEarth, Sentinel-2, ERA5, or other provider calculations and provenance;
- plugin isolation, science-off behavior, package identity, or bundled resources;
- normal Quit behavior and macOS signing/security settings.

The formal Desktop application remains `/Users/USER/Desktop/osgSol Earth.app`.
Automated implementation does not launch or foreground it. The exact packaged
candidate requires a user-triggered normal Quit with no new matching `.ips`
report before release acceptance.

## Implementation boundary

- The science plugin owns scientific state, jobs, artifacts, and provenance.
- The host owns formal product layout, chart drawing, map projection, report
  window state, and pre-UI map-context capture.
- A versioned bounded UTF-8 snapshot/action protocol crosses the plugin boundary.
- RmlUi is the formal product layer; ImGui remains a legacy/debug fallback until
  packaged manual acceptance passes.
- Point/cell identification is a screen-space UI overlay. Raster scientific
  products may continue using the existing terrain material path; this contract
  does not route point-series highlights into that path.

## Acceptance evidence

Automated evidence:

- workbench reducer and immutable-target tests;
- plugin protocol bounds and ABI fallback tests;
- projection/occlusion/antimeridian tests;
- pointer, wheel, focus, and IME routing tests;
- report lifecycle and chart-formatting tests;
- full non-network regression suite;
- package, dependency, signature, and bundle audit.

Human evidence from the exact candidate:

- Yunnan/Guangxi ERA5 target and actual grid-cell correspondence;
- Shenzhen/Hong Kong orientation and no terrain/z-fighting regression;
- Tokyo/Fuji projection, scroll, drag, resize, minimize, and close;
- AI, photo, video, and 3D smoke checks;
- normal UI Quit with no new matching `.ips` report.
