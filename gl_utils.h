#pragma once

#include <GLES2/gl2.h>
#include <stddef.h>

#define check_gl() __check_gl(__FILE__, __LINE__)
void __check_gl(const char *file, int line);

extern const GLfloat gl_quad_vertices[8];
extern const GLfloat gl_quad_texcoords[8];

GLuint gl_load_shader(const char *path, GLenum type);
GLuint gl_link_program(GLuint *shaders, size_t num_shaders);
