precision mediump float;

uniform sampler2D uSamp1;
uniform sampler2D uSamp2;
varying vec2 vTexCoord0;

void main()
{
  gl_FragColor = texture2D(uSamp1, vTexCoord0) + texture2D(uSamp2, vTexCoord0);
}

