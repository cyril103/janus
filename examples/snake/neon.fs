#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 resolution;
uniform float time;

out vec4 finalColor;

void main()
{
    vec2 uv = fragTexCoord;
    vec2 texel = 1.0 / resolution;

    // The input texture contains only the snake on a transparent background.
    float pulse = 0.65 + 0.35 * sin(time * 2.4);
    vec2 split = vec2((1.0 + pulse) * texel.x, 0.0);
    vec4 center = texture(texture0, uv);
    vec3 core;
    core.r = texture(texture0, uv + split).r;
    core.g = center.g;
    core.b = texture(texture0, uv - split).b;

    // Spread the snake color and alpha into a soft transparent halo.
    vec3 glow = vec3(0.0);
    float glowAlpha = 0.0;
    float weight = 0.0;
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            float distanceWeight = 1.0 / (1.0 + float(x * x + y * y));
            vec4 sampleColor = texture(
                texture0,
                uv + vec2(float(x), float(y)) * texel * 2.1
            );
            glow += sampleColor.rgb * sampleColor.a * distanceWeight;
            glowAlpha += sampleColor.a * distanceWeight;
            weight += distanceWeight;
        }
    }
    glow /= weight;
    glowAlpha /= weight;

    float flicker = 0.97 + 0.03 * sin(time * 13.0);
    vec3 color = core * center.a + glow * (2.0 + pulse * 0.7);
    color *= flicker;
    float alpha = clamp(center.a + glowAlpha * (1.3 + pulse * 0.35), 0.0, 1.0);

    finalColor = vec4(color, alpha) * colDiffuse * fragColor;
}
