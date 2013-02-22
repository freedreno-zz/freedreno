__kernel void kern(
   __global float *a,
   __global float *b,
   __global float *c,
   __global float *result)
{
   result[0] = fmin(a[0], b[0]);
   result[1] = fmod(a[1], b[1]);
   result[2] = hypot(a[2], b[2]);
   result[3] = lgamma(a[3]);
   result[4] = log(a[4]);
   result[5] = log2(a[5]);
   result[6] = log10(a[6]);
   result[7] = log1p(a[7]);
   result[8] = logb(a[8]);
   result[9] = mad(a[9], b[9], c[9]);
}

