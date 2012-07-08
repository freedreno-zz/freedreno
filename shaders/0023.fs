precision mediump float;

uniform sampler2D uSamp1;
uniform sampler2D uSamp2;
uniform sampler2D uSamp3;
uniform vec4 uVal;
uniform vec4 uVal2;
uniform vec4 uVal3;
uniform vec4 uVal4;
varying vec2 vTexCoord0;
varying vec2 vTexCoord1;

/*
 *   00 - .xyzw (natural order)
 *   ff - .wxyz
 *   aa - .zwxy
 *   55 - .yzwx
 *   - .xyxy
 *   - .wzzw
 *   - .xxxx
 *   - .yyyy
 *   - .zzzz
 *   - .wwww
 *   6c - .x
 *   b1 - .y
 */

void main()
{
  vec4 v1 = texture2D(uSamp1, vTexCoord1);
  vec4 v2 = texture2D(uSamp3, v1.xy);
  vec4 v3 = texture2D(uSamp2, vTexCoord0);
  vec4 v4 = v1 * uVal;
  vec4 v5 = v1.wxyz * uVal;
  vec4 v6 = v2 * uVal;
  vec4 v7 = v2.zwxy * uVal.yzwx;
  vec4 v8 = v1.xyxy + uVal;
  vec4 v9 = v2.wzzw * uVal;
  vec4 v10 = v1.xxxx * uVal2.x;
  vec4 v11 = v2.wwww * uVal3.y;
  vec4 v12 = v3 + uVal.zzzz;
  vec4 v13 = v3 * uVal.w;
  vec4 v14 = v3.wwww * uVal4.z;
  gl_FragColor = v1 * v2 + v3 * v4 + v5 * v6 + v7 * v8 + v9 * v10 + v11 * v12 + v13 * v14;
}

