__kernel void kern(
   __global float *a,
   __global float *b,
   __global float *result)
{
	result[0] = (a[0]  > b[0]) ? a[0] : b[0];
	result[1] = (a[1] >= b[1]) ? a[1] : b[1];
	result[2] = (a[2]  < b[2]) ? a[2] : b[2];
	result[3] = (a[3] <= b[3]) ? a[3] : b[3];
}

