# ScienceEarth processing registry and plugin boundary

ScienceEarth keeps data acquisition and analysis behind typed processing
capabilities. Every existing `IScienceProvider` is registered as a
`built-in-provider` capability. DuckDB Spatial/GeoParquet, PMTiles v3, and
Zarr-family engines are catalogued separately as `native-plugin` capabilities.

## Runtime truth

- Built-in source capabilities are available only when their source descriptor
  is not `Unavailable`.
- The three external engine families default to `available=false` with the
  message `optional native plugin is not loaded`.
- Loading a native module requires ABI v1, bounded metadata, a unique plugin
  identity, typed input formats, an output-kind mask, and working
  create/submit/snapshot/cancel callbacks.
- A loaded module replaces only its matching unavailable catalog slot. It
  cannot replace an active built-in provider or another loaded plugin.
- No DuckDB, PMTiles, Zarr, network client, token, or dataset is bundled by this
  foundation. A concrete engine module remains an independently built and
  distributed component.

This distinction is intentional: the application and AI can explain both what
is supported now and which optional engines are absent without claiming that a
missing engine ran.

## Durable task records

When the product science plugin starts, `ScienceQueryService` enables its
processing ledger under:

`~/Library/Application Support/osgSol Earth/research/processing/`

Automated and developer sessions may isolate both research evidence and
processing records by setting `OSGSOL_SCIENCE_RESEARCH_ROOT`. The product
default remains the directory above.

Each submitted job receives one `osgsol-science-processing-v1` JSON record.
The record is updated atomically and retains:

- capability and live job identity;
- exact pre-submit cost bounds;
- provider state and structured progress;
- cancellation state;
- terminal artifact id and warnings;
- source id, provider version, dataset id, original URL, attribution, and
  acquisition time;
- creation and last-update timestamps.

The record does not contain API keys, bearer tokens, cookies, raw raster bytes,
or mutable artifact content. Scientific evidence remains owned by the existing
immutable evidence store.

## AI and workbench visibility

The workbench snapshot publishes `processing` and `processingCapabilities`.
Science AI tool results publish the same current processing record, while
`search_science_sources` also returns `processing_capabilities`. Optional
engines therefore remain explicitly unavailable until a valid module is
loaded; the model is not asked to infer availability from file extensions.

## ABI v1

The C ABI is declared in `science/ScienceProcessingPluginApi.h`. The host first
reads capability metadata, creates an isolated plugin context, and then uses
bounded UTF-8 JSON requests and results. Result buffers use a size-probe then a
copy, and the host rejects results above 16 MiB. The host owns module lifetime
and destroys the plugin context before unloading its dynamic library.

The offline fake-module test proves module loading, capability replacement,
request submission, bounded result retrieval, cancellation, and missing-module
failure. It does not claim that any of the three production engines is bundled.
