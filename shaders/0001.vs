attribute vec4 aPosition0;
attribute vec4 aPosition1;
attribute vec4 aPosition2;

void main()
{
	vec3 v, w;
	float f;
	v.x = aPosition0.y;
	v.y = aPosition1.x;
	v.z = aPosition2.z;
	f = dot(v, v);
	w.x = f;
	w.y = aPosition1.y;
	w.z = f * f;
	gl_PointSize = dot(f * v, w);
	gl_Position = aPosition0;
}

