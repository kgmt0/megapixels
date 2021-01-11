# Megapixels

A GTK3 camera application that knows how to deal with the media request api

irc: #megapixels on freenode

## Building

```shell-session
$ meson build
$ cd build
$ ninja
$ sudo ninja install
```

# Config

Megapixels checks multiple locations for it's configuration file and uses the first one it finds.
As first step it will get the first compatible name in the device tree, in the case of a PinePhone
this might be "pine64,pinephone-1.2". Then that dtname will be used as the filename in the search
path in this order:

* $XDG_CONFIG_DIR/megapixels/config/$dtname.ini
* ~/.config/megapixels/config/$dtname.ini
* /etc/megapixels/config/$dtname.ini
* /usr/share/megapixels/config/$dtname.ini

The files in /usr/share/megapixels should be the config files distributed in this repository. The other
locations allow the user or distribution to override config.

## Config file format

Configuration files are INI format files. 

### [device]

This provides global info, currently only the `make` and `model` keys exist, which is metadata added to the
generated pictures.

### All other sections

These are the sections describing the sensors.

* `driver=ov5640` the name of the media node that provides the sensor and it's /dev/v4l-subdev* node.
* `media-driver=sun6i-csi` the name of the media node that has this camera in it.
* `rotate=90` the rotation angle to make the sensor match the screen
* `mirrored=true` whether the output is mirrored, useful for front-facing cameras
* `colormatrix=` the DNG colormatrix1 attribute as 9 comma seperated floats
* `forwardmatrix=` the DNG forwardmatrix1 attribute as 9 comma seperated floats
* `blacklevel=10` The DNG blacklevel attribute for this camera
* `whitelevel=255` The DNG whitelevel attribute for this camera
* `focallength=3.33` The focal length of the camera, for EXIF
* `cropfactor=10.81` The cropfactor for the sensor in the camera, for EXIF
* `fnumber=3.0` The aperture size of the sensor, for EXIF

These sections have two possibly prefixes: `capture-` and `preview-`. Both sets
are required. Capture is used when a picture is taken, whereas preview is used
when previewing.

* `width=640` and `height=480` the resolution to use for the sensor
* `rate=15` the refresh rate in fps to use for the sensor
* `fmt=BGGR8` sets the pixel and bus formats used when capturing from the sensor, only BGGR8 is fully supported

# Post processing

Megapixels only captures raw frames and stores .dng files. It captures a 5 frame burst and saves it to a temporary
location. Then the postprocessing script is run which will generate the final .jpg file and writes it into the 
pictures directory. Megapixels looks for the post processing script in the following locations:

* ./postprocess.sh
* $XDG_CONFIG_DIR/megapixels/postprocess.sh
* ~/.config/megapixels/postprocess.sh
* /etc/megapixels/postprocess.sh
* /usr/share/megapixels/postprocess.sh

The bundled postprocess.sh script will copy the first frame of the burst into the picture directory as an DNG
file and if dcraw and imagemagick are installed it will generate a JPG and also write that to the picture
directory. It supports either the full dcraw or dcraw_emu from libraw.

It is possible to write your own post processing pipeline my providing your own `postprocess.sh` script at
one of the above locations. The first argument to the script is the directory containing the temporary 
burst files and the second argument is the final path for the image without an extension. For more details
see postprocess.sh in this repository.

# Developing

See the mailing list and issue tracker on https://sr.ht/~martijnbraam/Megapixels/

To send patches, follow this procedure:

1. Change the default subject prefix from "PATCH" to "PATCH Megapixels" by
   running this command (only needed once).
   ```shell-session
   $ git config --local format.subjectPrefix "PATCH Megapixels"
   ```
2. Rebase your commits on top of the latest `master`.
3. Send them to the mailing list:
   ```shell-session
   $ git send-email --to="~martijnbraam/public-inbox@lists.sr.ht" origin/master
   ```

## Source code organization

* `ini.c` contains a INI file format parser.
* `camera_config.c` describes how cameras are configured. Contains no state.
* `main.c` contains the entry point and UI portion of the application.
* `quickpreview.c` implements fast preview functionality, including debayering, color correction, rotation, etc.
* `io_pipeline.c` implements all IO interaction with V4L2 devices in a separate thread to prevent blocking.
* `process_pipeline.c` implements all process done on captured images, including launching post-processing
* `pipeline.c` Generic threaded message passing implementation based on glib, used to implement the pipelines.
* `camera.c` V4L2 abstraction layer to make working with cameras easier
* `device.c` V4L2 abstraction layer for devices

The primary image pipeline consists of the main application, the IO pipeline and
the process pipeline. The main application sends commands to the IO pipeline,
which in turn talks to the process pipeline, which then talks to the main
application. This way neither IO nor processing blocks the main application and
races are generally avoided.

Tests are located in `tests/`.

## Tools

All tools are contained in `tools/`

* `list_devices` lists all V4L2 devices and their hardware layout
* `camera_test` lists controls and video modes of a specific camera and tests capturing data from it

## Linux video subsystem 

Most of the logic is contained inside `main.c`, but before we look at it, it is
convenient to have some basic notions about the Linux video subsystem that
Megapixels directly uses (instead of, for example, using a higher level
framework such as "gstreamer", as other camera apps do).

Typically, for "simple" video capture devices (such as some old webcams on a
PC), the Linux kernel creates an entry on `/dev/` called `/dev/videoX` (where X
can be `0`, `1`, ...). The user can then `open()` that file descriptor, use
standard `ioctl()`s on it to start/stop/configure the hardware and finally
`read()` from it to obtain individual video frames.

In the PinePhone we have two cameras ("front" and "rear") but, surprinsingly,
the Linux kernel does not expose two video devices but just a single one named
`/dev/video1`.

This is because, on the PinePhone, there is one single "capture device" and two
"image sensors" (one for each camera) attached to it:

```
    .-----------.         .--------------.
    |           |---------| front sensor ))))))
    |  Sensors  |         '--------------'
    | interface |         .--------------.
    |           |---------| rear sensor  ))))))
    '-----------'         '--------------'
```

The only video device exposed (`/dev/video1`) represents the "sensors interface"
block, which can be configured at runtime to capture data from one sensor or the
other.

But there is more: in order to configure the properties of each sensor (example:
capture frame rate, auto exposure, ...), instead of issuing `ioctl()` calls on
`/dev/video1`, the Linux kernel (for this particular case) exposes two extra
devices (`/dev/v4l-subdev0` for one sensor and `/dev/v4l-subdev1` for the other
one)

How does the user know that `/dev/v4l-subdev0`, `/dev/v4l-subdev1` and
`/dev/video1` are related? Thanks to the "media subsystem": for "complex" cases
such as this one, the Linux kernel exposes an extra device (`/dev/mediaX`, where
X can be `0`, `1`, ...) that can be used to...

* Obtain the list of related devices to that "media interface". 
* Link/unlink the different "blocks" at runtime.

Pheeew.... let's recap what we have to far:

* `/dev/mediaW` represents the "whole camera hardware"
* `/dev/videoX` is the "sensors interface" from where we will `read()` frames.
* `/dev/vl4-subdevY` and `/dev/vl4-subdevZ` can be used to configure the
  sensors.

Notice how I used `W`, `X`, `Y` and `Z` instead of numbers. In the current
kernel `W==1`, `X==0`, `Y==0` and `Z==1`, but that might change in the future.
That's why `main()` needs to figure them out by following this procedure:

1. List all `/dev/mediaX` devices present (ex: `/dev/media0`, `/dev/media1`,
   ...)
2. Query each of them with `ioctl(MEDIA_IOC_DEVICE_INFO)` until we find the
   entry managed by a driver named "sun6i-csi" (as that is the name of the
   driver of the sensor interface for the [Allwinner SoC camera
   sensor](https://linux-sunxi.org/CSI) that the PinePhone uses, which is
   provided on the `*.ini` file).
3. Obtain a list of elements associated to that "media device" by calling
   `ioctl(MEDIA_IOC_ENUM_ENTITIES)`.
4. The entry called "ov5640" is the rear camera (as that is the name of the
   driver of the rear sensor, which is provided on the `*.ini` file). Save its
   device name (ex: `/dev/v4l-subdev1`) for later.
5. The entry called "gc2145" is the front camera (as that is the name of the
   driver of the front sensor, which is provided on the `*.ini` file). Save its
   device name (ex: `/dev/v4l-subdev0`) for later.
6. The entry called "sun6i-csi" is the sensors interface (same name as the
   driver in charge of the `/dev/mediaX` interface). Save its device name (ex:
   `/dev/video1`) for later.

By the way, regarding steps 1 and 2, you can manually inspect the list of
"elements" that are related to a given `/dev/mediaX` entry from user space using
the `media-ctl` tool. This is what the current kernel and hardware revision
return:
```shell-session
$ media-tcl -d /dev/media1 -p

Media controller API version 5.7.19
 
Media device information
------------------------
driver          sun6i-csi
model           Allwinner Video Capture Device
serial          
bus info        
hw revision     0x0
driver version  5.7.19
 
Device topology
- entity 1: sun6i-csi (1 pad, 2 links)
            type Node subtype V4L flags 0
            device node name /dev/video1
        pad0: Sink
                <- "gc2145 4-003c":0 []
                <- "ov5640 4-004c":0 [ENABLED]
 
- entity 5: gc2145 4-003c (1 pad, 1 link)
            type V4L2 subdev subtype Sensor flags 0
            device node name /dev/v4l-subdev0
        pad0: Source
                [fmt:YUYV8_2X8/1280x720@1/10 field:none colorspace:srgb]
                -> "sun6i-csi":0 []
 
- entity 7: ov5640 4-004c (1 pad, 1 link)
            type V4L2 subdev subtype Sensor flags 0
            device node name /dev/v4l-subdev1
        pad0: Source
                [fmt:YUYV8_2X8/1280x720@1/30 colorspace:srgb xfer:srgb ycbcr:601 quantization:full-range]
                -> "sun6i-csi":0 [ENABLED]
```
...which means what we already know: `sun6i-csi` is the sensors interface sink
(on `/dev/video1`) where the two sensors (`gc2145` on `/dev/v4l-subdev0` and
`ov5640` on `/dev/v4l-subdev1` are connected). By default (or, at least, in the
example above) the sensors interface is connected to the rear camera (`ov5640`)
as its link is the only one "ENABLED".

Anyway... once `main()` has figured out the values of `W`, `X`, `Y` and `Z`,
this is how all these device entries are used to manage the camera hardware:

* Use `ioctl(MEDIA_IOC_SETUP_LINK)` on the `/dev/mediaW` entry to "link" the
  sensors interface with either the rear sensor or the front sensor (this is
  how we choose from which camera we will be capturing frames)
* Use `ioctl(VIDIOC_SUBDEV_...)` on `/dev/v4l-subdev{Y,Z}` to configure the
  sensors.
* Use `ioctl(VIDIOC_...)` on `/dev/videoX` to configure the sensors interface.
* Use `read()` on `/dev/videoX` to capture frames.

The mechanism described on the last point (ie. use `read()` to capture frames),
while possible, is not actually what `main()` does. Instead, a more complex
mechanism (described
[here](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/io.html))
is used, where a series of buffers are allocated, sent to `/dev/videoX` with
`ioctl(VIDIOC_QBUF)` and then retrieved with `ioctl(VIDIOC_DQBUF)` once they
have been filled with video frames (after having called
`ioctl(VIDIOC_STREAMON)`)... but it is basically the same as performing a
`read()` (except that it has more flexibility).

## Source code walkthrough

As we have just seen on the [previous section](#linux-video-subsystem), in the
current kernel version, and for the latest PinePhone revision (1.2a), the Linux
kernel exposes 4 device entries to manage the camera hardware:

* `/dev/media1` to select the active camera ("front" or "rear")
* `/dev/vl4-subdev0` and `/dev/vl4-subdev1` to configure the sensor of each
  camera (aperture, auto exposure, etc...)
* `/dev/video1` to capture frames (video stream and/or pictures)

However these device entries might change with future versions of the kernel
and/or the hardware (for example, `/dev/video3` instead of `/dev/video1`), and
that's why function `main()` in `main.c` starts by trying to figure out the
correct names.

It does so by checking the hardware revision in `/proc/device-tree/compatible`
and then opening the corresponding `.ini` file from the config folder (ex:
`pine64,pinephone-1.2.ini` for the latest PinePhone revision as of today,
`pine64,pinetab.ini` for the PineTab, etc...).

The `.ini` file contains the name of the driver that manages the `/dev/mediaX`
interface (`csi` entry on the `device` section) and, from there, `main()` can
figure out the rest of the device names as already explained on the [previous
section](#linux-video-subsystem).


```
    /proc/device-tree/compatible
        |
        |
        V
    config/*.ini ---------------.
        |                       |
        |                       V
        |          .~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        |          :                           :
        |          :  .----> /dev/video1       :
        V          :  |                        :
    /dev/media1 ------+----> /dev/v4l-subdev0  :
                   :  |                        :
                   :  '----> /dev/v4l-subdev1  :
                   :                           :
                   '~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

Anyway... in addition to figuring out these entry names, `main()` also prepares
the GTK widgets layout and installs a series of callbacks. Among them we find
these two:

1. One on the "switch camera button" (`on_camera_switch_clicked()`) which uses
   `/dev/media1` to switch between the front and rear cameras.
   Every time this happens, the sensors and the sensors interface are
   reconfigured according to the parameters provided on the `.ini` file using
   `/dev/video1`, `/dev/v4l-subdev0` and `/v4l-subdev1`.
   ```
   on_camera_switch_clicked()
   |
   |--> stop_capturing()
   |    `--> ioctl('/dev/video1', ...)           # Stop processing frames
   |
   |--> setup_front() or setup_rear()
   |    |--> ioctl('/dev/media1', ...)
   |    `--> init_sensor()
   |         `--> ioctl('/dev/v4l-subdev{0,1}')  # Reconfigure sensor
   |         
   |--> init_device()
   |    `--> ioctl('/dev/video1')                # Reconfigure sensors interface
   |
   `--> start_capturing()
        `--> ioctl('/dev/video1')                # Resume capturing frames
    
   ```
2. Another one on the "take a photo button" (`on_shutter_clicked()`) which
   will use `/dev/v4l-subdev{0,1}` to disable hardware "auto gain" and "auto
   exposure" and initiate the "single frame capture process" (described later).

Finally, before calling GTK's main loop, `main()` installs another function
(`get_frame()`) on the "nothing else todo" GTK slot. It will thus be called
continuosly as long as there are no other GTK events queued (ie. almost always).

This `get_frame()` function is where the magic happens: it will call
`read_frame()` to `read()` from the `/dev/video1` device an image frame and
then call `process_image()` to process it.

> NOTE: As explained at the end of the [Linux video subsystem
> section](linux-video-subsystem), it is a bit more complex than that (that's
> why you will find a `ioctl()` instead of a `read()` inside `read_frame()`),
> but for all purposes, you can ignore this fact.

So... let's recap: as long as the user does not click on any application button,
the `process_image()` function is being called all the time with a pointer to
the latest captured frame. What does it do with it?

The captured frame buffer contains "RAW data", whose format depends on the value
specified on the `.ini` file for each sensor. Right now we are using `BGGR8` for
both of them, so the function that takes this buffer to process it is always the
same (`quick_debayer_bggr8()`). The result is a buffer of "standard pixels" that
can be drawn to screen using GTK/cairo functions.

When the user clicks on the "take a photo button", however, a special global
variable (`capture`) is set so that the next `N` times (currently `N==10`), the
`process_image()` will do something different:
1. It will first retrieve the latest "auto gain" and "auto exposure" values
   (remember they were disabled when the user clicked on the "take a photo
   button").
2. It will save the latest captured buffer (in "RAW data" format, ie. `BGGR8`)
   to a `.dng` file using the "TIFF" library, which makes it possible to attach
   all the needed metadata (which Megapixels extracts from the hardware itself
   and/or the values on the `.ini` file).
3. In addition, **only** the very last time (from the `N` times):
     - The captured buffer is run through `quick_debayer_bggr8()` and the result
       printed to the UI.
     - The `postprocess.sh` script (see the [Post processing
       section](#post-processing)) is called with two arguments: the path to the
       `/tmp` folder where the `N` `.dng` images have been saved and the path
       and filename where the resulting post-processed (typically JPEG) image
       should be saved to (as a result of running `postprocess.sh`)
     - "Auto exposure" and "auto gain" are re-enabled.

In other words: every time the user clicks on the "take a photo button", `N`
RAW images are saved and `postprocess.sh` called, which is expected to take
those `N` images and generate a final JPEG.

