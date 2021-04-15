precision highp float;

attribute vec2 vert;
attribute vec2 tex_coord;

uniform mat3 transform;
uniform vec2 pixel_size;
uniform vec2 half_image_size;

// varying vec2 uv1;
// varying vec2 uv2;

varying vec2 top_left_uv;
varying vec2 top_right_uv;
varying vec2 bottom_left_uv;
varying vec2 bottom_right_uv;
varying vec2 half_pixel_coord;

void main() {
	top_left_uv = tex_coord - pixel_size / 2.0;
	bottom_right_uv = tex_coord + pixel_size / 2.0;
	top_right_uv = vec2(top_left_uv.x, bottom_right_uv.y);
	bottom_left_uv = vec2(bottom_right_uv.x, top_left_uv.y);

	// uv1 = tex_coord - pixel_size / 2.0;
	// uv2 = tex_coord + pixel_size / 2.0;
	// uv1 += pixel_size;
	// uv2 += pixel_size;
	half_pixel_coord = top_left_uv * half_image_size;

	gl_Position = vec4(transform * vec3(vert, 1), 1);
}
