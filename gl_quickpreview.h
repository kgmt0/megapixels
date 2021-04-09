#include "quickpreview.h"
#include <assert.h>
#include <stdio.h>
#include <GLES2/gl2.h>

typedef struct _GLQuickPreview GLQuickPreview;

GLQuickPreview* gl_quick_preview_new();
void gl_quick_preview_free(GLQuickPreview *self);

bool ql_quick_preview_supports_format(GLQuickPreview *self, const MPPixelFormat format);
bool gl_quick_preview(GLQuickPreview *self,
		      GLuint dst_id,
		      const uint32_t dst_width, const uint32_t dst_height,
		      GLuint source_id,
		      const uint32_t src_width, const uint32_t src_height,
		      const MPPixelFormat format,
		      const uint32_t rotation,
		      const bool mirrored,
		      const float *colormatrix,
		      const uint8_t blacklevel);
