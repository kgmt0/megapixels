#include <stdint.h>

void quick_debayer(const uint8_t *source, uint8_t *destination,
		   uint32_t pix_fmt, int width, int height, int skip,
		   int blacklevel);

void quick_yuv2rgb(const uint8_t *source, uint8_t *destination,
		   uint32_t pix_fmt, int width, int height, int skip);
