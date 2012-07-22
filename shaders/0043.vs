attribute vec4 aPosition;
attribute vec4 aFoo;
varying float foo;
varying vec4 uColor;

void main()
{
  foo = aFoo.y;
  uColor = gl_Position = aPosition;
}
