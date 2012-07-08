attribute vec4 aPosition;
attribute vec2 aTexCoord;

varying vec2 vTexCoord0;
varying vec2 vTexCoord1;

void main()
{
	vTexCoord0 = aTexCoord;
	vTexCoord1 = 2.0 * aTexCoord;
	gl_Position = aPosition;
}
