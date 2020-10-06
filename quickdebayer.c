#include "quickdebayer.h"

// Fast but bad debayer method that scales and rotates by skipping source pixels and
// doesn't interpolate any values at all

void
quick_debayer_bggr8(const uint8_t *source, uint8_t *destination, int width, int height, int skip, int blacklevel)
{
	int byteskip = 2 * skip;
	int input_size = width * height;
	int output_size = (width/byteskip) * (height/byteskip);
	int i;
	int j=0;
	int row_left = width;

	// Linear -> sRGB lookup table
	static int srgb[] = {
		0, 12, 21, 28, 33, 38, 42, 46, 49, 52, 55, 58, 61, 63, 66, 68, 70, 73, 75, 77, 79, 
		81, 82, 84, 86, 88, 89, 91, 93, 94, 96, 97, 99, 100, 102, 103, 104, 106, 107, 109,
		110, 111, 112, 114, 115, 116, 117, 118, 120, 121, 122, 123, 124, 125, 126, 127, 129,
		130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 142, 143, 144, 145,
		146, 147, 148, 149, 150, 151, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160,
		161, 161, 162, 163, 164, 165, 165, 166, 167, 168, 168, 169, 170, 171, 171, 172, 173,
		174, 174, 175, 176, 176, 177, 178, 179, 179, 180, 181, 181, 182, 183, 183, 184, 185,
		185, 186, 187, 187, 188, 189, 189, 190, 191, 191, 192, 193, 193, 194, 194, 195, 196,
		196, 197, 197, 198, 199, 199, 200, 201, 201, 202, 202, 203, 204, 204, 205, 205, 206,
		206, 207, 208, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215, 215,
		216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 222, 223, 223, 224, 224,
		225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233, 233,
		234, 234, 235, 235, 236, 236, 237, 237, 237, 238, 238, 239, 239, 240, 240, 241, 241,
		242, 242, 243, 243, 244, 244, 245, 245, 245, 246, 246, 247, 247, 248, 248, 249, 249,
		250, 250, 251, 251, 251, 252, 252, 253, 253, 254, 254, 255
	};

	// B G
	// G R
	for(i=0;i<input_size;) {
		destination[j++] = srgb[source[i+width+1] - blacklevel];
		destination[j++] = srgb[source[i+1] - blacklevel];
		destination[j++] = srgb[source[i] - blacklevel];
		i = i + byteskip;
		row_left = row_left - byteskip;
		if(row_left < byteskip){
			i = i + row_left;
			row_left = width;
			i = i + width;
			i = i + (width * 2 * (skip-1));
		}
	}
}
