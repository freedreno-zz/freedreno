precision mediump float;

uniform sampler2D uSamp1;
uniform sampler2D uSamp2;
uniform sampler2D uSamp3;
uniform vec4 uVal;
varying vec2 vTexCoord;

void main()
{
  vec4 v1 = texture2D(uSamp1, vTexCoord);
  vec4 v2 = texture2D(uSamp3, vTexCoord);
  vec4 v3 = texture2D(uSamp2, vTexCoord);
  vec4 v4 = v1.bgra + v2;
  vec4 v5 = v4.rabg * v3;
  vec4 v6 = uVal * v5.agbr;
  gl_FragColor = v6;
}

