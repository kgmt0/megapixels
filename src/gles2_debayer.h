#include "camera.h"
#include "gl_util.h"
#include <assert.h>
#include <stdio.h>

typedef struct _GLES2Debayer GLES2Debayer;

GLES2Debayer *gles2_debayer_new(MPPixelFormat format);
void gles2_debayer_free(GLES2Debayer *self);

void gles2_debayer_use(GLES2Debayer *self);

void gles2_debayer_configure(GLES2Debayer *self,
                             const uint32_t dst_width,
                             const uint32_t dst_height,
                             const uint32_t src_width,
                             const uint32_t src_height,
                             const uint32_t rotation,
                             const bool mirrored,
                             const float *colormatrix,
                             const uint8_t blacklevel);

void gles2_debayer_process(GLES2Debayer *self, GLuint dst_id, GLuint source_id);
