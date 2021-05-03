#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D texture;

varying vec2 uv;

void main() {
	gl_FragColor = vec4(texture2D(texture, uv).rgb, 1);
}
