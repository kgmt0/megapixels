#include "gl_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <gdk/gdk.h>

void
gl_util_check_error(const char *file, int line)
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

	// raise(SIGTRAP);
}

GLuint
gl_util_load_shader(const char *resource, GLenum type, const char **extra_sources,
		    size_t num_extra)
{
	GdkGLContext *context = gdk_gl_context_get_current();
	assert(context);

	GLuint shader = glCreateShader(type);
	if (shader == 0) {
		return 0;
	}

	GBytes *bytes = g_resources_lookup_data(resource, 0, NULL);
	if (!bytes) {
		printf("Failed to load shader resource %s\n", resource);
		return 0;
	}

	// Build #define for OpenGL context information
	gboolean is_es = gdk_gl_context_get_use_es(context);
	int major, minor;
	gdk_gl_context_get_version(context, &major, &minor);
	char context_info_buf[128];
	snprintf(context_info_buf, 128,
		 "#define %s\n#define GL_%d\n#define GL_%d_%d\n",
		 is_es ? "GL_ES" : "GL_NO_ES", major, major, minor);

	gsize glib_size = 0;
	const GLchar *source = g_bytes_get_data(bytes, &glib_size);
	if (glib_size == 0 || glib_size > INT_MAX) {
		printf("Invalid size for resource\n");
		return 0;
	}

	const GLchar **sources = malloc((num_extra + 1) * sizeof(GLchar *));
	GLint *sizes = malloc((num_extra + 1) * sizeof(GLint));

	for (size_t i = 0; i < num_extra; ++i) {
		sources[i] = extra_sources[i];
		sizes[i] = -1;
	}
	sources[num_extra] = source;
	sizes[num_extra] = glib_size;

	glShaderSource(shader, num_extra + 1, sources, sizes);
	glCompileShader(shader);
	check_gl();

	free(sources);
	free(sizes);

	g_bytes_unref(bytes);

	// Check compile status
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (success == GL_FALSE) {
		printf("Shader compilation failed for %s\n", resource);

		glDeleteShader(shader);
		return 0;
	}

	GLint log_length;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 0) {
		char *log = malloc(sizeof(char) * log_length);
		glGetShaderInfoLog(shader, log_length - 1, &log_length, log);

		printf("Shader %s log: %s\n", resource, log);
		free(log);

		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

GLuint
gl_util_link_program(GLuint *shaders, size_t num_shaders)
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

static const GLfloat quad_data[] = {
	// Vertices
	-1,
	-1,
	1,
	-1,
	-1,
	1,
	1,
	1,
	// Texcoords
	0,
	0,
	1,
	0,
	0,
	1,
	1,
	1,
};

GLuint
gl_util_new_quad()
{
	GdkGLContext *context = gdk_gl_context_get_current();
	assert(context);

	if (gdk_gl_context_get_use_es(context)) {
		return 0;
	} else {
		GLuint buffer;
		glGenBuffers(1, &buffer);

		glBindBuffer(GL_ARRAY_BUFFER, buffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad_data), quad_data,
			     GL_STATIC_DRAW);
		check_gl();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		check_gl();

		return buffer;
	}
}

void
gl_util_bind_quad(GLuint buffer)
{
	GdkGLContext *context = gdk_gl_context_get_current();
	assert(context);

	if (gdk_gl_context_get_use_es(context)) {
		glVertexAttribPointer(GL_UTIL_VERTEX_ATTRIBUTE, 2, GL_FLOAT, 0, 0,
				      quad_data);
		check_gl();
		glEnableVertexAttribArray(GL_UTIL_VERTEX_ATTRIBUTE);
		check_gl();

		glVertexAttribPointer(GL_UTIL_TEX_COORD_ATTRIBUTE, 2, GL_FLOAT, 0, 0,
				      quad_data + 8);
		check_gl();
		glEnableVertexAttribArray(GL_UTIL_TEX_COORD_ATTRIBUTE);
		check_gl();
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, buffer);
		check_gl();

		glVertexAttribPointer(GL_UTIL_VERTEX_ATTRIBUTE, 2, GL_FLOAT,
				      GL_FALSE, 0, 0);
		glEnableVertexAttribArray(GL_UTIL_VERTEX_ATTRIBUTE);
		check_gl();

		glVertexAttribPointer(GL_UTIL_TEX_COORD_ATTRIBUTE, 2, GL_FLOAT,
				      GL_FALSE, 0, (void *)(8 * sizeof(float)));
		glEnableVertexAttribArray(GL_UTIL_TEX_COORD_ATTRIBUTE);
		check_gl();
	}
}

void
gl_util_draw_quad(GLuint buffer)
{
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	check_gl();
}
