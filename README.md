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
