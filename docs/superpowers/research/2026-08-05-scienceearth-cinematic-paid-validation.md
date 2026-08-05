# ScienceEarth current-view cinematic paid validation — 2026-08-05

## Scope and safety boundary

The user authorized at most 10 paid image requests and 5 paid video requests, with every
video limited to 5 seconds. The ledger closed at exactly those limits: 10 image requests and
5 video requests. No additional generation request is permitted for this validation run.

After the user prohibited foreground executable testing, no further osgSol or packaged app
executable was launched. The remaining paid validation used direct Gemini API calls against
fixed, concrete geographic references. Build and unit-test commands used later in the run do
not create an application window.

## Provider contract verified

- Image generation: `gemini-3.1-flash-image` (Nano Banana 2), reference-image input, 16:9,
  2K request.
- Video generation: `gemini-omni-flash-preview`, Interactions API, `image_to_video`, 16:9,
  exact `duration: "5s"`.
- Product default remains 8 seconds. The request body now transmits duration and aspect ratio
  instead of relying on provider defaults.
- Omni currently returns generated audio by default. Product defaults now request restrained,
  place/era-appropriate ambience and do not expose an untrue guaranteed-silent default.

Official references:

- <https://ai.google.dev/gemini-api/docs/image-generation>
- <https://ai.google.dev/gemini-api/docs/video>
- <https://ai.google.dev/gemini-api/docs/omni>
- <https://ai.google.dev/api/interactions-api>

## Concrete geographic anchors

| Place | Anchor | Reference | Purpose |
|---|---:|---|---|
| Victoria Harbour, Hong Kong | 22.292 N, 114.171 E | EOX Sentinel-2 Cloudless 2024 | Current view, 1920 reconstruction, aerial/orbit/dive |
| Chengjiang, Yunnan | 24.673 N, 102.958 E | EOX Sentinel-2 Cloudless 2024 | Early Cambrian scientific reconstruction |
| Sydney Harbour | -33.857, 151.215 | EOX Sentinel-2 Cloudless 2024; 2009 oblique photo | Independent-location and anime tests |

EOX imagery was used only as non-commercial validation input; source and terms:
<https://cloudless.eox.at/>. The Sydney oblique reference is Andy's
"Aerial view of Sydney Harbour", CC BY-SA 2.0:
<https://commons.wikimedia.org/wiki/File:Aerial_view_of_Sydney_Harbour.jpg>.

The local references and paid outputs are retained in the ignored validation directory
`build/media_paid_validation_20260805/`; they are evidence, not distributable application
assets and are intentionally not committed.

## Image request ledger (10/10)

| # | Scenario | Result | Scientific/product finding |
|---:|---|---|---|
| 1 | Hong Kong, present day | Image | View held broadly, but the model hallucinated Kai Tak as an active runway. Rejected baseline. |
| 2 | Hong Kong, 1920, 19:00 | Image | Geography held; lighting read too much like day and street density was too modern. Rejected baseline. |
| 3 | Chengjiang, Cambrian | Image | Correct high-altitude intent, but red salt-lake/Mars palette overstated certainty. Rejected baseline. |
| 4 | Hong Kong, present, guarded | Image | Active-runway error removed; current-view coverage held. Accepted with generative-detail caveat. |
| 5 | Hong Kong, 1920 twilight, guarded | Image | Twilight and framing improved; uncertain urban history remained over-detailed. Conditional only. |
| 6 | Chengjiang, Cambrian, guarded | Image | Neutral shallow-water/sedimentary palette and high-altitude scale; no enlarged organisms. Accepted as uncertainty-labelled reconstruction. |
| 7 | Sydney, anime, nadir | Image | No Hong Kong contamination, but nadir anime looked like a styled map. Rejected as cinematic default. |
| 8 | Sydney, anime, oblique | Image | Bridge, Opera House and harbour composition remained coherent; no cross-request contamination. Accepted. |
| 9 | Hong Kong, 1920, Kai Tak fact guard | Image | Runway removed, but street density remained too modern. Conditional only; prompt text cannot replace historical source research. |
| 10 | Sydney, independent photoreal | Text-only refusal | Provenance/legal disclaimer inside the generation prompt was over-constraining. Move such disclosure to result labelling where possible. |

Outcome: 9 image files and 1 text-only refusal. A successful HTTP/model response is not by
itself an accepted visual result.

## Video request ledger (5/5, each exactly 5 seconds)

All five outputs are 1280x720, 24 fps, 120 video frames, with a 5.000-second video stream.
Each also contained a non-silent AAC audio track; this is why the product audio default was
made explicit and truthful.

| # | Scenario | Result | Scientific/product finding |
|---:|---|---|---|
| 1 | Hong Kong aerial tour | Pass | Starts from the reference and continuously tilts/advances; no cut or low-altitude jump. Out-of-frame distant detail remains inferential. |
| 2 | Hong Kong 360 orbit | Fail for advertised 360 | Continuous geography, but only roughly one-quarter to one-third orbit completed in 5 seconds. Full 360 must not be promised for a 5-second preview. |
| 3 | Hong Kong dive | Pass | Controlled continuous nadir-to-oblique dive; remained aerial without teleport or altitude cut. |
| 4 | Hong Kong 1920 twilight truck | Conditional | Motion and period treatment stayed coherent and no active runway appeared; historical geometry is still reconstruction, not evidence. |
| 5 | Sydney anime crane | Pass | Stable bridge/Opera House/harbour identity and coherent anime treatment; no Hong Kong carry-over. |

Product rule derived from the failed orbit: the UI now states that a complete 360-degree orbit
needs at least 8–10 seconds and that a 5-second preview may not close. The default remains
8 seconds; acceptance still depends on inspecting the returned motion.

## Code changes driven by evidence

1. Interactions requests now carry explicit video `duration`, `aspect_ratio`, and
   `generation_config.video_config.task = image_to_video`.
2. Present-day prompts forbid restoring demolished features or activating obsolete
   infrastructure solely from a suggestive shape.
3. 1920 prompts limit detail to what the locked altitude and evidence can support, and forbid
   sharpening uncertainty into a modern street grid.
4. Cambrian prompts keep organisms unresolved at high altitude and explicitly label ancient
   colours and geography as inferential rather than pixel-exact.
5. A 19:00 request without season/date is forced toward unmistakable civil twilight rather
   than midday-looking light.
6. Image output must preserve locked coverage and scale rather than crop toward a landmark.
7. Video audio intent is explicit, with restrained ambient sound as the truthful default.

## Remaining hard limits

- Prompting alone cannot make a historically exact 1920 reconstruction. A future historical
  evidence preflight should retrieve dated maps, coastlines, construction timelines and source
  citations before generation.
- Generative out-of-frame video details are not survey truth. The product must label them as
  inferred and must not claim measurement-grade continuity.
- A requested "silent" video is only best-effort until a deterministic mux/post-processing
  path strips the audio stream.
- The one text-only image result proves that model success and media success must remain
  separate states in the UI and job registry.
- These direct-API samples validate provider behaviour and prompt direction. They do not claim
  runtime/manual acceptance of the macOS application.

## Non-window regression evidence

- Science ON: application target compiled; 84/84 offline tests passed.
- Science OFF: application target compiled; 35/35 offline tests passed after the previously
  unbuilt test targets were compiled.
- Explicitly excluded in both configurations: GL runtime smoke tests and the
  `OsgApplication` exit executable. They create a graphics/application context and were not
  appropriate after the user prohibited foreground/executable testing.
- No application executable was run, no packaging or Desktop app replacement was performed,
  and no tag or remote push belongs to this validation change.
