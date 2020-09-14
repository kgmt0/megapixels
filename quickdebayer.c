#include "quickdebayer.h"

// Fast but bad debayer method that scales and rotates by skipping source pixels and
// doesn't interpolate any values at all

void
quick_debayer_bggr8(const uint8_t *source, uint8_t *destination, int width, int height, int skip)
{
	int byteskip = 2 * skip;
	int input_size = width * height;
	int i;
	int j=0;
	int row_left = width;

	// B G
	// G R
	for(i=0;i<input_size;) {
		destination[j++] = source[i+width+1];
		destination[j++] = source[i+1];
		destination[j++] = source[i];
		i = i + byteskip;
		row_left = row_left - byteskip;
		if(row_left <= 0){
			row_left = width;
			i = i+width;
			i = i + (width * 2 * (skip-1));
		}
	}
}
