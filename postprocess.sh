#!/bin/sh

# The post-processing script gets called after taking a burst of
# pictures into a temporary directory. The first argument is the
# directory containing the raw files in the burst. The contents
# are 1.dng, 2.dng.... up to the number of photos in the burst.
#
# The second argument is the filename for the final photo without
# the extension, like "/home/user/Pictures/IMG202104031234" 
#
# The post-processing script is responsible for cleaning up
# temporary directory for the burst.

if [ "$#" -ne 2 ]; then
	echo "Usage: $0 [burst-dir] [target-name]"
	exit 2
fi

BURST_DIR="$1"
TARGET_NAME="$2"

# Copy the first frame of the burst as the raw photo
cp "$BURST_DIR"/1.dng "$TARGET_NAME.dng"

# Create a .jpg if raw processing tools are installed
DCRAW=""
if command -v "dcraw_emu" &> /dev/null
then
	DCRAW=dcraw_emu
fi
if command -v "dcraw" &> /dev/null
then
	DCRAW=dcraw
fi

if [ -n "$DCRAW" ]; then
	# +M		use embedded color matrix
	# -H 4		Recover highlights by rebuilding them
	# -o 1		Output in sRGB colorspace
	# -q 3		Debayer with AHD algorithm
	# -T		Output TIFF
	# -fbdd 1	Raw denoising with FBDD
	$DCRAW +M -H 4 -o 1 -q 3 -T -fbdd 1 $BURST_DIR/1.dng

	if command -v convert &> /dev/null
	then
		convert "$BURST_DIR"/1.dng.tiff "$TARGET_NAME.jpg"

		# If exiftool is installed copy the exif data over from the tiff to the jpeg
		# since imagemagick is stupid
		if command -v exiftool &> /dev/null
		then
			exiftool -tagsFromfile "$BURST_DIR"/1.dng.tiff \
				 -software="Megapixels" \
				 -overwrite_original "$TARGET_NAME.jpg"
		fi

	else
		cp "$BURST_DIR"/1.dng.tiff "$TARGET_NAME.tiff"
	fi
fi

# Clean up the temp dir containing the burst
rm -rf "$BURST_DIR"
