uniform sampler2D samp;
attribute vec4 aFoo;
varying float foo;

void main()
{
  vec4 aPosition = texture2D(samp, aFoo.xy);
  foo = aFoo.w;
  gl_Position = aPosition;
}
