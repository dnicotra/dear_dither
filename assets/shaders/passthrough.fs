#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float exposure;
uniform float contrast;
uniform float gamma;

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    vec3 c = texel.rgb;

    c *= pow(2.0, exposure);
    c = (c - 0.5) * contrast + 0.5;
    c = pow(max(c, 0.0), vec3(1.0 / gamma));

    finalColor = vec4(c, texel.a);
}
