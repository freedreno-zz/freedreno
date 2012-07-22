precision highp float;
varying vec4 uColor;
uniform samplerCube samp;
varying float foo;

void main()
{
  gl_FragColor = foo * textureCube(samp, uColor.xxy, uColor.y);
}
