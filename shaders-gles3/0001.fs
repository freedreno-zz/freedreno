#version 300 es
precision mediump float;

uniform sampler2D uTexture;
in vec3 vTexCoord;
out vec4 gl_FragColor;

void main()
{
    gl_FragColor.xy = vec2(textureSize(uTexture, int(vTexCoord.x)));
    gl_FragColor.zw = vec2(textureSize(uTexture, int(vTexCoord.y)));
}

