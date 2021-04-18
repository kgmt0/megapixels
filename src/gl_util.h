#pragma once

#include <epoxy/gl.h>
#include <stddef.h>

#define GL_UTIL_VERTEX_ATTRIBUTE 0
#define GL_UTIL_TEX_COORD_ATTRIBUTE 1

#define check_gl() gl_util_check_error(__FILE__, __LINE__)
void gl_util_check_error(const char *file, int line);

GLuint gl_util_load_shader(const char *resource, GLenum type, const char **extra_sources, size_t num_extra);
GLuint gl_util_link_program(GLuint *shaders, size_t num_shaders);

GLuint gl_util_new_quad();
void gl_util_bind_quad(GLuint buffer);
void gl_util_draw_quad(GLuint buffer);
