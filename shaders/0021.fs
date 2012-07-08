precision mediump float;

uniform sampler2D uSamp1;
uniform sampler2D uSamp2;
uniform sampler2D uSamp3;
uniform vec4 uVal;
varying vec2 vTexCoord0;
varying vec2 vTexCoord1;

void main()
{
  vec4 v1 = texture2D(uSamp1, vTexCoord1);
  vec4 v2 = texture2D(uSamp3, v1.xy);
  vec4 v3 = texture2D(uSamp2, vTexCoord0);
  vec4 v4 = v1.bgra * uVal;
  vec4 v5 = v2.rabg + v3;
  vec4 v6 = v4 * v5;
  gl_FragColor = v6 * uVal;
}

