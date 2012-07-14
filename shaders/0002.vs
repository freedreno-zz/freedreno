//attribute vec4 aPosition0;
vec4 aPosition0 = vec4(1.2,2.0,3.0,4.0);
attribute vec4 aPosition1;
attribute vec3 aPosition2;

void main()
{
	vec4 v1, v2;
	float f;

	v1.x = aPosition0.x * aPosition1.w;
	v1.y = aPosition0.y + aPosition1.z;
	v1.z = aPosition0.z * aPosition1.y;
	v1.w = aPosition0.w + aPosition1.x;

	if ((aPosition2.z > 1.1) && (aPosition2.y >= 1.3)) {
		v2.x = aPosition1.x * aPosition2.z;
		v2.y = aPosition1.y * aPosition2.y;
		v2.z = aPosition1.z + radians(aPosition2.x);
		v2.w = aPosition1.w * 42.0;
	} else {
		v2 = aPosition1.wzyx;
	}

	f = dot(v1, v2);

	gl_PointSize = f;
	gl_Position = f * v1 * v2;
}

