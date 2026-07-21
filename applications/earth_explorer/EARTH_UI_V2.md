# EarthUI v2 — Carbon Spectrum

EarthUI v2 is the stable application shell for osgSol Earth and ScienceEarth.
It keeps the 3D world as the primary surface and organizes controls by task,
instead of putting every feature into one long inspector.

## Visual system

- Carbon `#07090A`: primary low-glare surface.
- Iron `#0D1011`: controls and secondary surfaces.
- Raised iron `#141719`: hover and nested regions.
- Oxblood `#380F0C`: selected and hover state.
- Vermilion `#D33123`: primary actions, progress, and urgent state.
- Science cyan `#38C3DF`: measured data, charts, selected map evidence.
- Geometry is compact and editorial: 2–3 px radii, thin borders, no white
  canvases and no generic dark-blue glass.

## Stable shell

1. Top bar: product, active module, camera position, AI/science health.
2. Left rail: one icon per primary module.
3. Module drawer: only the controls for the selected module; it always scrolls.
4. Map canvas: the largest region and the source of geographic context.
5. Insight Lens: selected-object details, scientific results, charts, evidence.
6. AI Command Deck: compact natural-language input; history expands upward.
7. Context tray: controls that belong to the active data type.
8. Status strip: optional UTC, feed health, and active preset.

The drawer and Insight Lens end above the command deck. The ScienceEarth result
view owns the Insight Lens while the Science module is active, so generic cards
cannot cover its results.

## Module information model

| Module | Drawer | Context tray | Insight Lens |
| --- | --- | --- | --- |
| Explore | camera, sun, render, go-to, bookmarks | view/camera actions | selected world object |
| Layers | searchable full layer catalog | visibility, legend, opacity | selected feature/source |
| Science | science sources plus the six-step research flow | source-defined time/cloud/static controls | results, professional charts, evidence, limitations |
| Live | live/weather layers | time window, refresh, last update | current event and source health |
| Satellites | satellite layers | orbit, pass, track, footprint | selected spacecraft |
| 3D City | 3D city layers | object, LOD, height/material relation | selected 3D object |
| Tasks | AI state, event stream, status strip | running, queue, history | task output |
| Settings | application configuration and normal Quit | none | none |

## Interaction rules

- Controls are data-specific. A year control is shown only when the selected
  source is temporal; a static DEM never inherits AlphaEarth year controls.
- Permanent scientific explanation is hidden behind contextual help and
  evidence sections. The primary workflow shows only what is needed now.
- Loading, empty, degraded, failed, cancelled, and ready are distinct states.
  Errors stay in their module/result surface and never multiply across the map.
- Scientific charts show recorded values only. EarthUI v2 does not fabricate
  confidence bands, physical meanings, or causal explanations.
- The new shell changes presentation and routing only. Existing camera,
  layer, science, AI, media, package, and normal-Quit paths remain the owners of
  their behavior.

## Validation boundary

Automated checks cover layout invariants, module/context routing, chart wiring,
compilation, and existing functional contracts. Visual comparison requires the
user to launch the fixed Desktop package and return a screenshot; automated
validation must not launch or foreground the macOS app.
