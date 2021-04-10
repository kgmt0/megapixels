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
	GLuint uniform_srgb_map;
	GLuint uniform_color_matrix;
	GLuint srgb_texture;
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

// static const uint8_t srgb[] = {
// 	0, 12, 21, 28, 33, 38, 42, 46, 49, 52, 55, 58, 61, 63, 66, 68, 70,
// 	73, 75, 77, 79, 81, 82, 84, 86, 88, 89, 91, 93, 94, 96, 97, 99, 100,
// 	102, 103, 104, 106, 107, 109, 110, 111, 112, 114, 115, 116, 117, 118,
// 	120, 121, 122, 123, 124, 125, 126, 127, 129, 130, 131, 132, 133, 134,
// 	135, 136, 137, 138, 139, 140, 141, 142, 142, 143, 144, 145, 146, 147,
// 	148, 149, 150, 151, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159,
// 	160, 161, 161, 162, 163, 164, 165, 165, 166, 167, 168, 168, 169, 170,
// 	171, 171, 172, 173, 174, 174, 175, 176, 176, 177, 178, 179, 179, 180,
// 	181, 181, 182, 183, 183, 184, 185, 185, 186, 187, 187, 188, 189, 189,
// 	190, 191, 191, 192, 193, 193, 194, 194, 195, 196, 196, 197, 197, 198,
// 	199, 199, 200, 201, 201, 202, 202, 203, 204, 204, 205, 205, 206, 206,
// 	207, 208, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214,
// 	215, 215, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222,
// 	222, 223, 223, 224, 224, 225, 226, 226, 227, 227, 228, 228, 229, 229,
// 	230, 230, 231, 231, 232, 232, 233, 233, 234, 234, 235, 235, 236, 236,
// 	237, 237, 237, 238, 238, 239, 239, 240, 240, 241, 241, 242, 242, 243,
// 	243, 244, 244, 245, 245, 245, 246, 246, 247, 247, 248, 248, 249, 249,
// 	250, 250, 251, 251, 251, 252, 252, 253, 253, 254, 254, 255
// };

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
	glBindAttribLocation(program, TEX_COORD_ATTRIBUTE, "tex_coord");
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

	// GLuint srgb_texture;
	// glGenTextures(1, &srgb_texture);
	// check_gl();

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	check_gl();

	// glBindTexture(GL_TEXTURE_2D, srgb_texture);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// check_gl();
	// glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, srgb);
	// check_gl();
	// glBindTexture(GL_TEXTURE_2D, 0);

	GLQuickPreview *self = malloc(sizeof(GLQuickPreview));
	self->frame_buffer = frame_buffer;
	self->program = program;

	self->uniform_transform = glGetUniformLocation(self->program, "transform");
	self->uniform_pixel_size = glGetUniformLocation(self->program, "pixel_size");
	self->uniform_texture = glGetUniformLocation(self->program, "texture");
	self->uniform_color_matrix = glGetUniformLocation(self->program, "color_matrix");
	self->uniform_srgb_map = glGetUniformLocation(self->program, "srgb_map");
	// self->srgb_texture = srgb_texture;
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

	// glActiveTexture(GL_TEXTURE1);
	// glBindTexture(GL_TEXTURE_2D, self->srgb_texture);
	// glUniform1i(self->uniform_srgb_map, 1);
	// check_gl();

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
