in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTex;
uniform int uFlipY;
uniform int uSwapRB;

void main() {
    vec2 uv = uFlipY != 0 ? vec2(vUv.x, 1.0 - vUv.y) : vUv;
    vec4 c = texture(uTex, uv);
    fragColor = uSwapRB != 0 ? vec4(c.b, c.g, c.r, 1.0) : c;
}
