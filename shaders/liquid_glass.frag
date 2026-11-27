in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uBackdrop;
uniform sampler2D uBlurred;
uniform vec2 uResolution;
uniform vec2 uBackdropSize;
uniform vec2 uSurfaceOrigin;

uniform vec4 uRect;
uniform float uRadius;

uniform float uRefraction;
uniform float uDispersion;
uniform float uFrost;
uniform float uSpecular;
uniform vec4 uTintColor;
uniform float uShadowAlpha;
uniform float uIsPill;
uniform float uTime;
uniform vec4 uClick; // (clickX, clickY, ageSec, activeFlag >0.5)
uniform float uTranslucentGlass;
uniform float uCustomOpacity;
uniform float uNoBorder;
uniform float uDirectGlass;
uniform float uBezel;
uniform float uShaderMode; // <= 0.5: mizzle molten liquid, > 0.5: OverShifted LiquidGlass squircle optics

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float sdSquircleBox(vec2 p, vec2 b, float r) {
    if (r <= 1.0) {
        return sdRoundedBox(p, b, r);
    }
    vec2 inner = b - vec2(r);
    vec2 d = max(abs(p) - inner, vec2(0.0));
    // Apple G2 continuous superellipse corner exponent (~3.6) for OverShifted/LiquidGlass silhouette
    const float kPow = 3.6;
    float corner = pow(pow(d.x / r, kPow) + pow(d.y / r, kPow), 1.0 / kPow) * r;
    float inside = min(max(abs(p.x) - b.x, abs(p.y) - b.y), 0.0);
    return (d.x > 0.0 || d.y > 0.0) ? (corner - r) : inside;
}

float evalPanelSdf(vec2 p, vec2 b, float r) {
    return (uShaderMode > 0.5) ? sdSquircleBox(p, b, r) : sdRoundedBox(p, b, r);
}

vec2 calcSdfNormal(vec2 p, vec2 b, float r) {
    const float eps = 0.75;
    float dx = evalPanelSdf(p + vec2(eps, 0.0), b, r) - evalPanelSdf(p - vec2(eps, 0.0), b, r);
    float dy = evalPanelSdf(p + vec2(0.0, eps), b, r) - evalPanelSdf(p - vec2(0.0, eps), b, r);
    vec2 g = vec2(dx, dy);
    float len = length(g);
    return (len > 1e-5) ? (g / len) : vec2(0.0, -1.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Smooth low-frequency liquid surface height field for molten optical texture
float liquidHeight(vec2 px, float t) {
    vec2 q = px * 0.016;
    float h = valueNoise(q + vec2(t * 0.18, -t * 0.12)) * 0.65;
    h += valueNoise(q * 2.15 + vec2(-t * 0.24, t * 0.16)) * 0.35;
    return h;
}

vec2 liquidNormal(vec2 px, float t) {
    const float e = 2.0;
    float hx = liquidHeight(px + vec2(e, 0.0), t) - liquidHeight(px - vec2(e, 0.0), t);
    float hy = liquidHeight(px + vec2(0.0, e), t) - liquidHeight(px - vec2(0.0, e), t);
    return vec2(hx, hy);
}

vec3 sampleGlass(vec2 uv, float frostMix) {
    vec2 clamped = clamp(uv, vec2(0.001), vec2(0.999));
    vec3 sharp = texture(uBackdrop, clamped).rgb;
    vec3 blurred = texture(uBlurred, clamped).rgb;
    return mix(sharp, blurred, frostMix);
}

void main() {
    vec2 px = vec2(gl_FragCoord.x, uResolution.y - gl_FragCoord.y);
    vec2 screenPx = uSurfaceOrigin + px;
    vec2 bdSize = max(uBackdropSize, vec2(1.0));

    vec2 halfSize = uRect.zw * 0.5;
    vec2 center = uRect.xy + halfSize;
    vec2 local = px - center;
    float minDim = min(halfSize.x, halfSize.y);
    float radius = min(uRadius, minDim);

    float sd = evalPanelSdf(local, halfSize, radius);

    // Soft ambient shadow underneath (omitted completely when uShadowAlpha <= 0.001)
    float shadow = 0.0;
    if (uShadowAlpha > 0.001) {
        float shadowDist = evalPanelSdf(local - vec2(0.0, 4.0), halfSize + vec2(0.5), radius + 0.5);
        shadow = (1.0 - smoothstep(-2.0, 22.0, shadowDist)) * uShadowAlpha;
    }

    if (sd > 1.5) {
        if (shadow < 0.002) {
            discard;
        }
        fragColor = vec4(0.0, 0.0, 0.0, shadow);
        return;
    }

    float bodyMask = 1.0 - smoothstep(-0.7, 0.7, sd);
    float depthInside = max(-sd, 0.0);

    vec2 n = calcSdfNormal(local, halfSize, radius);
    float bezelT = clamp(uBezel, 0.0, 1.0);
    float bezelWidth = clamp(minDim * mix(0.14, 0.54, bezelT), 4.0, 24.0);
    float edgeT = clamp(1.0 - depthInside / bezelWidth, 0.0, 1.0);
    float cornerBoost = 1.0 + 0.32 * abs(n.x * n.y) * 2.0;
    float rimCurve = (uShaderMode > 0.5)
        ? (1.0 - pow(1.0 - edgeT, 2.35)) * cornerBoost
        : (pow(edgeT, 1.95) * 0.72 + pow(edgeT, 3.8) * 0.38) * cornerBoost;

    vec2 baseUv = vec2(screenPx.x / bdSize.x, 1.0 - screenPx.y / bdSize.y);
    vec2 pxToUv = 1.0 / bdSize;

    // Surface ripple in mizzle mode; static crisp spherical meniscus in OverShifted LiquidGlass mode
    vec2 flowN = (uShaderMode > 0.5) ? vec2(0.0) : liquidNormal(screenPx, uTime);
    vec2 liquidTextureOffset = flowN * (5.2 * uRefraction) * pxToUv;

    float domeFactor = (uShaderMode > 0.5) ? 0.028 : 0.019;
    float rimFactor  = (uShaderMode > 0.5) ? 32.0  : 24.0;
    vec2 centerLens = -local * pxToUv * (domeFactor * uRefraction);
    vec2 rimBend = -n * rimCurve * (rimFactor * uRefraction) * pxToUv;

    // Subtle macOS-style click pulse ONLY on click inside this panel (zero hover effect)
    float clickFlash = 0.0;
    vec2 clickRefract = vec2(0.0);
    if (uClick.w > 0.5 && uClick.z >= 0.0 && uClick.z < 0.35 && sd < 0.0) {
        vec2 clickLocal = uClick.xy - center;
        if (evalPanelSdf(clickLocal, halfSize, radius) <= 2.0) {
            float tClick = uClick.z / 0.35;
            float env = sin(tClick * 3.14159) * (1.0 - tClick);
            float distC = length(px - uClick.xy);
            float ring = exp(-abs(distC - tClick * 140.0) * 0.06) * env;
            vec2 dirC = (distC > 0.5) ? ((px - uClick.xy) / distC) : vec2(0.0);
            clickRefract = dirC * ring * (3.5 * uRefraction) * pxToUv;
            clickFlash = env * 0.07;
        }
    }

    vec3 refracted;
    vec3 edgeLight = vec3(0.0);
    if (uDirectGlass > 0.5) {
        // Direct compositor-translucent glass: zero static blurred desktop/wallpaper baked inside.
        float meniscusShade = rimCurve * (uShaderMode > 0.5 ? 0.070 : 0.045) * uRefraction;
        vec2 liquidShift = liquidTextureOffset * bdSize * 0.008;
        refracted = vec3(0.11, 0.14, 0.19) + vec3(meniscusShade + liquidShift.x - liquidShift.y);
    } else {
        vec2 refractedUv = baseUv + centerLens + rimBend + liquidTextureOffset + clickRefract;

        // 5-tap spectral dispersion concentrated along the curved bezel & corners
        float dispMul = (uShaderMode > 0.5) ? 4.8 : 3.4;
        vec2 dispStep = n * pow(edgeT, 1.75) * cornerBoost * (dispMul * uDispersion) * pxToUv;
        float localFrost = clamp(uFrost + rimCurve * (uShaderMode > 0.5 ? 0.06 : 0.10), 0.0, 1.0);

        vec3 c0 = sampleGlass(refractedUv + dispStep * 1.0, localFrost);
        vec3 c1 = sampleGlass(refractedUv + dispStep * 0.5, localFrost);
        vec3 c2 = sampleGlass(refractedUv, localFrost);
        vec3 c3 = sampleGlass(refractedUv - dispStep * 0.5, localFrost);
        vec3 c4 = sampleGlass(refractedUv - dispStep * 1.0, localFrost);

        refracted = vec3(
            c0.r * 0.55 + c1.r * 0.45,
            c1.g * 0.25 + c2.g * 0.50 + c3.g * 0.25,
            c3.b * 0.45 + c4.b * 0.55
        );

        vec2 outerSampleUv = clamp(baseUv + n * 7.0 * pxToUv, vec2(0.001), vec2(0.999));
        edgeLight = texture(uBlurred, outerSampleUv).rgb;
    }

    float luma = dot(refracted, vec3(0.2126, 0.7152, 0.0722));
    vec3 vibrant = mix(vec3(luma), refracted, (uShaderMode > 0.5) ? 1.24 : 1.18);

    // Airy glass tint + liquid/Beer-Lambert optical sheen
    float vertSheen = smoothstep(halfSize.y, -halfSize.y, local.y);
    float wetWaveSheen = clamp(dot(normalize(vec3(flowN * 2.2, 1.0)), normalize(vec3(-0.4, -0.5, 1.0))) - 0.86, 0.0, 0.14) * 0.42;
    float edgeTintBoost = (uShaderMode > 0.5) ? (uTintColor.a * (1.0 + rimCurve * 0.45)) : uTintColor.a;
    vec3 glassColor = mix(vibrant, uTintColor.rgb, clamp(edgeTintBoost, 0.0, 0.92));
    glassColor += vec3(0.038 * vertSheen - 0.012 * (1.0 - vertSheen) + wetWaveSheen + clickFlash) * (1.0 - uFrost * 0.35);

    // Minimal dithering grain
    float grain = (hash12(px) - 0.5) * mix(0.004, 0.010, uFrost);
    glassColor += vec3(grain);

    float edgeLightLuma = dot(edgeLight, vec3(0.2126, 0.7152, 0.0722));

    // Classic slim Liquid Glass diagonal specular shine
    vec2 lightDir = normalize(vec2(-0.68, -0.73));
    float topLeftGlint = pow(max(dot(n, lightDir), 0.0), 2.2);
    float botRightGlint = pow(max(dot(n, -lightDir), 0.0), 2.6) * 0.74;
    float diagShine = (topLeftGlint + botRightGlint) * (0.95 + 0.25 * abs(n.x * n.y) * 2.0);

    float innerKiss = pow(edgeT, (uShaderMode > 0.5) ? 2.4 : 2.9) * smoothstep(0.0, 1.2, depthInside);
    vec3 causticTint = (uDirectGlass > 0.5)
        ? vec3(0.94, 0.97, 1.0)
        : mix(vec3(0.94, 0.97, 1.0), edgeLight * 1.38 + vec3(0.24), clamp(edgeLightLuma * 0.95, 0.0, 0.70));
    float rimScale = (1.0 - clamp(uNoBorder, 0.0, 1.0));
    glassColor += causticTint * innerKiss * (0.04 + 0.12 * diagShine) * uSpecular * rimScale;

    float razorRim = smoothstep(1.0, 0.0, depthInside) * rimScale;
    float rimAlpha = razorRim * (0.05 + 0.22 * diagShine) * uSpecular;
    glassColor = mix(glassColor, vec3(0.97, 0.99, 1.0), clamp(rimAlpha, 0.0, 0.28));

    float baseTranslucent = (uCustomOpacity >= 0.0)
        ? uCustomOpacity
        : clamp(0.32 + uFrost * 0.34 + uTintColor.a * 0.62 + rimCurve * 0.22 + razorRim * 0.25, 0.26, 0.90);
    float panelAlpha = mix(1.0, baseTranslucent, clamp(uTranslucentGlass, 0.0, 1.0));

    vec3 clampedGlass = clamp(glassColor, 0.0, 1.0) * panelAlpha;
    vec3 outRgb = mix(vec3(0.0), clampedGlass, bodyMask);
    float outAlpha = bodyMask * panelAlpha + shadow * (1.0 - bodyMask);
    fragColor = vec4(outRgb, outAlpha);
}
