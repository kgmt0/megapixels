precision mediump float;

attribute vec2 vert;
attribute vec2 tex_coord;

varying vec2 uv;

void main() {
	uv = tex_coord;

	gl_Position = vec4(vert, 0, 1);
}
