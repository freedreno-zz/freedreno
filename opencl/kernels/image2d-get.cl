__kernel void
image_test(__read_only image2d_t image,
	__global int *result)
{
	result[0] = get_image_width(image);
	result[1] = get_image_height(image);
	result[3] = get_image_depth(image);
	result[4] = get_image_channel_data_type(image);
	result[5] = get_image_channel_order(image);
}

