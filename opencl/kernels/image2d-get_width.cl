__kernel void
image_test(__read_only image2d_t image,
	__global int *result)
{
	result[0] = get_image_width(image);
}

