# ScienceEarth scientific workbench manual acceptance

Date: 2026-07-24

Candidate: `/Users/USER/Desktop/osgSol Earth.app`

Automated implementation never launches or foregrounds this application. The
checks below must be performed by the user on this exact fixed-path candidate
before it can be tagged or synchronized as accepted.

## Candidate identity

- Product: osgSol Earth
- Version: 0.6.1
- ScienceEarth phase: G3.1
- Channel: ui-ux-goal-candidate
- Source commit: `011f00994e5aded2ee1b5833927c8770527cde6c`
- Provenance: recorded in `Contents/Info.plist` and
  `Contents/Resources/package-audit.env`
- Automated gates: package contract, bundle closure, provenance, data manifest,
  staging strict/deep signature, G0-v2 bundle audit, Rml document parsing, and
  82/82 offline regression suite and 43/43 bundle-audit unit suite. The UI gate
  also composes the production ImGui shell with the real RmlUi Science
  workbench/report in the same offscreen frame at 1024×576, 1280×720, and
  1440×900, verifies the shared workbench/Shell safe regions at all three sizes,
  then at 1440×900 exercises the menu, year input, bidirectional wheel
  scrolling, run action, report safe region, and axis-bearing trend report.
  The composite frame now executes the real `AIChatUI` rather than a command-bar
  surrogate and rejects overlap against the actual ImGui command window.
  The interaction gate also selects every Science data source and target
  operation, then clicks report tabs, metric, copy, overflow, delete
  cancel/confirm, minimize/reopen, close, and target focus. The native AI gate
  clicks history and templates, checks the template popup against the real
  command window, and renders the video confirmation surface. The production
  card gate also renders bar/line/donut/stat charts, photo/video results,
  generation progress, the event stream/status strip, the single maximum-detail
  notice, and module help; it clicks or scrolls every applicable surface.
- Desktop inventory: exactly one `osgSol Earth.app`; no hidden staging bundle.
  A FileProvider-restored old `osgSol Earth 2.app` was archived outside Desktop
  without launching or altering either package.
- Automated launch count: zero
- Desktop executable bytes and normal signature verification: valid. Desktop
  FileProvider subsequently adds `com.apple.FinderInfo` inside the App, so the
  Desktop-path strict check reports that external metadata. No xattr, security
  setting, or signature was modified to hide it.

## Visual and interaction matrix

- [ ] Launch opens one application instance and preserves the existing Earth,
  module rail, top bar, AI command area, camera controls, and normal imagery.
- [ ] Opening `科学` shows one 340–380 px composer beside the permanent module
  rail. It does not cover the rail, top bar, map command area, or bottom AI area.
- [ ] The composer scroll bar is visible when content overflows, and the mouse
  wheel can scroll both down and back up after reaching either edge.
- [ ] Short module and AI panels do not show a decorative scroll bar when
  nothing overflows. Long content shows one continuous in-panel scroll bar.
- [ ] Select `ERA5 agricultural climate`, use the `位置操作` menu to lock the
  map center, and verify the
  exact coordinates and the message `镜头移动不会改变已锁定范围`.
- [ ] Moving the camera after locking does not silently change the target.
  `回到分析区域` is the only control in this flow that moves the camera.
- [ ] Set a valid year range and run once. The CTA remains in place, changes to
  progress/cancel, and does not freeze map navigation or Quit.
- [ ] The live map shows the requested target in cyan and provider-returned
  coverage in warm yellow. These are screen-space marks only: terrain height,
  terrain mesh, Hong Kong 3D Tiles, and ground imagery do not flicker, deform,
  sink, or acquire new patches.
- [ ] A ready result opens one centered report that can move, resize, minimize,
  reopen, close without deletion, and explicitly delete only after confirmation.
- [ ] The Overview map snapshot matches the analyzed place and independently
  marks requested and actual coverage. `回到实时地图中的分析区域` is explicit.
- [ ] Time Trends shows one main chart. Metric changes replace that chart; units
  are not mixed; missing years break the line; values use normal formatting;
  `ET₀`, `MJ/m²`, and Chinese text render correctly.
- [ ] Clicking a chart year pins that year and shows its value or `数据缺测`.
- [ ] Spatial Range clearly distinguishes the requested point/area from actual
  provider coverage and never describes a grid-cell point series as a regional
  average.
- [ ] Methods & Evidence shows only recorded facts: aggregation, missing-data
  rule, processing steps, source/version/link/license, limitations, warnings,
  artifact ID, timing, and export capabilities. Missing facts say `未提供`.
- [ ] Switching away from the Science module hides the composer/report overlay;
  returning restores the scientific workflow without losing the result.
- [ ] Existing AlphaEarth, Sentinel-2, terrain, 3D city, satellite, AI research,
  photo/video visible-view capture, and natural-language tools remain usable.
- [ ] Open every left-rail module once: Explore, Layers, Science, Live,
  Satellites, 3D, Tasks, and Settings. The drawer remains flush with the rail,
  uses the same dark palette, and no edge, button, label, or scrollbar is
  clipped.
- [ ] At normal Retina size and at the smallest practical window, the top
  actions, AI command row, context strip, Insight Lens tabs, object cards,
  warnings, menus, dialogs, and close/cancel actions remain reachable.
- [ ] In the default collapsed AI state, title/status/history/template form one
  compact tool header at normal width and a deliberate two-line header at
  compact width; the AI bar never covers the scientific report.
- [ ] Open AI history, analysis templates, and video confirmation once. The
  template panel stays entirely above the command bar, and the photo/video
  labels and confirmation actions are complete and clickable.
- [ ] Resize the window smaller and then larger once. The Science composer and
  report reflow inside the viewport; no old 720 px panel remains outside it.
- [ ] Tab advances through visible native and Science controls in task order;
  pointer clicks still select every left-rail module and clicking the current
  module again collapses it.

## Normal Quit gate

- [ ] Before launching, note the newest matching crash report timestamp in
  `~/Library/Logs/DiagnosticReports`.
- [ ] Quit this exact candidate using the in-app normal Quit control.
- [ ] The app exits once, does not hang, and macOS does not show an unexpected
  quit/error dialog.
- [ ] No new matching `.ips` report appears after the recorded timestamp.

Acceptance remains pending until all unchecked items above are confirmed on the
exact candidate. Do not tag, push, synchronize, or publish it before that
confirmation.
