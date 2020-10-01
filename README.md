# Megapixels

A GTK3 camera application that knows how to deal with the media request api

## Building

```shell-session
$ meson build
$ cd build
$ ninja
$ sudo ninja install
```

# Developing

See the mailing list and issue tracker on https://sr.ht/~martijnbraam/Megapixels/

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

This provides global info, currently only the `csi` key exists, telling megapixels which device in the 
media-ctl tree is the interface to the kernel. This should provide the /dev/video* node.

### [rear] and [front]

These are the sections describing the sensors.

* `driver=ov5640` the name of the media node that provides the sensor and it's /dev/v4l-subdev* node.
* `width=640` and `height=480` the resolution to use for the sensor
* `rate=15` the refresh rate in fps to use for the sensor
* `fmt=BGGR8` sets the pixel and bus formats used when capturing from the sensor, only BGGR8 is fully supported
* `rotate=90` the rotation angle to make the sensor match the screen
* `colormatrix=` the DNG colormatrix1 attribute as 9 comma seperated floats
* `forwardmatrix=` the DNG forwardmatrix1 attribute as 9 comma seperated floats
* `blacklevel=10` The DNG blacklevel attribute for this camera
* `whitelevel=255` The DNG whitelevel attribute for this camera
* `focallength=3.33` The focal length of the camera, for EXIF
* `cropfactor=10.81` The cropfactor for the sensor in the camera, for EXIF
* `fnumber=3.0` The aperture size of the sensor, for EXIF

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
