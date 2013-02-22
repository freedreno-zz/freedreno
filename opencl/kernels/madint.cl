__kernel void kern(
   __global int4 *a,
   __global int4 *b,
   __global int4 *c,
   __global int4 *result)
{
	result[0] = a[0] * b[0] + c[0] - a[0];
	result[1] = mad_hi(a[1], b[1], c[1]);
	result[2] = mad_sat(a[2], b[2], c[2]);
	result[3] = mad24(a[3], b[3], c[3]);
}

