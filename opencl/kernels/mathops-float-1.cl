__kernel void kern(
   __global float *a,
   __global float *b,
   __global float *c,
   __global float *result)
{
   result[0] = acos(a[0]);
   result[1] = acosh(a[1]);
   result[2] = acospi(a[2]);
   result[3] = asin(a[3]);
   result[4] = asinh(a[4]);
   result[5] = asinpi(a[5]);
   result[6] = atan(a[6]);
   result[7] = atan2(a[7], b[7]);
   result[8] = atanh(a[8]);
   result[9] = atanpi(a[9]);
}

