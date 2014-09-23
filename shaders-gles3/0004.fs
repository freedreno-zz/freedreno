#version 300 es
precision highp float;

uniform samplerCubeShadow uTexture;
in vec4 vTexCoord1;
in vec4 vTexCoord2;
in vec4 vTexCoord3;
out vec4 gl_FragColor;

void main()
{
    gl_FragColor = vec4(textureGrad(uTexture, vTexCoord1.xyzw, vTexCoord2.xyz, vTexCoord3.xyz));
}

