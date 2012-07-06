precision highp float;
uniform vec4 uColor1;
uniform vec4 uColor2;

void main()
{
gl_FragColor = uColor1 + uColor2;
}
