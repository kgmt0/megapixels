precision mediump float;

uniform sampler2D texture;

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

	gl_FragColor = vec4(color, 0);
}
