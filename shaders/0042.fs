precision highp float;
varying vec4 uColor;
uniform sampler2D samp;
varying float foo;

void main()
{
  gl_FragColor = foo * texture2D(samp, uColor.xx);
}
