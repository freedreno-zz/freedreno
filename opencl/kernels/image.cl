__kernel void
image_copy(__read_only image2d_t image1, __write_only image2d_t image2)
{
	const int xout = get_global_id(0);
	const int yout = get_global_id(1);

	write_imagef(image2, (int2)(yout,xout),
		read_imagef(image1, CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST, (int2)(xout,yout)));

	write_imagef(image2, (int2)(yout,xout),
		read_imagef(image1, CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_LINEAR, (int2)(xout,yout)));

//	write_imagef(image2, (int2)(yout,xout),
//		read_imagef(image1, CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_REPEAT | CLK_FILTER_LINEAR, (int2)(xout,yout)));

	write_imagef(image2, (int2)(yout,xout),
		read_imagef(image1, CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_LINEAR, (int2)(xout,yout)));

	write_imagef(image2, (int2)(yout,xout),
		read_imagef(image1, CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR, (int2)(xout,yout)));

//	write_imagef(image2, (int2)(yout,xout),
//		read_imagef(image1, CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_MIRRORED_REPEAT | CLK_FILTER_LINEAR, (int2)(xout,yout)));

	write_imagef(image2, (int2)(yout,xout),
		read_imagef(image1, CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_CLAMP | CLK_FILTER_LINEAR, (int2)(xout,yout)));
}

