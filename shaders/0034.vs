uniform float foo;

void main()
{
  vec4 v = vec4(1.0, 2.0, 3.0, 4.0);
  float f = foo;
  while (f > 42.0) {
    float g = foo;
    while (g > 41.2) {
      v = v * f;
      g -= 3.1;
    }
    f -= 6.1;
  }
  gl_Position = v;
}
