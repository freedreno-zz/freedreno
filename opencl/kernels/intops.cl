__kernel void kern(
   __global int *a,
   __global int *b,
   __global int *c,
   __global int *result)
{
   result[0] = mad24(a[0], b[0], c[0]);
   result[1] = mul24(a[1], b[1]);
   result[2] = clz(a[2]);
   result[3] = clamp(a[3], b[3], c[3]);
   result[4] = mad_hi(a[4], b[4], c[4]);
   result[5] = mad_sat(a[5], b[5], c[5]);
   result[6] = max(a[6], b[6]);
   result[7] = min(a[7], b[7]);
   result[8] = mul_hi(a[8], b[8]);
   result[9] = rotate(a[9], b[9]);
   result[10] = sub_sat(a[10], b[10]);
   result[11] = abs(a[11]);
   result[12] = abs_diff(a[12], b[12]);
   result[13] = add_sat(a[13], b[13]);
   result[14] = hadd(a[14], b[14]);
   result[15] = rhadd(a[15], b[15]);
}

