__kernel void kern(
   __global float4 *a,
   __global float4 *b,
   __global float4 *result4,
   __global float  *result)
{
   result[0] = dot(a[0], b[0]);
   result4[1] = cross(a[1], b[1]);
   result[2] = distance(a[2], b[2]);
   result[3] = length(a[3]);
   result4[4] = normalize(a[4]);
   result[5] = fast_distance(a[5], b[5]);
   result[6] = fast_length(a[6]);
   result4[7] = fast_normalize(a[7]);
}

