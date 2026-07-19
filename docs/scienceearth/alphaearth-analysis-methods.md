# ScienceEarth AlphaEarth analysis methods

## Scientific contract

AlphaEarth Foundations publishes annual 64-component embedding vectors rather than named
physical variables. Individual components must not be relabeled as vegetation, temperature,
urbanization, elevation, or another physical quantity without an independently validated
downstream model.

The model paper places each embedding on a 64-dimensional unit sphere. Its unsupervised change
evaluation L2-normalizes the before/after vectors and compares their dot product. Consequently,
cosine distance, angular distance, and Euclidean distance are coupled scales for unit vectors;
showing several of them is useful for inspection, but does not create independent evidence.

## Implemented tools

- False-color context: A01/A16/A09 is loaded for the comparison year before any point or regional
  analysis starts. Every indexed tile intersecting the frozen scope is reprojected onto one WGS84
  mosaic; incomplete mosaics are rejected rather than published as clipped rectangles. It is not
  natural-color imagery.
- Point baseline trajectory: compares every selected year with the first selected year using
  direction change, vector displacement, or both.
- Largest adjacent-year interval: reports the strongest consecutive valid-year step separately
  for every selected metric.
- Regional change map: uses one explicitly selected metric so the heatmap method is never chosen
  implicitly.
- Relative hotspots: P90, P95, or P99 selects the highest local fraction of change values in the
  current result. This is a within-result quantile, not a physical threshold.
- Local PCA: summarizes dominant mathematical directions in the selected regional sample.
- Spherical clustering: produces deterministic unlabeled embedding groups. Clusters are not land
  cover classes.

## Interpretation and verification

An embedding change detects a difference in the learned multi-source representation; it does not
identify the cause. Soil moisture, flooding, biomass, construction, agriculture, atmospheric
conditions, or other signals may contribute. Findings must be checked against interpretable
evidence such as year-matched optical imagery, field observations, or a validated thematic data
product.

Supervised classification and regression are deliberately not exposed as generic buttons. They
require a declared target, representative labeled samples, spatially separated validation data,
and reported accuracy or error. They can be added as research workflows once those inputs exist.

## Primary references

- Brown et al., *AlphaEarth Foundations: An embedding field model for accurate and efficient
  global mapping from sparse label data*, sections 7 and S18.1.
- Google Earth, *Detect change (Experimental)*, especially the visual-verification guidance.
- Google DeepMind, *AlphaEarth Foundations helps map our planet in unprecedented detail*.
