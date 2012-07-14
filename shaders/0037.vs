uniform float foo;

void main()
{
  vec4 v = vec4(1.0, 2.0, 3.0, 4.0);
  if (foo != 3.0) {
    v = v * foo;
  }
  gl_Position = v;
}
