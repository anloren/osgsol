# osgSol Earth UI modernization decision

Date: 2026-07-20

## Decision

Keep Dear ImGui as the debug/developer UI and as the low-risk bridge for the
current release. Start the product-UI migration with a bounded RmlUi prototype
for the AI chat and ScienceEarth task flow. Do not replace the Earth renderer,
camera, science services, AI agent, or data-source architecture.

## Why the current UI feels old

The application currently expresses almost every state as an ImGui window,
tree, small button, or vertically stacked control. The AI composer, transcript,
template gallery, Earth controls, and science result cards therefore compete for
the same map pixels. Dear ImGui itself describes its primary audience as content
creation, visualization, and debug tools rather than ordinary end-user UI, and
does not provide full accessibility support.

Current source: <https://github.com/ocornut/imgui>

## Framework comparison

### RmlUi — recommended product layer

- C++ library with HTML/CSS-like documents and a DOM/event model.
- Produces vertices, indices, textures, and draw commands for an application-
  supplied renderer, which fits the existing OpenGL/OSG frame loop.
- Cross-platform, including macOS, with a lightweight core and MIT license.
- Allows Chat and ScienceEarth workflows to migrate one surface at a time.

Official source: <https://github.com/mikke89/RmlUi>

### Qt Quick/QML — capable but heavier integration

Qt Quick has strong declarative layout and C++ integration, but the clean
architecture requires exposing a QObject-based model to QML. It also introduces
a larger UI runtime and packaging/licensing review. It is suitable if osgSol
Earth later becomes a conventional desktop suite, but it is not the lowest-risk
overlay migration for the current fullscreen OSG application.

Official source: <https://doc.qt.io/qt-6/qtqml-cppintegration-overview.html>

### Slint — promising, not the first choice here

Slint supports ahead-of-time compiled declarative UI and C++ CMake integration,
but adopting it would still introduce a new backend/renderer boundary and a
separate licensing decision. It remains a viable comparison candidate for a
small proof of concept, not the default migration path.

Official sources:
<https://docs.slint.dev/latest/docs/cpp/>,
<https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/>

## Safe migration boundary

1. Keep all AI tools, science jobs, source adapters, camera behavior, and raster
   rendering in their existing C++ services.
2. Extract a UI-facing state model for chat entries, task status, prompt presets,
   research progress, and result cards. The UI must not own scientific work.
3. Prototype only the AI composer, transcript drawer, and analysis-template
   chooser in RmlUi over the existing OpenGL context.
4. Keep the ImGui version available as a debug fallback until the RmlUi path
   passes the same functional and manual checks.
5. Move the ScienceEarth workflow and remaining end-user panels only after the
   Chat prototype is accepted. Do not migrate debug/diagnostic controls merely
   for visual consistency.

## Immediate bridge implemented in the current release

- Chat defaults to a compact composer instead of an open transcript panel.
- History opens only on demand.
- Analysis templates open in a separate bounded popup instead of increasing the
  height of the bottom bar.
- Location templates prepare the camera, years, sources, and analysis intent;
  current-view templates explicitly retain the camera.
- Template preparation never auto-submits. The user reviews the populated prompt
  and presses a visible Send button.

## Acceptance gates for the RmlUi prototype

- No change to Earth rendering, camera math, scientific geometry, or AI tool
  contracts.
- Chat occupies no more than the compact composer footprint when idle.
- Keyboard/IME entry, scrolling, focus, and explicit Send behavior work on macOS.
- Long scientific tasks expose queued/running/partial/failed/ready states without
  freezing or blocking Quit.
- The packaged app remains self-contained and the new UI dependency has an
  auditable license and package-size delta.

## 2026-07-22 ScienceEarth workbench decision

The first formal RmlUi product surface is now the ScienceEarth analysis
workbench. The accepted product contract is
[`design/science-workbench-contract-2026-07-22.md`](design/science-workbench-contract-2026-07-22.md).

The Science drawer becomes one connected analysis composer. Requested and
provider-returned geometry are displayed on the live map. Scientific results
open in a central movable, resizable, minimizable report with Overview, Trends,
Spatial Range, and Methods & Evidence sections. The existing AI/chat migration
remains planned, but it does not block the ScienceEarth workbench.

The host owns RmlUi, input, layout, map projection, chart rendering, and context
capture. The science plugin owns query state, execution, artifacts, and
provenance. A bounded versioned snapshot/action protocol joins them. ImGui and
the current ABI remain the rollback path until packaged manual acceptance.
