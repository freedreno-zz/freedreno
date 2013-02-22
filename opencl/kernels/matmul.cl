__kernel void kern(
   __global float* mat4,
   __global float* vec4,
   __global float* outvec4)
{
   // vec4.w * mat4[12..15]:
   outvec4[0] = vec4[3] * mat4[12];
   outvec4[1] = vec4[3] * mat4[13];
   outvec4[2] = vec4[3] * mat4[14];
   outvec4[3] = vec4[3] * mat4[15];

   // += vec4.z * mat4[8..11]:
   outvec4[0] += vec4[2] * mat4[8];
   outvec4[1] += vec4[2] * mat4[9];
   outvec4[2] += vec4[2] * mat4[10];
   outvec4[3] += vec4[2] * mat4[11];

   // += vec4.y * mat4[4..7]:
   outvec4[0] += vec4[1] * mat4[4];
   outvec4[1] += vec4[1] * mat4[5];
   outvec4[2] += vec4[1] * mat4[6];
   outvec4[3] += vec4[1] * mat4[7];

   // += vec4.x * mat4[0..3]:
   outvec4[0] += vec4[0] * mat4[0];
   outvec4[1] += vec4[0] * mat4[1];
   outvec4[2] += vec4[0] * mat4[2];
   outvec4[3] += vec4[0] * mat4[3];
}

