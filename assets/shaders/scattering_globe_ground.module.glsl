vec4 composeGroundLayers(vec4 groundColor, vec2 tileUv)
{
    vec4 layerColor = VERSE_TEX2D(
        ExtraLayerSampler, tileUv * UvOffset3.zw + UvOffset3.xy);
    groundColor.rgb = mix(
        groundColor.rgb, layerColor.rgb,
        layerColor.a * clamp(LabelOpacity, 0.0, 1.0));
    vec4 overlay2Color = VERSE_TEX2D(
        Overlay2Sampler, tileUv * UvOffset4.zw + UvOffset4.xy);
    groundColor.rgb = mix(
        groundColor.rgb, overlay2Color.rgb,
        overlay2Color.a * clamp(Overlay2Opacity, 0.0, 1.0));
    return groundColor;
}
