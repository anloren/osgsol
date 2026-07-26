# osgSol Earth AI workspace context

## Purpose

The AI assistant must interpret the Earth workspace the user is actually
operating. A request such as “解读当前报告” must not degrade into a coordinate-only
answer or ask the user to transcribe a visible chart.

This increment establishes one process-local, structured context path:

```text
Earth shell + camera + layers + selections
                         \
Science workbench + active report + evidence
                           -> EarthContextHub
Tool and data-source catalog /
                              -> automatic per-turn snapshot
                              -> get_earth_context on demand
                              -> AIChatCore -> model
```

The application does not send screen pixels or perform OCR. It sends the
authoritative state already owned by the renderer, panels, query service and
science plugin.

## Published sections

### `workspace`

- active product module and drawer state;
- camera eye and target latitude, longitude and altitude;
- render state;
- every registered layer, enabled state and opacity;
- active layer preset;
- selected flight, ship, satellite or feed feature.

### `scienceWorkbench`

The ScienceEarth plugin publishes its existing workbench snapshot, including:

- selected source, analysis method, metric and year range;
- requested and actual spatial coverage;
- current phase, progress and cost estimate;
- complete science source catalog;
- active artifact identifier and series values;
- report evidence, units, native resolution and spatial support;
- provenance, citations, quality notes, warnings and limitations;
- export capabilities.

### `capabilities`

The live AI tool registry is exposed as a catalog of tool names and
descriptions. This lets the model discover which backend data sources and
cross-source actions are currently available without hard-coding a second,
stale capability list.

## Delivery contract

Each accepted user turn receives a fresh `EARTH_CONTEXT_V1` envelope before the
raw user request. The automatic snapshot is bounded to 120 KiB. High-priority
workspace and report sections are retained first. If a section cannot fit, its
status remains visible as `omitted_oversize`, and the model can call
`get_earth_context(section=...)` for a bounded full section.

The snapshot remains attached throughout the current function-call loop. Before
the next user turn it is removed from conversation history and replaced by the
original user text. This prevents stale “current report” snapshots from
accumulating, avoids contradictory report revisions and bounds token growth.

Context collection failures never block ordinary chat. The envelope reports
`context_unavailable`, `context_invalid` or `context_oversize`, and the user
request still proceeds.

## Trust boundary

Runtime values, provider metadata, URLs, citations and source text are evidence,
not instructions. The system prompt and context envelope both state this
boundary. The model must preserve units, coverage, provenance, warnings and
limitations, and must not invent visual facts absent from structured context.

The context path excludes:

- API keys, credentials and payment/contact identifiers;
- image buffers, screenshots and base64 payloads;
- arbitrary local files;
- instructions embedded in provider metadata.

## User-visible state

The chat header displays `上下文已连接`. Its tooltip explains that the assistant
receives the current module, map state, layers, selections and scientific
report as structured data, not screen pixels.

## Verification cases

1. Open or generate a ScienceEarth report, then ask:
   `解读当前报告，指出最明显的年份变化，并给出单位、来源和局限。`
2. Change the selected report or year range and ask:
   `按我现在的设置重新解释；不要沿用上一份报告。`
3. Select a map object and ask:
   `结合当前选中对象、已打开图层和可用数据源做交叉研究。`
4. Confirm that the response names the active artifact/source, uses the current
   series values and units, and cites report evidence without claiming to see
   screen pixels.
