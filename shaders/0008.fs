precision mediump float;

uniform sampler2D g_NormalMap;
varying vec2 vTexCoord0;

void main()
{
  vec3 vNormal = vec3(2.0, 2.0, 0.0) * texture2D(g_NormalMap, vTexCoord0).xyz;
  vNormal.y = sqrt(1.0 - dot(vNormal, vNormal));
  gl_FragColor = vec4(vNormal, 1.0);
}

