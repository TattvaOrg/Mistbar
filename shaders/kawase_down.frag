in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTex;
uniform vec2 uHalfPixel;

void main() {
    vec4 sum = texture(uTex, vUv) * 4.0;
    sum += texture(uTex, vUv - uHalfPixel.xy);
    sum += texture(uTex, vUv + uHalfPixel.xy);
    sum += texture(uTex, vUv + vec2(uHalfPixel.x, -uHalfPixel.y));
    sum += texture(uTex, vUv - vec2(uHalfPixel.x, -uHalfPixel.y));
    fragColor = sum / 8.0;
}
