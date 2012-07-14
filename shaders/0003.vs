attribute vec4 in_position;

varying vec4 vVaryingColor;

void main()
{
	gl_Position = in_position;
	vVaryingColor = mod(in_position, 42.0);
}
