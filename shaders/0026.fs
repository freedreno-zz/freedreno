precision highp float;
uniform vec4 uColor1;
uniform sampler2D samp;

void main()
{
	vec4 uColor2 = texture2D(samp, uColor1.xy);
	float f0 = uColor2.x;
	vec4 v = uColor1;
	while (f0 > 2.0) {
		v = f0 * v;
		f0 -= 2.1;
	}
	gl_FragColor = v;
}
