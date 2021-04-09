#include <GLES2/gl2.h>

#define check_gl() __check_gl(__FILE__, __LINE__)

static void __check_gl(const char *file, int line)
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
