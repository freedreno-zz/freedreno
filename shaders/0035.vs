uniform float foo;

void main()
{
  vec4 v = vec4(1.0, 2.0, 3.0, 4.0);
  float f = foo;
  do {
    v = v * f;
    f -= 6.1;
  } while(f > 42.0);
  gl_Position = v;
}
