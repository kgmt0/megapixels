#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D texture;

varying vec2 uv;

#define fetch(p) texture2D(texture, p).r

void main() {
	gl_FragColor = texture2D(texture, uv);
}
