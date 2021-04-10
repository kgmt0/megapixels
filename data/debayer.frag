precision mediump float;

uniform sampler2D texture;
// uniform sampler2D srgb_map;
uniform mat3 color_matrix;

varying vec2 uv1;
varying vec2 uv2;
varying vec2 pixel_coord;

#define fetch(p) texture2D(texture, p).r

void main() {
	vec4 pixels = vec4(
		fetch(uv1),
		fetch(vec2(uv1.x, uv2.y)),
		fetch(uv2),
		fetch(vec2(uv2.x, uv1.y)));

	vec2 arrangement = mod(pixel_coord, 2.0);

	vec2 is_top_left = step(1.0, arrangement);

	vec3 color = mix(
		mix(pixels.zyx, pixels.wzy, is_top_left.y),
		mix(pixels.yzw, pixels.xyz, is_top_left.y),
		is_top_left.x);

	vec3 srgb_color = pow(color, vec3(1.0 / 2.2));
	// vec3 srgb_color = vec3(
	// 	texture2D(srgb_map, vec2(color.r, 0)).r,
	// 	texture2D(srgb_map, vec2(color.g, 0)).r,
	// 	texture2D(srgb_map, vec2(color.b, 0)).r);

	gl_FragColor = vec4((color_matrix * srgb_color).bgr, 0);
}
