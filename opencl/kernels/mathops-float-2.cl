__kernel void kern(
   __global float *a,
   __global float *b,
   __global float *c,
   __global float *result)
{
   result[0] = atan2pi(a[0], b[0]);
   result[1] = cbrt(a[1]);
   result[2] = ceil(a[2]);
   result[3] = copysign(a[3], b[3]);
   result[4] = cos(a[4]);
   result[5] = cosh(a[5]);
   result[6] = cospi(a[6]);
   result[7] = half_divide(a[7], b[7]);
   result[8] = native_divide(a[8], b[8]);
   result[9] = erfc(a[9]);
}

