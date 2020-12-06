#include "camera.h"
#include <stdint.h>

void quick_preview(uint32_t *dst, const uint32_t dst_width,
		   const uint32_t dst_height, const uint8_t *src,
		   const uint32_t src_width, const uint32_t src_height,
		   const MPPixelFormat format, const uint32_t rotation,
		   const bool mirrored, const float *colormatrix,
		   const uint8_t blacklevel, const uint32_t skip);

void quick_preview_size(uint32_t *dst_width, uint32_t *dst_height, uint32_t *skip,
			const uint32_t preview_width, const uint32_t preview_height,
			const uint32_t src_width, const uint32_t src_height,
			const MPPixelFormat format, const int rotation);
