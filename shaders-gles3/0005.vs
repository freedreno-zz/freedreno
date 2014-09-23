#version 300 es
in vec4 in_position;
in vec4 in_TexCoord;

out vec4 vTexCoord1;
out vec4 vTexCoord2;
out vec4 vTexCoord3;

void main()
{
    gl_Position = in_position;
    vTexCoord1 = vTexCoord2 = vTexCoord3 = in_TexCoord;
}

