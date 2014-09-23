#version 300 es
precision highp float;

in vec3 vTexCoord;
out vec4 gl_FragColor;

void main()
{
    gl_FragColor = vec4(1.0, 1.0, 1.0, float(int(vTexCoord.x) / int(vTexCoord.y)));
}

