__kernel void kern(
   __global float *a,
   __global float *b,
   __global float *c,
   __global float *result)
{
   result[0] = maxmag(a[0], b[0]);
   result[1] = minmag(a[1], b[1]);
   result[2] = nextafter(a[2], b[2]);
   result[3] = pow(a[3], b[3]);
   result[4] = half_recip(a[4]);
   result[5] = native_recip(a[5]);
   result[6] = remainder(a[6], b[6]);
   result[7] = rint(a[7]);
   result[8] = rootn(a[8], (int)b[8]);
   result[9] = round(a[9]);
}

