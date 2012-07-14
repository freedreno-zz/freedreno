attribute vec4 aPosition;
varying float foo;

void main()
{
	foo = log2(aPosition.z);
	gl_Position = aPosition;
}
