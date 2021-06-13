#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D texture;
uniform mat3 color_matrix;

varying vec2 top_left_uv;
varying vec2 top_right_uv;
varying vec2 bottom_left_uv;
varying vec2 bottom_right_uv;

void main() {
	// Note the coordinates for texture samples need to be a varying, as the
	// Mali-400 has this as a fast path allowing 32-bit floats. Otherwise
	// they end up as 16-bit floats and that's not accurate enough.
	vec4 samples = vec4(
		texture2D(texture, top_left_uv).r,
		texture2D(texture, top_right_uv).r,
		texture2D(texture, bottom_left_uv).r,
		texture2D(texture, bottom_right_uv).r);

	// Assume BGGR for now. Currently this just takes 3 of the four samples
	// for each pixel, there's room here to do some better debayering.
	vec3 color = vec3(samples.w, (samples.y + samples.z) / 2.0, samples.x);
	vec3 corrected = color * color_matrix;

	// Fast SRGB estimate. See https://mimosa-pudica.net/fast-gamma/
	vec3 srgb_color = (vec3(1.138) * inversesqrt(corrected) - vec3(0.138)) * corrected;

	// Slow SRGB estimate
	// vec3 srgb_color = pow(color, vec3(1.0 / 2.2));

	gl_FragColor = vec4(srgb_color, 1);
}
