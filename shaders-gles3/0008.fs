#version 300 es
precision highp float;

uniform sampler2D uTexture;
in vec4 vTexCoord1;
in vec4 vTexCoord2;
in vec4 vTexCoord3;
out vec4 gl_FragColor;

void main()
{
    gl_FragColor = textureGradOffset(uTexture, vTexCoord1.xy, vTexCoord2.xy, vTexCoord3.xy, ivec2(7, 9));
}

