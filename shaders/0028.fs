precision highp float;
uniform vec4 uColor1;
uniform sampler2D samp;

void main()
{
	vec4 uColor2 = texture2D(samp, uColor1.xy);
	float f0 = uColor2.x;
	float f1 = uColor2.w;
	vec4 v = uColor1;
	if ((f0 > 2.0) && (f1 > 4.3)) {
		v = f0 * v;
	}
	if (f1 > 3.3) {
		v = f1 * v;
	}
	gl_FragColor = v;
}
