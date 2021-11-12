#include "gles2_debayer.h"

#include "camera.h"
#include "gl_util.h"
#include <stdlib.h>

#define VERTEX_ATTRIBUTE 0
#define TEX_COORD_ATTRIBUTE 1

struct _GLES2Debayer {
	GLuint frame_buffer;
	GLuint program;
	GLuint uniform_transform;
	GLuint uniform_pixel_size;
	GLuint uniform_texture;
	GLuint uniform_color_matrix;

	GLuint quad;
};

GLES2Debayer *
gles2_debayer_new(MPPixelFormat format)
{
	if (format != MP_PIXEL_FMT_BGGR8) {
		return NULL;
	}

	GLuint frame_buffer;
	glGenFramebuffers(1, &frame_buffer);
	check_gl();

	GLuint shaders[] = {
		gl_util_load_shader("/org/postmarketos/Megapixels/debayer.vert",
				    GL_VERTEX_SHADER, NULL, 0),
		gl_util_load_shader("/org/postmarketos/Megapixels/debayer.frag",
				    GL_FRAGMENT_SHADER, NULL, 0),
	};

	GLuint program = gl_util_link_program(shaders, 2);
	glBindAttribLocation(program, VERTEX_ATTRIBUTE, "vert");
	glBindAttribLocation(program, TEX_COORD_ATTRIBUTE, "tex_coord");
	check_gl();

	GLES2Debayer *self = malloc(sizeof(GLES2Debayer));
	self->frame_buffer = frame_buffer;
	self->program = program;

	self->uniform_transform = glGetUniformLocation(self->program, "transform");
	self->uniform_pixel_size = glGetUniformLocation(self->program, "pixel_size");
	self->uniform_texture = glGetUniformLocation(self->program, "texture");
	self->uniform_color_matrix =
		glGetUniformLocation(self->program, "color_matrix");
	check_gl();

	self->quad = gl_util_new_quad();

	return self;
}

void
gles2_debayer_free(GLES2Debayer *self)
{
	glDeleteFramebuffers(1, &self->frame_buffer);

	glDeleteProgram(self->program);

	free(self);
}

void
gles2_debayer_use(GLES2Debayer *self)
{
	glUseProgram(self->program);
	check_gl();

	gl_util_bind_quad(self->quad);
}

void
gles2_debayer_configure(GLES2Debayer *self, const uint32_t dst_width,
			const uint32_t dst_height, const uint32_t src_width,
			const uint32_t src_height, const uint32_t rotation,
			const bool mirrored, const float *colormatrix,
			const uint8_t blacklevel)
{
	glViewport(0, 0, dst_width, dst_height);
	check_gl();

	GLfloat rotation_list[4] = { 0, -1, 0, 1 };
	int rotation_index = 4 - rotation / 90;

	GLfloat sin_rot = rotation_list[rotation_index];
	GLfloat cos_rot = rotation_list[(rotation_index + 1) % 4];
	GLfloat scale_x = mirrored ? 1 : -1;
	GLfloat matrix[9] = {
		// clang-format off
		cos_rot * scale_x,  sin_rot, 0,
		-sin_rot * scale_x, cos_rot, 0,
		0,                        0, 1,
		// clang-format on
	};
	glUniformMatrix3fv(self->uniform_transform, 1, GL_FALSE, matrix);
	check_gl();

	GLfloat pixel_size_x = 1.0f / src_width;
	GLfloat pixel_size_y = 1.0f / src_height;
	glUniform2f(self->uniform_pixel_size, pixel_size_x, pixel_size_y);
	check_gl();

	if (colormatrix) {
		GLfloat transposed[9];
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				transposed[i + j * 3] = colormatrix[j + i * 3];

		glUniformMatrix3fv(self->uniform_color_matrix, 1, GL_FALSE,
				   transposed);
	} else {
		static const GLfloat identity[9] = {
			// clang-format off
			1, 0, 0,
			0, 1, 0,
			0, 0, 1,
			// clang-format on
		};
		glUniformMatrix3fv(self->uniform_color_matrix, 1, GL_FALSE,
				   identity);
	}
	check_gl();
}

void
gles2_debayer_process(GLES2Debayer *self, GLuint dst_id, GLuint source_id)
{
	glBindFramebuffer(GL_FRAMEBUFFER, self->frame_buffer);
	glBindTexture(GL_TEXTURE_2D, dst_id);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			       dst_id, 0);
	check_gl();

	assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, source_id);
	glUniform1i(self->uniform_texture, 0);
	check_gl();

	gl_util_draw_quad(self->quad);
}
