#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D texture;
uniform mat3 color_matrix;
#ifdef BITS_10
uniform float row_length;
uniform float padding_ratio;
#endif

varying vec2 top_left_uv;
varying vec2 top_right_uv;
varying vec2 bottom_left_uv;
varying vec2 bottom_right_uv;

#ifdef BITS_10
vec2
skip_5th_pixel(vec2 uv)
{
        vec2 new_uv = uv;

        new_uv.x *= 0.8;
        new_uv.x += floor(uv.x * row_length / 5.0) / row_length;

        // Crop out padding
        new_uv.x *= padding_ratio;

        return new_uv;
}
#endif

void
main()
{
        // Note the coordinates for texture samples need to be a varying, as the
        // Mali-400 has this as a fast path allowing 32-bit floats. Otherwise
        // they end up as 16-bit floats and that's not accurate enough.
#ifdef BITS_10
        vec4 samples = vec4(texture2D(texture, skip_5th_pixel(top_left_uv)).r,
                            texture2D(texture, skip_5th_pixel(top_right_uv)).r,
                            texture2D(texture, skip_5th_pixel(bottom_left_uv)).r,
                            texture2D(texture, skip_5th_pixel(bottom_right_uv)).r);
#else
        vec4 samples = vec4(texture2D(texture, top_left_uv).r,
                            texture2D(texture, top_right_uv).r,
                            texture2D(texture, bottom_left_uv).r,
                            texture2D(texture, bottom_right_uv).r);
#endif

#if defined(CFA_BGGR)
        vec3 color = vec3(samples.w, (samples.y + samples.z) / 2.0, samples.x);
#elif defined(CFA_GBRG)
        vec3 color = vec3(samples.z, (samples.x + samples.w) / 2.0, samples.y);
#elif defined(CFA_GRBG)
        vec3 color = vec3(samples.y, (samples.x + samples.w) / 2.0, samples.z);
#else
        vec3 color = vec3(samples.x, (samples.y + samples.z) / 2.0, samples.w);
#endif

        // Some crude blacklevel correction to make the preview a bit nicer, this
        // should be an uniform
        vec3 corrected = color - 0.02;

        // Apply the color matrices
        // vec3 corrected = color_matrix * color2;

        // Fast SRGB estimate. See https://mimosa-pudica.net/fast-gamma/
        vec3 srgb_color =
                (vec3(1.138) * inversesqrt(corrected) - vec3(0.138)) * corrected;

        // Slow SRGB estimate
        // vec3 srgb_color = pow(color, vec3(1.0 / 2.2));

        gl_FragColor = vec4(srgb_color, 1);
}
