__kernel void kern(
   __global int *a,
   __global int *b,
   __global int *result)
{
   result[0] = a[0] / b[0];
}

