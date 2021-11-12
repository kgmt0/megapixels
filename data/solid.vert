#ifdef GL_ES
precision mediump float;
#endif

attribute vec2 vert;

void
main()
{
        gl_Position = vec4(vert, 0, 1);
}
