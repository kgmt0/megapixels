precision mediump float;

attribute vec2 vert;
attribute vec2 tex_coord;

uniform mat3 transform;
uniform vec2 pixel_size;

varying vec2 uv1;
varying vec2 uv2;
varying vec2 pixel_coord;

void main() {
	uv1 = tex_coord - pixel_size / 2.0;
	uv2 = tex_coord + pixel_size / 2.0;
	uv1 += pixel_size;
	uv2 += pixel_size;
	pixel_coord = uv1 / pixel_size;

	gl_Position = vec4(transform * vec3(vert, 1), 1);
}
