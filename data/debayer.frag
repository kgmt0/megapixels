precision highp float;

uniform sampler2D texture;
uniform sampler2D pixel_layout;
uniform mat3 color_matrix;

uniform vec2 pixel_size;

// varying vec2 uv1;
// varying vec2 uv2;
varying vec2 top_left_uv;
varying vec2 top_right_uv;
varying vec2 bottom_left_uv;
varying vec2 bottom_right_uv;
varying vec2 half_pixel_coord;

#define fetch(p) texture2D(texture, p).r

void main() {
	vec4 pixels = vec4(
		fetch(top_left_uv),
		fetch(top_right_uv),
		fetch(bottom_right_uv),
		fetch(bottom_left_uv));

	vec2 arrangement = floor(half_pixel_coord);

	vec2 is_bottom_left = step(1.0, arrangement);

	// vec3 color = mix(
	// 	mix(pixels.xyz, pixels.yzw, is_bottom_left.y),
	// 	mix(pixels.wzy, pixels.zyx, is_bottom_left.y),
	// 	is_bottom_left.x);
	vec3 color = pixels.zyx;

	// Fast SRGB estimate. See https://mimosa-pudica.net/fast-gamma/
	vec3 srgb_color = (vec3(1.138) * inversesqrt(color) - vec3(0.138)) * color;

	// Slow SRGB estimate
	// vec3 srgb_color = pow(color, vec3(1.0 / 2.2));

	gl_FragColor = vec4(color_matrix * srgb_color, 0);
}
