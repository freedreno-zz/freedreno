__kernel void kern(
   __global float *a,
   __global float *result)
{
   result[0] = sin(a[0]);
}

