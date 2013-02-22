__kernel void kern(
   __global float4 *a,
   __global float4 *b,
   __global float4 *c,
   __global float4 *result)
{
	result[0] = a[0] * b[0] + c[0] - a[0];
	result[1] = mad(a[1], b[1], c[1]);
}

