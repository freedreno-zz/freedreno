precision mediump float;

varying vec4 vColor1;
varying vec4 vColor2;

void main()
{
	gl_FragColor = vec4(vColor1.xy, sqrt(vColor2.xy));
}
