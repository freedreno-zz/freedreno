attribute vec4 aPosition0;
attribute vec4 aPosition1;

void main()
{
	vec2 v;
	v.x = aPosition0.y;
	v.y = aPosition1.x;
	gl_PointSize = dot(v, v);
	gl_Position = aPosition0;
}

