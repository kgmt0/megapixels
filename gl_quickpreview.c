#include "camera.h"
#include "gl_quickpreview.h"
#include "gl_utils.h"
#include <stdint.h>
#include <stdlib.h>

#define VERTEX_ATTRIBUTE 0
#define TEX_COORD_ATTRIBUTE 1

static const GLfloat square_vertices[] = {
	-1, -1,
	1, -1,
	-1, 1,
	1, 1,
};

static const GLfloat square_texcoords[] = {
	1, 1,
	1, 0,
	0, 1,
	0, 0,
};

struct _GLQuickPreview {
	GLuint frame_buffer;
	GLuint program;
	GLuint uniform_transform;
	GLuint uniform_pixel_size;
	GLuint uniform_texture;
};

static GLuint load_shader(const char *path, GLenum type)
{
	check_gl();

	FILE *f = fopen(path, "r");

	fseek(f, 0, SEEK_END);
	GLint size = ftell(f);
	char *source = malloc(sizeof(char) * size);

	fseek(f, 0, SEEK_SET);
	if (fread(source, size, 1, f) == 0) {
		printf("Failed to read shader file\n");
		return 0;
	}

	GLuint shader = glCreateShader(type);
	assert(shader != 0);
	glShaderSource(shader, 1, (const GLchar * const*)&source, &size);
	glCompileShader(shader);
	check_gl();

	// Check compile status
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (success == GL_FALSE) {
		printf("Shader compilation failed for %s\n", path);
	}

	GLint log_length;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 0) {
		char *log = malloc(sizeof(char) * log_length);
		glGetShaderInfoLog(shader, log_length - 1, &log_length, log);

		printf("Shader %s log: %s\n", path, log);
		free(log);
	}

	free(source);

	return shader;
}

GLQuickPreview *gl_quick_preview_new()
{
	GLuint frame_buffer;
	glGenFramebuffers(1, &frame_buffer);
	check_gl();

	GLuint vert = load_shader("data/debayer.vert", GL_VERTEX_SHADER);
	GLuint frag = load_shader("data/debayer.frag", GL_FRAGMENT_SHADER);

	GLuint program = glCreateProgram();
	glAttachShader(program, vert);
	glAttachShader(program, frag);

	glBindAttribLocation(program, VERTEX_ATTRIBUTE, "vert");
	check_gl();
	glBindAttribLocation(program, TEX_COORD_ATTRIBUTE, "tex_coord");
	check_gl();
	glLinkProgram(program);
	check_gl();

	GLint success;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (success == GL_FALSE) {
		printf("Program linking failed\n");
	}

	GLint log_length;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 0) {
		char *log = malloc(sizeof(char) * log_length);
		glGetProgramInfoLog(program, log_length - 1, &log_length, log);

		printf("Program log: %s\n", log);
		free(log);
	}

	check_gl();

	GLQuickPreview *self = malloc(sizeof(GLQuickPreview));
	self->frame_buffer = frame_buffer;
	self->program = program;

	self->uniform_transform = glGetUniformLocation(self->program, "transform");
	check_gl();

	self->uniform_pixel_size = glGetUniformLocation(self->program, "pixel_size");
	check_gl();

	self->uniform_texture = glGetUniformLocation(self->program, "texture");
	check_gl();

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

	GLfloat rotation_list[4] = { 1, 0, -1, 0 };
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

	GLfloat pixel_size_x = 1.0f / src_width;
	GLfloat pixel_size_y = 1.0f / src_height;
	glUniform2f(self->uniform_pixel_size, pixel_size_x, pixel_size_y);
	check_gl();

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, source_id);
	glUniform1i(self->uniform_texture, 0);
	check_gl();

	glVertexAttribPointer(VERTEX_ATTRIBUTE, 2, GL_FLOAT, 0, 0, square_vertices);
	check_gl();
	glEnableVertexAttribArray(VERTEX_ATTRIBUTE);
	check_gl();
	glVertexAttribPointer(TEX_COORD_ATTRIBUTE, 2, GL_FLOAT, 0, 0, square_texcoords);
	check_gl();
	glEnableVertexAttribArray(TEX_COORD_ATTRIBUTE);
	check_gl();
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	check_gl();

	// TODO: Render
	// glClearColor(0, 0, 1, 0);
	// glClear(GL_COLOR_BUFFER_BIT);

	// glBindTexture(GL_TEXTURE_2D, 0);
	// glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glUseProgram(0);
	check_gl();

	return true;
}
