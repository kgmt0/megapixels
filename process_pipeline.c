#include "process_pipeline.h"

#include "pipeline.h"
#include "zbar_pipeline.h"
#include "io_pipeline.h"
#include "main.h"
#include "config.h"
#include "quickpreview.h"
#include "gl_quickpreview.h"
#include <tiffio.h>
#include <assert.h>
#include <math.h>
#include <wordexp.h>
#include <gtk/gtk.h>

#include "gl_utils.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gdk/gdkwayland.h>
#include <sys/mman.h>
#include <drm/drm_fourcc.h>

#define TIFFTAG_FORWARDMATRIX1 50964

static const float colormatrix_srgb[] = { 3.2409, -1.5373, -0.4986, -0.9692, 1.8759,
					  0.0415, 0.0556,  -0.2039, 1.0569 };

static MPPipeline *pipeline;

static char burst_dir[23];
static char processing_script[512];

static volatile bool is_capturing = false;
static volatile int frames_processed = 0;
static volatile int frames_received = 0;

static const struct mp_camera_config *camera;

static MPCameraMode mode;

static int burst_length;
static int captures_remaining = 0;

static int preview_width;
static int preview_height;

// static bool gain_is_manual;
static int gain;
static int gain_max;

static bool exposure_is_manual;
static int exposure;

static char capture_fname[255];

static void
register_custom_tiff_tags(TIFF *tif)
{
	static const TIFFFieldInfo custom_fields[] = {
		{ TIFFTAG_FORWARDMATRIX1, -1, -1, TIFF_SRATIONAL, FIELD_CUSTOM, 1, 1,
		  "ForwardMatrix1" },
	};

	// Add missing dng fields
	TIFFMergeFieldInfo(tif, custom_fields,
			   sizeof(custom_fields) / sizeof(custom_fields[0]));
}

static bool
find_processor(char *script)
{
	char *xdg_config_home;
	char filename[] = "postprocess.sh";
	wordexp_t exp_result;

	// Resolve XDG stuff
	if ((xdg_config_home = getenv("XDG_CONFIG_HOME")) == NULL) {
		xdg_config_home = "~/.config";
	}
	wordexp(xdg_config_home, &exp_result, 0);
	xdg_config_home = strdup(exp_result.we_wordv[0]);
	wordfree(&exp_result);

	// Check postprocess.h in the current working directory
	sprintf(script, "%s", filename);
	if (access(script, F_OK) != -1) {
		sprintf(script, "./%s", filename);
		printf("Found postprocessor script at %s\n", script);
		return true;
	}

	// Check for a script in XDG_CONFIG_HOME
	sprintf(script, "%s/megapixels/%s", xdg_config_home, filename);
	if (access(script, F_OK) != -1) {
		printf("Found postprocessor script at %s\n", script);
		return true;
	}

	// Check user overridden /etc/megapixels/postprocessor.sh
	sprintf(script, "%s/megapixels/%s", SYSCONFDIR, filename);
	if (access(script, F_OK) != -1) {
		printf("Found postprocessor script at %s\n", script);
		return true;
	}

	// Check packaged /usr/share/megapixels/postprocessor.sh
	sprintf(script, "%s/megapixels/%s", DATADIR, filename);
	if (access(script, F_OK) != -1) {
		printf("Found postprocessor script at %s\n", script);
		return true;
	}

	return false;
}

static void
setup(MPPipeline *pipeline, const void *data)
{
	TIFFSetTagExtender(register_custom_tiff_tags);

	if (!find_processor(processing_script)) {
		g_printerr("Could not find any post-process script\n");
		exit(1);
	}
}

void
mp_process_pipeline_start()
{
	pipeline = mp_pipeline_new();

	mp_pipeline_invoke(pipeline, setup, NULL, 0);


	mp_zbar_pipeline_start();
}

void
mp_process_pipeline_stop()
{
	mp_pipeline_free(pipeline);

	mp_zbar_pipeline_stop();
}

void
mp_process_pipeline_sync()
{
	mp_pipeline_sync(pipeline);
}

#define NUM_BUFFERS 4

static GLQuickPreview *gl_quick_preview_state = NULL;

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;

// struct buffer {
// 	GLuint texture_id;
// 	EGLImage egl_image;
// 	int dma_fd;
// 	int dma_stride;
// 	int dma_offset;
// 	void *data;
// };
// static struct buffer input_buffers[NUM_BUFFERS];
// static struct buffer output_buffers[NUM_BUFFERS];

static PFNEGLEXPORTDMABUFIMAGEMESAPROC eglExportDMABUFImageMESA;
static PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC eglExportDMABUFImageQueryMESA;

static const char *
egl_get_error_str()
{
	EGLint error = eglGetError();
	switch (error) {
		case EGL_SUCCESS: return "EGL_SUCCESS";
		case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
		case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
		case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
		case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
		case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
		case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
		case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
		case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
		case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
		case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
		case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
		case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
		case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
		case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
	}
	return "Unknown";
}

#define RENDERDOC

#ifdef RENDERDOC
#include <dlfcn.h>
#include <renderdoc/app.h>
RENDERDOC_API_1_1_2 *rdoc_api = NULL;
#endif

static void
init_gl(MPPipeline *pipeline, GdkWindow **window)
{
#ifdef RENDERDOC
	{
		void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
		if (mod)
		{
		    pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
		    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&rdoc_api);
		    assert(ret == 1);
		}
		else
		{
			printf("Renderdoc not found\n");
		}
	}
#endif

	GdkDisplay *gdk_display = gdk_window_get_display(*window);
	egl_display = eglGetDisplay((EGLNativeDisplayType) gdk_wayland_display_get_wl_display(gdk_display));
	assert(egl_display != EGL_NO_DISPLAY);

	EGLint major, minor;
	if (!eglInitialize(egl_display, &major, &minor)) {
		printf("Failed to initialize egl: %s\n", egl_get_error_str());
		return;
	}

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		printf("Failed to bind OpenGL ES: %s\n", egl_get_error_str());
		return;
	}

	printf("extensions: %s\n", eglQueryString(egl_display, EGL_EXTENSIONS));

	EGLint config_attrs[1] = {
		EGL_NONE
	};

	EGLConfig config;
	EGLint num_configs = 0;
	if (!eglChooseConfig(egl_display, config_attrs, &config, 1, &num_configs)) {
		printf("Failed to pick a egl config: %s\n", egl_get_error_str());
		return;
	}

	if (num_configs != 1) {
		printf("No egl configs found: %s\n", egl_get_error_str());
		return;
	}

	EGLint context_attrs[5] = {
		EGL_CONTEXT_CLIENT_VERSION,
		2,
		EGL_CONTEXT_FLAGS_KHR,
		EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR,
		EGL_NONE,
	};

	egl_context = eglCreateContext(egl_display, config, NULL, context_attrs);

	if (egl_context == EGL_NO_CONTEXT) {
		printf("Failed to create OpenGL ES context: %s\n", egl_get_error_str());
		return;
	}

	if (!eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context))
	{
		printf("Failed to set current OpenGL context: %s\n", egl_get_error_str());
		return;
	}

	check_gl();

	eglExportDMABUFImageMESA = (PFNEGLEXPORTDMABUFIMAGEMESAPROC)
		eglGetProcAddress("eglExportDMABUFImageMESA");
	eglExportDMABUFImageQueryMESA = (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)
		eglGetProcAddress("eglExportDMABUFImageQueryMESA");

	// Generate textures for the buffers
	// GLuint textures[NUM_BUFFERS * 2];
	// glGenTextures(NUM_BUFFERS * 2, textures);

	// for (size_t i = 0; i < NUM_BUFFERS; ++i) {
	// 	input_buffers[i].texture_id = textures[i];
	// 	input_buffers[i].egl_image = EGL_NO_IMAGE;
	// 	input_buffers[i].dma_fd = -1;
	// }
	// for (size_t i = 0; i < NUM_BUFFERS; ++i) {
	// 	output_buffers[i].texture_id = textures[NUM_BUFFERS + i];
	// 	output_buffers[i].egl_image = EGL_NO_IMAGE;
	// 	output_buffers[i].dma_fd = -1;
	// }

	gl_quick_preview_state = gl_quick_preview_new();
	check_gl();

	printf("Initialized OpenGL\n");
}

void
mp_process_pipeline_init_gl(GdkWindow *window)
{
	mp_pipeline_invoke(pipeline, (MPPipelineCallback) init_gl, &window, sizeof(GdkWindow *));
}

static cairo_surface_t *
process_image_for_preview(const uint8_t *image)
{
	cairo_surface_t *surface;

	clock_t t1 = clock();

	if (gl_quick_preview_state && ql_quick_preview_supports_format(gl_quick_preview_state, mode.pixel_format)) {
#ifdef RENDERDOC
		if (rdoc_api) rdoc_api->StartFrameCapture(NULL, NULL);
#endif
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		check_gl();

		GLuint textures[2];
		glGenTextures(2, textures);

		glBindTexture(GL_TEXTURE_2D, textures[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, mode.width, mode.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, image);
		check_gl();

		glBindTexture(GL_TEXTURE_2D, textures[1]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, preview_width, preview_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		check_gl();

		gl_quick_preview(
			gl_quick_preview_state,
			textures[1], preview_width, preview_height,
			textures[0], mode.width, mode.height,
			mode.pixel_format,
			camera->rotate, camera->mirrored,
			camera->previewmatrix[0] == 0 ? NULL : camera->previewmatrix,
			camera->blacklevel);
		check_gl();

		surface = cairo_image_surface_create(
			CAIRO_FORMAT_RGB24, preview_width, preview_height);
		uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(surface);
		glFinish();

		clock_t t2 = clock();
		printf("%fms\n", (float)(t2 - t1) / CLOCKS_PER_SEC * 1000);

		// {
		// 	glBindTexture(GL_TEXTURE_2D, textures[1]);
		// 	EGLImage egl_image = eglCreateImage(egl_display, egl_context, EGL_GL_TEXTURE_2D, (EGLClientBuffer)(size_t)textures[1], NULL);

		// 	// Make sure it's in the expected format
		// 	int fourcc;
		// 	eglExportDMABUFImageQueryMESA(egl_display, egl_image, &fourcc, NULL, NULL);
		// 	assert(fourcc == DRM_FORMAT_ABGR8888);


		// 	int dmabuf_fd;
		// 	int stride, offset;
		// 	eglExportDMABUFImageMESA(egl_display, egl_image, &dmabuf_fd, &stride, &offset);

		// 	int fsize = lseek(dmabuf_fd, 0, SEEK_END);
		// 	printf("SIZE %d STRIDE %d OFFSET %d SIZE %d:%d\n", fsize, stride, offset, preview_width, preview_height);

		// 	size_t size = stride * preview_height;
		// 	uint32_t *data = mmap(NULL, fsize, PROT_READ, MAP_SHARED, dmabuf_fd, 0);
		// 	assert(data != MAP_FAILED);

		// 	int pixel_stride = stride / 4;

		// 	for (size_t y = 0; y < preview_height; ++y) {
		// 		for (size_t x = 0; x < preview_width; ++x) {
		// 			uint32_t p = data[x + y * pixel_stride];
		// 			pixels[x + y * preview_width] = p;
		// 		// 	uint16_t p = data[x + y * stride];
		// 		// 	uint32_t r = (p & 0b11111);
		// 		// 	uint32_t g = ((p >> 5) & 0b11111);
		// 		// 	uint32_t b = ((p >> 10) & 0b11111);
		// 		// 	pixels[x + y * preview_width] = (r << 16) | (g << 8) | b;
		// 		}
		// 		// memcpy(pixels + preview_width * y, data + stride * y, preview_width * sizeof(uint32_t));
		// 	}

		// 	{
		// 		FILE *f = fopen("test.raw", "w");
		// 		fwrite(data, fsize, 1, f);
		// 		fclose(f);
		// 	}

		// 	// memcpy(pixels, data, size);
		// 	munmap(data, size);
		// 	close(dmabuf_fd);
		// }
		glReadPixels(0, 0, preview_width, preview_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		check_gl();

		glBindTexture(GL_TEXTURE_2D, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

#ifdef RENDERDOC
		if(rdoc_api) rdoc_api->EndFrameCapture(NULL, NULL);
#endif
	} else {
		uint32_t surface_width, surface_height, skip;
		quick_preview_size(&surface_width, &surface_height, &skip, preview_width,
				   preview_height, mode.width, mode.height,
				   mode.pixel_format, camera->rotate);

		surface = cairo_image_surface_create(
			CAIRO_FORMAT_RGB24, surface_width, surface_height);

		uint8_t *pixels = cairo_image_surface_get_data(surface);

		quick_preview((uint32_t *)pixels, surface_width, surface_height, image,
			      mode.width, mode.height, mode.pixel_format,
			      camera->rotate, camera->mirrored,
			      camera->previewmatrix[0] == 0 ? NULL : camera->previewmatrix,
			      camera->blacklevel, skip);
	}

	// Create a thumbnail from the preview for the last capture
	cairo_surface_t *thumb = NULL;
	if (captures_remaining == 1) {
		printf("Making thumbnail\n");
		thumb = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, MP_MAIN_THUMB_SIZE, MP_MAIN_THUMB_SIZE);

		cairo_t *cr = cairo_create(thumb);
		draw_surface_scaled_centered(
			cr, MP_MAIN_THUMB_SIZE, MP_MAIN_THUMB_SIZE, surface);
		cairo_destroy(cr);
	}

	// Pass processed preview to main and zbar
	mp_zbar_pipeline_process_image(cairo_surface_reference(surface));
	mp_main_set_preview(surface);

	return thumb;
}

static void
process_image_for_capture(const uint8_t *image, int count)
{
	time_t rawtime;
	time(&rawtime);
	struct tm tim = *(localtime(&rawtime));

	char datetime[20] = { 0 };
	strftime(datetime, 20, "%Y:%m:%d %H:%M:%S", &tim);

	char fname[255];
	sprintf(fname, "%s/%d.dng", burst_dir, count);

	TIFF *tif = TIFFOpen(fname, "w");
	if (!tif) {
		printf("Could not open tiff\n");
	}

	// Define TIFF thumbnail
	TIFFSetField(tif, TIFFTAG_SUBFILETYPE, 1);
	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, mode.width >> 4);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, mode.height >> 4);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	TIFFSetField(tif, TIFFTAG_MAKE, mp_get_device_make());
	TIFFSetField(tif, TIFFTAG_MODEL, mp_get_device_model());
	uint16_t orientation;
	if (camera->rotate == 0) {
		orientation = camera->mirrored ? ORIENTATION_TOPRIGHT :
						 ORIENTATION_TOPLEFT;
	} else if (camera->rotate == 90) {
		orientation = camera->mirrored ? ORIENTATION_RIGHTBOT :
						 ORIENTATION_LEFTBOT;
	} else if (camera->rotate == 180) {
		orientation = camera->mirrored ? ORIENTATION_BOTLEFT :
						 ORIENTATION_BOTRIGHT;
	} else {
		orientation = camera->mirrored ? ORIENTATION_LEFTTOP :
						 ORIENTATION_RIGHTTOP;
	}
	TIFFSetField(tif, TIFFTAG_ORIENTATION, orientation);
	TIFFSetField(tif, TIFFTAG_DATETIME, datetime);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif, TIFFTAG_SOFTWARE, "Megapixels");
	long sub_offset = 0;
	TIFFSetField(tif, TIFFTAG_SUBIFD, 1, &sub_offset);
	TIFFSetField(tif, TIFFTAG_DNGVERSION, "\001\001\0\0");
	TIFFSetField(tif, TIFFTAG_DNGBACKWARDVERSION, "\001\0\0\0");
	char uniquecameramodel[255];
	sprintf(uniquecameramodel, "%s %s", mp_get_device_make(),
		mp_get_device_model());
	TIFFSetField(tif, TIFFTAG_UNIQUECAMERAMODEL, uniquecameramodel);
	if (camera->colormatrix[0]) {
		TIFFSetField(tif, TIFFTAG_COLORMATRIX1, 9, camera->colormatrix);
	} else {
		TIFFSetField(tif, TIFFTAG_COLORMATRIX1, 9, colormatrix_srgb);
	}
	if (camera->forwardmatrix[0]) {
		TIFFSetField(tif, TIFFTAG_FORWARDMATRIX1, 9, camera->forwardmatrix);
	}
	static const float neutral[] = { 1.0, 1.0, 1.0 };
	TIFFSetField(tif, TIFFTAG_ASSHOTNEUTRAL, 3, neutral);
	TIFFSetField(tif, TIFFTAG_CALIBRATIONILLUMINANT1, 21);
	// Write black thumbnail, only windows uses this
	{
		unsigned char *buf =
			(unsigned char *)calloc(1, (int)mode.width >> 4);
		for (int row = 0; row < (mode.height >> 4); row++) {
			TIFFWriteScanline(tif, buf, row, 0);
		}
		free(buf);
	}
	TIFFWriteDirectory(tif);

	// Define main photo
	TIFFSetField(tif, TIFFTAG_SUBFILETYPE, 0);
	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, mode.width);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, mode.height);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CFA);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	static const short cfapatterndim[] = { 2, 2 };
	TIFFSetField(tif, TIFFTAG_CFAREPEATPATTERNDIM, cfapatterndim);
#if (TIFFLIB_VERSION < 20201219) && !LIBTIFF_CFA_PATTERN
	TIFFSetField(tif, TIFFTAG_CFAPATTERN, "\002\001\001\000"); // BGGR
#else
	TIFFSetField(tif, TIFFTAG_CFAPATTERN, 4, "\002\001\001\000"); // BGGR
#endif
	printf("TIFF version %d\n", TIFFLIB_VERSION);
	if (camera->whitelevel) {
		TIFFSetField(tif, TIFFTAG_WHITELEVEL, 1, &camera->whitelevel);
	}
	if (camera->blacklevel) {
		TIFFSetField(tif, TIFFTAG_BLACKLEVEL, 1, &camera->blacklevel);
	}
	TIFFCheckpointDirectory(tif);
	printf("Writing frame to %s\n", fname);

	unsigned char *pLine = (unsigned char *)malloc(mode.width);
	for (int row = 0; row < mode.height; row++) {
		TIFFWriteScanline(tif, (void *) image + (row * mode.width), row, 0);
	}
	free(pLine);
	TIFFWriteDirectory(tif);

	// Add an EXIF block to the tiff
	TIFFCreateEXIFDirectory(tif);
	// 1 = manual, 2 = full auto, 3 = aperture priority, 4 = shutter priority
	if (!exposure_is_manual) {
		TIFFSetField(tif, EXIFTAG_EXPOSUREPROGRAM, 2);
	} else {
		TIFFSetField(tif, EXIFTAG_EXPOSUREPROGRAM, 1);
	}

	TIFFSetField(tif, EXIFTAG_EXPOSURETIME,
		     (mode.frame_interval.numerator /
		      (float)mode.frame_interval.denominator) /
			     ((float)mode.height / (float)exposure));
	uint16_t isospeed[1];
	isospeed[0] = (uint16_t)remap(gain - 1, 0, gain_max, camera->iso_min,
				      camera->iso_max);
	TIFFSetField(tif, EXIFTAG_ISOSPEEDRATINGS, 1, isospeed);
	TIFFSetField(tif, EXIFTAG_FLASH, 0);

	TIFFSetField(tif, EXIFTAG_DATETIMEORIGINAL, datetime);
	TIFFSetField(tif, EXIFTAG_DATETIMEDIGITIZED, datetime);
	if (camera->fnumber) {
		TIFFSetField(tif, EXIFTAG_FNUMBER, camera->fnumber);
	}
	if (camera->focallength) {
		TIFFSetField(tif, EXIFTAG_FOCALLENGTH, camera->focallength);
	}
	if (camera->focallength && camera->cropfactor) {
		TIFFSetField(tif, EXIFTAG_FOCALLENGTHIN35MMFILM,
			     (short)(camera->focallength * camera->cropfactor));
	}
	uint64_t exif_offset = 0;
	TIFFWriteCustomDirectory(tif, &exif_offset);
	TIFFFreeDirectory(tif);

	// Update exif pointer
	TIFFSetDirectory(tif, 0);
	TIFFSetField(tif, TIFFTAG_EXIFIFD, exif_offset);
	TIFFRewriteDirectory(tif);

	TIFFClose(tif);
}

static void
post_process_finished(GSubprocess *proc, GAsyncResult *res, cairo_surface_t *thumb)
{
	char *stdout;
	g_subprocess_communicate_utf8_finish(proc, res, &stdout, NULL, NULL);

	// The last line contains the file name
	int end = strlen(stdout);
	// Skip the newline at the end
	stdout[--end] = '\0';

	char *path = path = stdout + end - 1;
	do {
		if (*path == '\n') {
			path++;
			break;
		}
		--path;
	} while (path > stdout);

	mp_main_capture_completed(thumb, path);
}

static void
process_capture_burst(cairo_surface_t *thumb)
{
	time_t rawtime;
	time(&rawtime);
	struct tm tim = *(localtime(&rawtime));

	char timestamp[30];
	strftime(timestamp, 30, "%Y%m%d%H%M%S", &tim);

	if (g_get_user_special_dir(G_USER_DIRECTORY_PICTURES) != NULL) {
		sprintf(capture_fname,
			"%s/IMG%s",
			g_get_user_special_dir(G_USER_DIRECTORY_PICTURES),
			timestamp);
	} else if (getenv("XDG_PICTURES_DIR") != NULL) {
		sprintf(capture_fname,
			"%s/IMG%s",
			getenv("XDG_PICTURES_DIR"),
			timestamp);
	} else {
		sprintf(capture_fname,
			"%s/Pictures/IMG%s",
			getenv("HOME"),
			timestamp);
	}


	// Start post-processing the captured burst
	g_print("Post process %s to %s.ext\n", burst_dir, capture_fname);
	GError *error = NULL;
	GSubprocess *proc = g_subprocess_new(
		G_SUBPROCESS_FLAGS_STDOUT_PIPE,
		&error,
		processing_script,
		burst_dir,
		capture_fname,
		NULL);

	if (!proc) {
		g_printerr("Failed to spawn postprocess process: %s\n",
			   error->message);
		return;
	}

	g_subprocess_communicate_utf8_async(
		proc,
		NULL,
		NULL,
		(GAsyncReadyCallback)post_process_finished,
		thumb);
}

static void
process_image(MPPipeline *pipeline, const MPBuffer *buffer)
{
	size_t size =
		mp_pixel_format_width_to_bytes(mode.pixel_format, mode.width) *
		mode.height;
	uint8_t *image = malloc(size);
	memcpy(image, buffer->data, size);
	mp_io_pipeline_release_buffer(buffer->index);

	cairo_surface_t *thumb = process_image_for_preview(image);

	if (captures_remaining > 0) {
		int count = burst_length - captures_remaining;
		--captures_remaining;

		process_image_for_capture(image, count);

		if (captures_remaining == 0) {
			assert(thumb);
			process_capture_burst(thumb);
		} else {
			assert(!thumb);
		}
	} else {
		assert(!thumb);
	}

	free(image);

	++frames_processed;
	if (captures_remaining == 0) {
		is_capturing = false;
	}
}

void
mp_process_pipeline_process_image(MPBuffer buffer)
{
	// If we haven't processed the previous frame yet, drop this one
	if (frames_received != frames_processed && !is_capturing) {
		printf("Dropped frame at capture\n");
		mp_io_pipeline_release_buffer(buffer.index);
		return;
	}

	++frames_received;

	mp_pipeline_invoke(pipeline, (MPPipelineCallback)process_image, &buffer,
			   sizeof(MPBuffer));
}

static void
capture()
{
	char template[] = "/tmp/megapixels.XXXXXX";
	char *tempdir;
	tempdir = mkdtemp(template);

	if (tempdir == NULL) {
		g_printerr("Could not make capture directory %s\n", template);
		exit(EXIT_FAILURE);
	}

	strcpy(burst_dir, tempdir);

	captures_remaining = burst_length;
}

void
mp_process_pipeline_capture()
{
	is_capturing = true;

	mp_pipeline_invoke(pipeline, capture, NULL, 0);
}

static void
update_state(MPPipeline *pipeline, const struct mp_process_pipeline_state *state)
{
	camera = state->camera;
	mode = state->mode;

	burst_length = state->burst_length;

	preview_width = state->preview_width;
	preview_height = state->preview_height;

	// gain_is_manual = state->gain_is_manual;
	gain = state->gain;
	gain_max = state->gain_max;

	exposure_is_manual = state->exposure_is_manual;
	exposure = state->exposure;

	struct mp_main_state main_state = {
		.camera = camera,
		.mode = mode,
		.gain_is_manual = state->gain_is_manual,
		.gain = gain,
		.gain_max = gain_max,
		.exposure_is_manual = exposure_is_manual,
		.exposure = exposure,
		.has_auto_focus_continuous = state->has_auto_focus_continuous,
		.has_auto_focus_start = state->has_auto_focus_start,
	};
	mp_main_update_state(&main_state);
}

void
mp_process_pipeline_update_state(const struct mp_process_pipeline_state *new_state)
{
	mp_pipeline_invoke(pipeline, (MPPipelineCallback)update_state, new_state,
			   sizeof(struct mp_process_pipeline_state));
}
