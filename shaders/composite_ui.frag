in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uUiTex;

void main() {
    // Cairo ARGB32 on little-endian is premultiplied BGRA in memory;
    // uploaded as GL_RGBA, channels read as (B, G, R, A).
    vec2 uv = vec2(vUv.x, 1.0 - vUv.y);
    vec4 bgra = texture(uUiTex, uv);
    fragColor = vec4(bgra.b, bgra.g, bgra.r, bgra.a);
}
