#include "camera.h"
#include "gl_quickpreview.h"
#include "gl_utils.h"
#include <stdlib.h>


#define VERTEX_ATTRIBUTE 0
#define TEX_COORD_ATTRIBUTE 1

struct _GLQuickPreview {
	GLuint frame_buffer;
	GLuint program;
	GLuint uniform_transform;
	GLuint uniform_pixel_size;
	GLuint uniform_half_image_size;
	GLuint uniform_texture;
	GLuint uniform_pixel_layout;
	GLuint uniform_srgb_map;
	GLuint uniform_color_matrix;

	GLuint pixel_layout_texture_id;
	uint32_t pixel_layout_texture_width;
	uint32_t pixel_layout_texture_height;
	MPPixelFormat pixel_layout_texture_format;
};

GLQuickPreview *gl_quick_preview_new()
{
	GLuint frame_buffer;
	glGenFramebuffers(1, &frame_buffer);
	check_gl();

	GLuint shaders[] = {
		gl_load_shader("data/debayer.vert", GL_VERTEX_SHADER),
		gl_load_shader("data/debayer.frag", GL_FRAGMENT_SHADER),
	};

	GLuint program = gl_link_program(shaders, 2);
	glBindAttribLocation(program, VERTEX_ATTRIBUTE, "vert");
	glBindAttribLocation(program, TEX_COORD_ATTRIBUTE, "tex_coord");
	check_gl();

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	check_gl();

	GLuint pixel_layout_texture_id;
	glGenTextures(1, &pixel_layout_texture_id);
	check_gl();

	GLQuickPreview *self = malloc(sizeof(GLQuickPreview));
	self->frame_buffer = frame_buffer;
	self->program = program;

	self->uniform_transform = glGetUniformLocation(self->program, "transform");
	self->uniform_pixel_size = glGetUniformLocation(self->program, "pixel_size");
	self->uniform_half_image_size = glGetUniformLocation(self->program, "half_image_size");
	self->uniform_texture = glGetUniformLocation(self->program, "texture");
	self->uniform_pixel_layout = glGetUniformLocation(self->program, "pixel_layout");
	self->uniform_color_matrix = glGetUniformLocation(self->program, "color_matrix");
	self->uniform_srgb_map = glGetUniformLocation(self->program, "srgb_map");
	check_gl();

	self->pixel_layout_texture_id = pixel_layout_texture_id;
	self->pixel_layout_texture_width = 0;
	self->pixel_layout_texture_height = 0;
	self->pixel_layout_texture_format = MP_PIXEL_FMT_BGGR8;

	return self;
}

void gl_quick_preview_free(GLQuickPreview *self)
{
	glDeleteFramebuffers(1, &self->frame_buffer);

	free(self);
}

bool
ql_quick_preview_supports_format(GLQuickPreview *self, const MPPixelFormat format)
{
	return format == MP_PIXEL_FMT_BGGR8;
}

bool
gl_quick_preview(GLQuickPreview *self,
		 GLuint dst_id,
		 const uint32_t dst_width, const uint32_t dst_height,
		 GLuint source_id,
		 const uint32_t src_width, const uint32_t src_height,
		 const MPPixelFormat format,
		 const uint32_t rotation,
		 const bool mirrored,
		 const float *colormatrix,
		 const uint8_t blacklevel)
{
	// Generate pixel layout image
	if (src_width != self->pixel_layout_texture_width
	    || src_height != self->pixel_layout_texture_height
	    || format != self->pixel_layout_texture_format) {

		glBindTexture(GL_TEXTURE_2D, self->pixel_layout_texture_id);

		uint16_t *buffer = malloc(src_width * src_height * sizeof(uint16_t));
		for (size_t y = 0; y < src_height; ++y) {
			for (size_t x = 0; x < src_width; ++x) {
				buffer[x + y * src_width] =
					(y % 2) == 0
					? ((x % 2) == 0 ? 0b1111 << 4 : 0b1111 << 8)
					: ((x % 2) == 0 ? 0b1111 << 8 : 0b1111 << 12);
			}
		}

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0,  GL_RGBA, src_width, src_height, 0,  GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, buffer);
		check_gl();
		free(buffer);

		self->pixel_layout_texture_width = src_width;
		self->pixel_layout_texture_height = src_height;
		self->pixel_layout_texture_format = format;
		glBindTexture(GL_TEXTURE_2D, 0);
		check_gl();
	}

	assert(ql_quick_preview_supports_format(self, format));

	glBindFramebuffer(GL_FRAMEBUFFER, self->frame_buffer);
	glBindTexture(GL_TEXTURE_2D, dst_id);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst_id, 0);
	check_gl();

	glViewport(0, 0, dst_width, dst_height);
	check_gl();

	assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

	glUseProgram(self->program);
	check_gl();

	GLfloat rotation_list[4] = { 0, -1, 0, 1 };
	int rotation_index = 4 - rotation / 90;

	GLfloat sin_rot = rotation_list[rotation_index];
	GLfloat cos_rot = rotation_list[(rotation_index + 1) % 4];
	GLfloat scale_x = mirrored ? 1 : -1;
	GLfloat matrix[9] = {
		cos_rot * scale_x, sin_rot, 0,
		-sin_rot * scale_x, cos_rot, 0,
		0, 0, 1,
	};
	glUniformMatrix3fv(self->uniform_transform, 1, GL_FALSE, matrix);
	check_gl();

	glUniform2f(self->uniform_half_image_size, src_width / 2, src_height / 2);
	check_gl();

	GLfloat pixel_size_x = 1.0f / src_width;
	GLfloat pixel_size_y = 1.0f / src_height;
	glUniform2f(self->uniform_pixel_size, pixel_size_x, pixel_size_y);
	check_gl();

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, source_id);
	glUniform1i(self->uniform_texture, 0);
	check_gl();

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, self->pixel_layout_texture_id);
	glUniform1i(self->uniform_pixel_layout, 1);
	check_gl();

	if (colormatrix)
	{
		GLfloat transposed[9];
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				transposed[i + j * 3] = colormatrix[j + i * 3];

		glUniformMatrix3fv(self->uniform_color_matrix, 1, GL_FALSE, transposed);
	}
	else
	{
		static const GLfloat identity[9] = {
			1, 0, 0,
			0, 1, 0,
			0, 0, 1,
		};
		glUniformMatrix3fv(self->uniform_color_matrix, 1, GL_FALSE, identity);
	}

	glVertexAttribPointer(VERTEX_ATTRIBUTE, 2, GL_FLOAT, 0, 0, gl_quad_vertices);
	glEnableVertexAttribArray(VERTEX_ATTRIBUTE);
	glVertexAttribPointer(TEX_COORD_ATTRIBUTE, 2, GL_FLOAT, 0, 0, gl_quad_texcoords);
	glEnableVertexAttribArray(TEX_COORD_ATTRIBUTE);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	check_gl();

	// TODO: Render
	// glClearColor(0, 0, 1, 0);
	// glClear(GL_COLOR_BUFFER_BIT);

	// glBindTexture(GL_TEXTURE_2D, 0);
	// glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return true;
}
