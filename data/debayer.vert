#ifdef GL_ES
precision mediump float;
#endif

attribute vec2 vert;
attribute vec2 tex_coord;

uniform mat3 transform;
uniform vec2 pixel_size;

varying vec2 top_left_uv;
varying vec2 top_right_uv;
varying vec2 bottom_left_uv;
varying vec2 bottom_right_uv;

void
main()
{
        top_left_uv = tex_coord - pixel_size / 2.0;
        bottom_right_uv = tex_coord + pixel_size / 2.0;
        top_right_uv = vec2(top_left_uv.x, bottom_right_uv.y);
        bottom_left_uv = vec2(bottom_right_uv.x, top_left_uv.y);

        gl_Position = vec4(transform * vec3(vert, 1), 1);
}
