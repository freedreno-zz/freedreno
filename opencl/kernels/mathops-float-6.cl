__kernel void kern(
   __global float *a,
   __global float *b,
   __global float *c,
   __global float *result)
{
   result[0] = rsqrt(a[0]);
   result[1] = sin(a[1]);
   result[2] = sinh(a[2]);
   result[3] = sinpi(a[3]);
   result[4] = sqrt(a[4]);
   result[5] = tan(a[5]);
   result[6] = tanh(a[6]);
   result[7] = tanpi(a[7]);
   result[8] = tgamma(a[8]);
   result[9] = trunc(a[9]);
}

