precision mediump float;

varying vec4 vVaryingColor;

void main()
{
	gl_FragColor = vVaryingColor + vec4(1.0, 2.0, 3.0, 4.0);
}
