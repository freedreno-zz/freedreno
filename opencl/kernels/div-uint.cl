__kernel void kern(
   __global unsigned int *a,
   __global unsigned int *b,
   __global unsigned *result)
{
   result[0] = a[0] / b[0];
}

