#include "gl_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

void __check_gl(const char *file, int line)
{
	GLenum error = glGetError();

	const char *name;
	switch (error) {
		case GL_NO_ERROR:
			return; // no error
		case GL_INVALID_ENUM:
			name = "GL_INVALID_ENUM";
			break;
		case GL_INVALID_VALUE:
			name = "GL_INVALID_VALUE";
			break;
		case GL_INVALID_OPERATION:
			name = "GL_INVALID_OPERATION";
			break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			name = "GL_INVALID_FRAMEBUFFER_OPERATION";
			break;
		case GL_OUT_OF_MEMORY:
			name = "GL_OUT_OF_MEMORY";
			break;
		default:
			name = "UNKNOWN ERROR!";
			break;
	}

	printf("GL error at %s:%d - %s\n", file, line, name);
}

const GLfloat gl_quad_vertices[] = {
	-1, -1,
	1, -1,
	-1, 1,
	1, 1,
};

const GLfloat gl_quad_texcoords[] = {
	0, 0,
	1, 0,
	0, 1,
	1, 1,
};

GLuint
gl_load_shader(const char *path, GLenum type)
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

GLuint
gl_link_program(GLuint *shaders, size_t num_shaders)
{
	GLuint program = glCreateProgram();

	for (size_t i = 0; i < num_shaders; ++i) {
		glAttachShader(program, shaders[i]);
	}

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

	return program;
}
