__kernel void kern(
   __global short4 *a,
   __global short4 *b,
   __global short4 *c,
   __global short4 *result)
{
	result[0] = a[0] * b[0] + c[0] - a[0];
}

