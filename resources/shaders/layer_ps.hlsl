// Layer pixel shader: planar/semi-planar YCbCr or BGRA input, colour
// conversion, basic colour correction and a few whole-frame looks. Output is
// premultiplied alpha so layers composite with One / InvSrcAlpha.
cbuffer LayerConstants : register(b0) {
    float4x4 transform;
    float4   uvRect;
    float4   csRow0;
    float4   csRow1;
    float4   csRow2;
    float4   adjust0;     // opacity, exposure, brightness, contrast
    float4   adjust1;     // saturation, temperature, tint, highlights
    float4   adjust2;     // shadows, gamma, vibrance, hue
    int4     mode;        // x: 0 = NV12 (t0=Y R8, t1=UV RG8), 1 = I420 (t0,t1,t2 R8), 2 = BGRA (t0)
                          // y: flags  1 grayscale, 2 sepia, 4 vignette, 8 input premultiplied, 16 skip colour math
    float4   misc;        // vignetteAmount, texelW, texelH
};

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
SamplerState linearClamp : register(s0);

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

float3 hueRotate(float3 c, float degrees) {
    float a = radians(degrees);
    float cs = cos(a), sn = sin(a);
    // Rodrigues rotation around the grey axis (1,1,1)/sqrt(3)
    float3 k = float3(0.57735, 0.57735, 0.57735);
    return c * cs + cross(k, c) * sn + k * dot(k, c) * (1.0 - cs);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    // Outside the crop rectangle: transparent.
    if (uv.x < uvRect.x || uv.x > uvRect.z || uv.y < uvRect.y || uv.y > uvRect.w) return float4(0, 0, 0, 0);

    float3 rgb;
    float alpha = 1.0;
    int planeMode = mode.x;
    int flags = mode.y;
    if (planeMode == 2) {
        float4 c = tex0.Sample(linearClamp, uv);
        alpha = c.a;
        rgb = (flags & 8) ? (alpha > 0.0 ? c.rgb / alpha : c.rgb) : c.rgb;   // un-premultiply for colour math
    } else {
        float y = tex0.Sample(linearClamp, uv).r;
        float2 cbcr;
        if (planeMode == 0) cbcr = tex1.Sample(linearClamp, uv).rg;
        else cbcr = float2(tex1.Sample(linearClamp, uv).r, tex2.Sample(linearClamp, uv).r);
        float3 ycc = float3(y, cbcr.x, cbcr.y);
        rgb.r = dot(csRow0.xyz, ycc) + csRow0.w;
        rgb.g = dot(csRow1.xyz, ycc) + csRow1.w;
        rgb.b = dot(csRow2.xyz, ycc) + csRow2.w;
    }

    if (!(flags & 16)) {
        float exposure = adjust0.y, brightness = adjust0.z, contrast = adjust0.w;
        float saturation = adjust1.x, temperature = adjust1.y, tint = adjust1.z, highlights = adjust1.w;
        float shadows = adjust2.x, gamma = adjust2.y, vibrance = adjust2.z, hue = adjust2.w;

        rgb = saturate(rgb);
        // Exposure (stops) and brightness (offset)
        rgb *= exp2(exposure);
        rgb += brightness;
        // Contrast around mid grey
        rgb = (rgb - 0.5) * contrast + 0.5;
        // Temperature (warm/cool) and tint (green/magenta), gentle white-balance style shifts
        rgb.r += temperature * 0.10;
        rgb.b -= temperature * 0.10;
        rgb.g += tint * 0.10;
        // Shadows / highlights: weighted lifts and gains by luminance
        float l = dot(saturate(rgb), kLuma);
        float shadowW = 1.0 - smoothstep(0.0, 0.5, l);
        float highW = smoothstep(0.5, 1.0, l);
        rgb += shadows * 0.5 * shadowW;
        rgb += highlights * 0.5 * highW;
        // Gamma
        if (gamma != 1.0) rgb = pow(saturate(rgb), 1.0 / max(gamma, 0.01));
        // Saturation and vibrance (vibrance boosts less-saturated pixels more)
        float lum = dot(rgb, kLuma);
        float sat = saturation;
        if (vibrance != 0.0) {
            float maxc = max(rgb.r, max(rgb.g, rgb.b));
            float minc = min(rgb.r, min(rgb.g, rgb.b));
            float pixelSat = maxc - minc;
            sat += vibrance * (1.0 - pixelSat);
        }
        rgb = lerp(float3(lum, lum, lum), rgb, sat);
        if (hue != 0.0) rgb = hueRotate(rgb, hue);
        rgb = saturate(rgb);

        if (flags & 1) { float g = dot(rgb, kLuma); rgb = float3(g, g, g); }
        if (flags & 2) {
            float3 s = float3(dot(rgb, float3(0.393, 0.769, 0.189)), dot(rgb, float3(0.349, 0.686, 0.168)), dot(rgb, float3(0.272, 0.534, 0.131)));
            rgb = saturate(s);
        }
        if (flags & 4) {
            float2 d = (uv - 0.5) * 2.0;
            float v = 1.0 - misc.x * smoothstep(0.4, 1.4, dot(d, d));
            rgb *= v;
        }
    }

    float opacity = adjust0.x;
    float a = alpha * opacity;
    return float4(rgb * a, a);
}
