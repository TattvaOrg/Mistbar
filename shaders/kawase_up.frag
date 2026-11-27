in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTex;
uniform vec2 uHalfPixel;

void main() {
    vec4 sum = texture(uTex, vUv + vec2(-uHalfPixel.x * 2.0, 0.0));
    sum += texture(uTex, vUv + vec2(-uHalfPixel.x, uHalfPixel.y)) * 2.0;
    sum += texture(uTex, vUv + vec2(0.0, uHalfPixel.y * 2.0));
    sum += texture(uTex, vUv + vec2(uHalfPixel.x, uHalfPixel.y)) * 2.0;
    sum += texture(uTex, vUv + vec2(uHalfPixel.x * 2.0, 0.0));
    sum += texture(uTex, vUv + vec2(uHalfPixel.x, -uHalfPixel.y)) * 2.0;
    sum += texture(uTex, vUv + vec2(0.0, -uHalfPixel.y * 2.0));
    sum += texture(uTex, vUv + vec2(-uHalfPixel.x, -uHalfPixel.y)) * 2.0;
    fragColor = sum / 12.0;
}
