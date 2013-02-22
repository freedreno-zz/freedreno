__kernel void kern(
   __global float *a,
   __global float *b,
   __global float *c,
   __global float *result)
{
   result[0] = erf(a[0]);
   result[1] = exp(a[1]);
   result[2] = exp2(a[2]);
   result[3] = exp10(a[3]);
   result[4] = expm1(a[4]);
   result[5] = fabs(a[5]);
   result[6] = fdim(a[6], b[6]);
   result[7] = floor(a[7]);
   result[8] = fma(a[8], b[8], c[8]);
   result[9] = fmax(a[9], b[9]);
}

