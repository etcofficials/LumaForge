// Full quad from SV_VertexID (triangle strip, 4 vertices), transformed by a
// per-layer matrix into clip space. UVs cover the layer's crop rectangle.
cbuffer LayerConstants : register(b0) {
    float4x4 transform;   // layer-space quad (-1..1) -> clip space
    float4   uvRect;      // minU, minV, maxU, maxV (crop)
    float4   csRow0;      // YCbCr -> RGB matrix rows (xyz) + offsets in w
    float4   csRow1;
    float4   csRow2;
    float4   adjust0;     // opacity, exposure, brightness, contrast
    float4   adjust1;     // saturation, temperature, tint, highlights
    float4   adjust2;     // shadows, gamma, vibrance, hue (degrees)
    int4     mode;        // planeMode, flags, flipBits, unused
    float4   misc;        // vignetteAmount, texelW, texelH, unused
    float4   fx;          // grayscale, sepia, blur, sharpen
};

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID) {
    // 0:(-1,-1) 1:(-1,1) 2:(1,-1) 3:(1,1) in layer space; v flipped for texture rows.
    float2 corner = float2((id & 2) ? 1.0 : -1.0, (id & 1) ? 1.0 : -1.0);
    VSOut o;
    o.pos = mul(transform, float4(corner, 0.0, 1.0));
    float2 t = corner * 0.5 + 0.5;          // 0..1
    t.y = 1.0 - t.y;                        // texture origin is top-left
    if (mode.z & 1) t.x = 1.0 - t.x;        // flip horizontal
    if (mode.z & 2) t.y = 1.0 - t.y;        // flip vertical
    o.uv = lerp(uvRect.xy, uvRect.zw, t);
    return o;
}
