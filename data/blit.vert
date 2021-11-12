#ifdef GL_ES
precision mediump float;
#endif

attribute vec2 vert;
attribute vec2 tex_coord;

uniform mat3 transform;

varying vec2 uv;

void
main()
{
	uv = tex_coord;

	gl_Position = vec4(transform * vec3(vert, 1), 1);
}
