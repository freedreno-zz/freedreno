precision highp float;
uniform vec4 uColor;
varying float foo;

void main()
{
	gl_FragColor = foo * uColor;
}
