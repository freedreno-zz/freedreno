#version 300 es
in vec4 in_position;
in vec3 in_TexCoord;

out vec3 vTexCoord;

void main()
{
    gl_Position = in_position;
    vTexCoord = in_TexCoord;
}

