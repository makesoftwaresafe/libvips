/* save to tiff
 *
 * 2/12/11
 * 	- wrap a class around the tiff writer
 * 17/3/12
 * 	- argh xres/yres macro was wrong
 * 26/1/14
 * 	- add rgbjpeg flag
 * 21/12/15
 * 	- add properties flag
 * 31/5/16
 * 	- convert for jpg if jpg compression is on
 * 19/10/17
 * 	- predictor defaults to horizontal, reducing file size, usually
 * 13/6/18
 * 	- add region_shrink
 * 8/7/19
 * 	- add webp and zstd support
 * 	- add @level and @lossless
 * 4/9/18 [f--f]
 * 	- xres/yres params were in pixels/cm
 * 26/1/20
 * 	- add "depth" to set pyr depth
 * 12/5/20
 * 	- add "subifd" to create pyr layers as sub-directories
 * 8/6/20
 * 	- add bitdepth support for 2 and 4 bit greyscale images
 * 	- deprecate "squash"
 * 1/5/21
 * 	- add "premultiply" flag
 * 10/5/22
 * 	- add vips_tiffsave_target()
 */

/*

	This file is part of VIPS.

	VIPS is free software; you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
	02110-1301  USA

 */

/*

	These files are distributed with VIPS - http://www.vips.ecs.soton.ac.uk

 */

/*
#define DEBUG_VERBOSE
#define DEBUG
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /*HAVE_CONFIG_H*/
#include <glib/gi18n-lib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vips/vips.h>
#include <vips/internal.h>

#include "pforeign.h"

#ifdef HAVE_TIFF

#include "tiff.h"

typedef struct _VipsForeignSaveTiff {
	VipsForeignSave parent_object;

	/* Set by subclasses.
	 */
	VipsTarget *target;

	/* Many options argh.
	 */
	VipsForeignTiffCompression compression;
	int Q;
	VipsForeignTiffPredictor predictor;
	gboolean tile;
	int tile_width;
	int tile_height;
	gboolean pyramid;
	gboolean squash;
	int bitdepth;
	gboolean miniswhite;
	VipsForeignTiffResunit resunit;
	double xres;
	double yres;
	gboolean bigtiff;
	gboolean rgbjpeg;
	gboolean properties;
	VipsRegionShrink region_shrink;
	int level;
	gboolean lossless;
	VipsForeignDzDepth depth;
	gboolean subifd;
	gboolean premultiply;

} VipsForeignSaveTiff;

typedef VipsForeignSaveClass VipsForeignSaveTiffClass;

G_DEFINE_ABSTRACT_TYPE(VipsForeignSaveTiff, vips_foreign_save_tiff,
	VIPS_TYPE_FOREIGN_SAVE);

static void
vips_foreign_save_tiff_dispose(GObject *gobject)
{
	VipsForeignSaveTiff *tiff = (VipsForeignSaveTiff *) gobject;

	VIPS_UNREF(tiff->target);

	G_OBJECT_CLASS(vips_foreign_save_tiff_parent_class)->dispose(gobject);
}

#define UC VIPS_FORMAT_UCHAR

/* Type promotion for jpeg-in-tiff save ... just always go to uchar.
 */
static VipsBandFormat bandfmt_jpeg[10] = {
	/* Band format:  UC  C   US  S   UI  I   F   X   D   DX */
	/* Promotion: */ UC, UC, UC, UC, UC, UC, UC, UC, UC, UC
};

static int
vips_foreign_save_tiff_build(VipsObject *object)
{
	VipsForeignSaveClass *class = VIPS_FOREIGN_SAVE_GET_CLASS(object);
	VipsForeignSave *save = (VipsForeignSave *) object;
	VipsForeignSaveTiff *tiff = (VipsForeignSaveTiff *) object;

	if (VIPS_OBJECT_CLASS(vips_foreign_save_tiff_parent_class)->build(object))
		return -1;

	VipsImage *ready = save->ready;
	g_object_ref(ready);

	/* If we are saving jpeg-in-tiff, we need a different convert_saveable
	 * path. The regular tiff one will let through things like float and
	 * 16-bit and alpha for example, which will make the jpeg saver choke.
	 */
	if (tiff->compression == VIPS_FOREIGN_TIFF_COMPRESSION_JPEG) {
		VipsImage *x;

		/* See also vips_foreign_save_jpeg_class_init().
		 */
		if (vips__foreign_convert_saveable(ready, &x,
				VIPS_FOREIGN_SAVEABLE_MONO |
					VIPS_FOREIGN_SAVEABLE_RGB |
					VIPS_FOREIGN_SAVEABLE_CMYK,
				bandfmt_jpeg, class->coding,
				save->background)) {
			VIPS_UNREF(ready);
			return -1;
		}

		VIPS_UNREF(ready);
		ready = x;
	}

	/* resunit param overrides resunit metadata.
	 */
	VipsForeignTiffResunit resunit = tiff->resunit;
	const char *p;
	if (!vips_object_argument_isset(object, "resunit") &&
		vips_image_get_typeof(ready, VIPS_META_RESOLUTION_UNIT) &&
		!vips_image_get_string(ready, VIPS_META_RESOLUTION_UNIT, &p) &&
		vips_isprefix("in", p))
		resunit = VIPS_FOREIGN_TIFF_RESUNIT_INCH;

	double xres = vips_object_argument_isset(object, "xres")
		? tiff->xres
		: ready->Xres;

	double yres = vips_object_argument_isset(object, "yres")
		? tiff->yres
		: ready->Yres;

	if (tiff->resunit == VIPS_FOREIGN_TIFF_RESUNIT_INCH) {
		xres *= 25.4;
		yres *= 25.4;
	}
	else {
		xres *= 10.0;
		yres *= 10.0;
	}

	if (vips__tiff_write_target(ready, tiff->target,
			tiff->compression, tiff->Q, tiff->predictor,
			save->profile,
			tiff->tile, tiff->tile_width, tiff->tile_height,
			tiff->pyramid,
			// deprecated "squash" param
			tiff->squash ? 1 : tiff->bitdepth,
			tiff->miniswhite,
			resunit, xres, yres,
			tiff->bigtiff,
			tiff->rgbjpeg,
			tiff->properties,
			tiff->region_shrink,
			tiff->level,
			tiff->lossless,
			tiff->depth,
			tiff->subifd,
			tiff->premultiply,
			save->page_height)) {
		VIPS_UNREF(ready);
		return -1;
	}

	VIPS_UNREF(ready);

	if (vips_target_end(tiff->target))
		return -1;

	return 0;
}

static void
vips_foreign_save_tiff_class_init(VipsForeignSaveTiffClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsForeignClass *foreign_class = (VipsForeignClass *) class;
	VipsForeignSaveClass *save_class = (VipsForeignSaveClass *) class;

	gobject_class->dispose = vips_foreign_save_tiff_dispose;
	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "tiffsave_base";
	object_class->description = _("save image as tiff");
	object_class->build = vips_foreign_save_tiff_build;

	foreign_class->suffs = vips__foreign_tiff_suffs;

	save_class->saveable = VIPS_FOREIGN_SAVEABLE_ANY;
	save_class->coding |= VIPS_FOREIGN_CODING_LABQ;

	VIPS_ARG_ENUM(class, "compression", 6,
		_("Compression"),
		_("Compression for this file"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, compression),
		VIPS_TYPE_FOREIGN_TIFF_COMPRESSION,
		VIPS_FOREIGN_TIFF_COMPRESSION_NONE);

	VIPS_ARG_INT(class, "Q", 7,
		_("Q"),
		_("Q factor"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, Q),
		1, 100, 75);

	VIPS_ARG_ENUM(class, "predictor", 8,
		_("Predictor"),
		_("Compression prediction"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, predictor),
		VIPS_TYPE_FOREIGN_TIFF_PREDICTOR,
		VIPS_FOREIGN_TIFF_PREDICTOR_HORIZONTAL);

	VIPS_ARG_BOOL(class, "tile", 10,
		_("Tile"),
		_("Write a tiled tiff"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, tile),
		FALSE);

	VIPS_ARG_INT(class, "tile_width", 11,
		_("Tile width"),
		_("Tile width in pixels"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, tile_width),
		1, 32768, 128);

	VIPS_ARG_INT(class, "tile_height", 12,
		_("Tile height"),
		_("Tile height in pixels"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, tile_height),
		1, 32768, 128);

	VIPS_ARG_BOOL(class, "pyramid", 13,
		_("Pyramid"),
		_("Write a pyramidal tiff"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, pyramid),
		FALSE);

	VIPS_ARG_BOOL(class, "miniswhite", 14,
		_("Miniswhite"),
		_("Use 0 for white in 1-bit images"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, miniswhite),
		FALSE);

	VIPS_ARG_INT(class, "bitdepth", 15,
		_("Bit depth"),
		_("Write as a 1, 2, 4 or 8 bit image"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, bitdepth),
		0, 8, 0);

	VIPS_ARG_ENUM(class, "resunit", 16,
		_("Resolution unit"),
		_("Resolution unit"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, resunit),
		VIPS_TYPE_FOREIGN_TIFF_RESUNIT, VIPS_FOREIGN_TIFF_RESUNIT_CM);

	VIPS_ARG_DOUBLE(class, "xres", 17,
		_("Xres"),
		_("Horizontal resolution in pixels/mm"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, xres),
		0.001, 1000000, 1);

	VIPS_ARG_DOUBLE(class, "yres", 18,
		_("Yres"),
		_("Vertical resolution in pixels/mm"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, yres),
		0.001, 1000000, 1);

	VIPS_ARG_BOOL(class, "bigtiff", 19,
		_("Bigtiff"),
		_("Write a bigtiff image"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, bigtiff),
		FALSE);

	VIPS_ARG_BOOL(class, "properties", 21,
		_("Properties"),
		_("Write a properties document to IMAGEDESCRIPTION"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, properties),
		FALSE);

	VIPS_ARG_ENUM(class, "region_shrink", 22,
		_("Region shrink"),
		_("Method to shrink regions"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, region_shrink),
		VIPS_TYPE_REGION_SHRINK, VIPS_REGION_SHRINK_MEAN);

	VIPS_ARG_INT(class, "level", 23,
		_("Level"),
		_("Deflate (1-9, default 6) or ZSTD (1-22, default 9) compression level"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, level),
		0, 22, 0);

	VIPS_ARG_BOOL(class, "lossless", 24,
		_("Lossless"),
		_("Enable WEBP lossless mode"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, lossless),
		FALSE);

	VIPS_ARG_ENUM(class, "depth", 25,
		_("Depth"),
		_("Pyramid depth"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, depth),
		VIPS_TYPE_FOREIGN_DZ_DEPTH, VIPS_FOREIGN_DZ_DEPTH_ONETILE);

	VIPS_ARG_BOOL(class, "subifd", 26,
		_("Sub-IFD"),
		_("Save pyr layers as sub-IFDs"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, subifd),
		FALSE);

	VIPS_ARG_BOOL(class, "premultiply", 27,
		_("Premultiply"),
		_("Save with premultiplied alpha"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, premultiply),
		FALSE);

	VIPS_ARG_BOOL(class, "rgbjpeg", 28,
		_("RGB JPEG"),
		_("Output RGB JPEG rather than YCbCr"),
		VIPS_ARGUMENT_OPTIONAL_INPUT | VIPS_ARGUMENT_DEPRECATED,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, rgbjpeg),
		FALSE);

	VIPS_ARG_BOOL(class, "squash", 29,
		_("Squash"),
		_("Squash images down to 1 bit"),
		VIPS_ARGUMENT_OPTIONAL_INPUT | VIPS_ARGUMENT_DEPRECATED,
		G_STRUCT_OFFSET(VipsForeignSaveTiff, squash),
		FALSE);
}

static void
vips_foreign_save_tiff_init(VipsForeignSaveTiff *tiff)
{
	tiff->compression = VIPS_FOREIGN_TIFF_COMPRESSION_NONE;
	tiff->Q = 75;
	tiff->predictor = VIPS_FOREIGN_TIFF_PREDICTOR_HORIZONTAL;
	tiff->tile_width = 128;
	tiff->tile_height = 128;
	tiff->resunit = VIPS_FOREIGN_TIFF_RESUNIT_CM;
	tiff->xres = 1.0;
	tiff->yres = 1.0;
	tiff->region_shrink = VIPS_REGION_SHRINK_MEAN;
	tiff->level = 0;
	tiff->lossless = FALSE;
	tiff->depth = VIPS_FOREIGN_DZ_DEPTH_ONETILE;
	tiff->bitdepth = 0;
}

typedef struct _VipsForeignSaveTiffTarget {
	VipsForeignSaveTiff parent_object;

	VipsTarget *target;
} VipsForeignSaveTiffTarget;

typedef VipsForeignSaveTiffClass VipsForeignSaveTiffTargetClass;

G_DEFINE_TYPE(VipsForeignSaveTiffTarget, vips_foreign_save_tiff_target,
	vips_foreign_save_tiff_get_type());

static int
vips_foreign_save_tiff_target_build(VipsObject *object)
{
	VipsForeignSaveTiff *tiff = (VipsForeignSaveTiff *) object;
	VipsForeignSaveTiffTarget *target =
		(VipsForeignSaveTiffTarget *) object;

	tiff->target = target->target;
	g_object_ref(tiff->target);

	return VIPS_OBJECT_CLASS(vips_foreign_save_tiff_target_parent_class)
		->build(object);
}

static void
vips_foreign_save_tiff_target_class_init(
	VipsForeignSaveTiffTargetClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "tiffsave_target";
	object_class->description = _("save image to tiff target");
	object_class->build = vips_foreign_save_tiff_target_build;

	VIPS_ARG_OBJECT(class, "target", 1,
		_("Target"),
		_("Target to save to"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiffTarget, target),
		VIPS_TYPE_TARGET);
}

static void
vips_foreign_save_tiff_target_init(VipsForeignSaveTiffTarget *target)
{
}

typedef struct _VipsForeignSaveTiffFile {
	VipsForeignSaveTiff parent_object;

	char *filename;
} VipsForeignSaveTiffFile;

typedef VipsForeignSaveTiffClass VipsForeignSaveTiffFileClass;

G_DEFINE_TYPE(VipsForeignSaveTiffFile, vips_foreign_save_tiff_file,
	vips_foreign_save_tiff_get_type());

static int
vips_foreign_save_tiff_file_build(VipsObject *object)
{
	VipsForeignSaveTiff *tiff = (VipsForeignSaveTiff *) object;
	VipsForeignSaveTiffFile *file = (VipsForeignSaveTiffFile *) object;

	if (!(tiff->target = vips_target_new_to_file(file->filename)))
		return -1;

	return VIPS_OBJECT_CLASS(vips_foreign_save_tiff_file_parent_class)
		->build(object);
}

static void
vips_foreign_save_tiff_file_class_init(VipsForeignSaveTiffFileClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "tiffsave";
	object_class->description = _("save image to tiff file");
	object_class->build = vips_foreign_save_tiff_file_build;

	VIPS_ARG_STRING(class, "filename", 1,
		_("Filename"),
		_("Filename to save to"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiffFile, filename),
		NULL);
}

static void
vips_foreign_save_tiff_file_init(VipsForeignSaveTiffFile *file)
{
}

typedef struct _VipsForeignSaveTiffBuffer {
	VipsForeignSaveTiff parent_object;

	VipsArea *buf;
} VipsForeignSaveTiffBuffer;

typedef VipsForeignSaveTiffClass VipsForeignSaveTiffBufferClass;

G_DEFINE_TYPE(VipsForeignSaveTiffBuffer, vips_foreign_save_tiff_buffer,
	vips_foreign_save_tiff_get_type());

static int
vips_foreign_save_tiff_buffer_build(VipsObject *object)
{
	VipsForeignSaveTiff *tiff = (VipsForeignSaveTiff *) object;
	VipsForeignSaveTiffBuffer *buffer =
		(VipsForeignSaveTiffBuffer *) object;

	VipsBlob *blob;

	if (!(tiff->target = vips_target_new_to_memory()))
		return -1;

	if (VIPS_OBJECT_CLASS(vips_foreign_save_tiff_buffer_parent_class)
			->build(object))
		return -1;

	g_object_get(tiff->target, "blob", &blob, NULL);
	g_object_set(buffer, "buffer", blob, NULL);
	vips_area_unref(VIPS_AREA(blob));

	return 0;
}

static void
vips_foreign_save_tiff_buffer_class_init(
	VipsForeignSaveTiffBufferClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "tiffsave_buffer";
	object_class->description = _("save image to tiff buffer");
	object_class->build = vips_foreign_save_tiff_buffer_build;

	VIPS_ARG_BOXED(class, "buffer", 1,
		_("Buffer"),
		_("Buffer to save to"),
		VIPS_ARGUMENT_REQUIRED_OUTPUT,
		G_STRUCT_OFFSET(VipsForeignSaveTiffBuffer, buf),
		VIPS_TYPE_BLOB);
}

static void
vips_foreign_save_tiff_buffer_init(VipsForeignSaveTiffBuffer *buffer)
{
}

#endif /*HAVE_TIFF*/

/**
 * vips_tiffsave: (method)
 * @in: image to save
 * @filename: file to write to
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Write a VIPS image to a file as TIFF.
 *
 * If @in has the [const@META_PAGE_HEIGHT] metadata item, this is assumed to be a
 * "toilet roll" image. It will be
 * written as series of pages, each [const@META_PAGE_HEIGHT] pixels high.
 *
 * Use @compression to set the tiff compression. Currently jpeg, packbits,
 * fax4, lzw, none, deflate, webp and zstd are supported. The default is no
 * compression.
 * JPEG compression is a good lossy compressor for photographs, packbits is
 * good for 1-bit images, and deflate is the best lossless compression TIFF
 * can do.
 *
 * XYZ images are automatically saved as libtiff LOGLUV with SGILOG compression.
 * Float LAB images are saved as float CIELAB. Set @bitdepth to save as 8-bit
 * CIELAB.
 *
 * Use @Q to set the JPEG compression factor. Default 75.
 *
 * User @level to set the ZSTD (1-22) or Deflate (1-9) compression level. Use @lossless to
 * set WEBP lossless mode on. Use @Q to set the WEBP compression level.
 *
 * Use @predictor to set the predictor for lzw, deflate and zstd compression.
 * It defaults to [enum@Vips.ForeignTiffPredictor.HORIZONTAL], meaning horizontal
 * differencing. Please refer to the libtiff
 * specifications for further discussion of various predictors.
 *
 * Set @tile to `TRUE` to write a tiled tiff.  By default tiff are written in
 * strips. Use @tile_width and @tile_height to set the tile size. The defaiult
 * is 128 by 128.
 *
 * Set @pyramid to write the image as a set of images, one per page, of
 * decreasing size. Use @region_shrink to set how images will be shrunk: by
 * default each 2x2 block is just averaged, but you can set MODE or MEDIAN as
 * well.
 *
 * By default, the pyramid stops when the image is small enough to fit in one
 * tile. Use @depth to stop when the image fits in one pixel, or to only write
 * a single layer.
 *
 * Set @bitdepth to save 8-bit uchar images as 1, 2 or 4-bit TIFFs.
 * In case of depth 1: Values >128 are written as white, values <=128 as black.
 * Normally vips will write MINISBLACK TIFFs where black is a 0 bit, but if you
 * set @miniswhite, it will use 0 for a white bit. Many pre-press applications
 * only work with images which use this sense. @miniswhite only affects one-bit
 * images, it does nothing for greyscale images.
 * In case of depth 2: The same holds but values < 64 are written as black.
 * For 64 <= values < 128 they are written as dark grey, for 128 <= values < 192
 * they are written as light gray and values above are written as white.
 * In case @miniswhite is set to true this behavior is inverted.
 * In case of depth 4: values < 16 are written as black, and so on for the
 * lighter shades. In case @miniswhite is set to true this behavior is inverted.
 *
 * Use @resunit to override the default resolution unit.
 * The default
 * resolution unit is taken from the header field
 * [const@META_RESOLUTION_UNIT]. If this field is not set, then
 * VIPS defaults to cm.
 *
 * Use @xres and @yres to override the default horizontal and vertical
 * resolutions. By default these values are taken from the VIPS image header.
 * libvips resolution is always in pixels per millimetre.
 *
 * Set @bigtiff to attempt to write a bigtiff. Bigtiff is a variant of the TIFF
 * format that allows more than 4GB in a file.
 *
 * Set @properties to write all vips metadata to the IMAGEDESCRIPTION tag as
 * xml. If @properties is not set, the value of [const@META_IMAGEDESCRIPTION] is
 * used instead.
 *
 * The value of [const@META_XMP_NAME] is written to
 * the XMP tag. [const@META_ORIENTATION] (if set) is used to set the value of
 * the orientation
 * tag. [const@META_IPTC_NAME] (if set) is used to set the value of the IPTC tag.
 * [const@META_PHOTOSHOP_NAME] (if set) is used to set the value of the PHOTOSHOP
 * tag.
 *
 * By default, pyramid layers are saved as consecutive pages.
 * Set @subifd to save pyramid layers as sub-directories of the main image.
 * Setting this option can improve compatibility with formats like OME.
 *
 * Set @premultiply to save with premultiplied alpha. Some programs, such as
 * InDesign, will only work with premultiplied alpha.
 *
 * ::: tip "Optional arguments"
 *     * @compression: [enum@ForeignTiffCompression], write with this
 *       compression
 *     * @Q: `gint`, quality factor
 *     * @predictor: [enum@ForeignTiffPredictor], use this predictor
 *     * @tile: `gboolean`, set `TRUE` to write a tiled tiff
 *     * @tile_width: `gint`, for tile size
 *     * @tile_height: `gint`, for tile size
 *     * @pyramid: `gboolean`, write an image pyramid
 *     * @bitdepth: `gint`, change bit depth to 1,2, or 4 bit
 *     * @miniswhite: `gboolean`, write 1-bit images as MINISWHITE
 *     * @resunit: [enum@ForeignTiffResunit] for resolution unit
 *     * @xres: `gdouble`, horizontal resolution in pixels/mm
 *     * @yres: `gdouble`, vertical resolution in pixels/mm
 *     * @bigtiff: `gboolean`, write a BigTiff file
 *     * @properties: `gboolean`, set `TRUE` to write an IMAGEDESCRIPTION tag
 *     * @region_shrink: [enum@RegionShrink] How to shrink each 2x2 region.
 *     * @level: `gint`, Zstd or Deflate (zlib) compression level
 *     * @lossless: `gboolean`, WebP lossless mode
 *     * @depth: [enum@ForeignDzDepth] how deep to make the pyramid
 *     * @subifd: `gboolean`, write pyr layers as sub-ifds
 *     * @premultiply: `gboolean`, write premultiplied alpha
 *
 * ::: seealso
 *     [ctor@Image.tiffload], [method@Image.write_to_file].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_tiffsave(VipsImage *in, const char *filename, ...)
{
	va_list ap;
	int result;

	va_start(ap, filename);
	result = vips_call_split("tiffsave", ap, in, filename);
	va_end(ap);

	return result;
}

/**
 * vips_tiffsave_buffer: (method)
 * @in: image to save
 * @buf: (array length=len) (element-type guint8): return output buffer here
 * @len: (type gsize): return output length here
 * @...: `NULL`-terminated list of optional named arguments
 *
 * As [method@Image.tiffsave], but save to a memory buffer.
 *
 * The address of the buffer is returned in @buf, the length of the buffer in
 * @len. You are responsible for freeing the buffer with [func@GLib.free] when you
 * are done with it.
 *
 * ::: tip "Optional arguments"
 *     * @compression: [enum@ForeignTiffCompression], write with this
 *       compression
 *     * @Q: `gint`, quality factor
 *     * @predictor: [enum@ForeignTiffPredictor], use this predictor
 *     * @tile: `gboolean`, set `TRUE` to write a tiled tiff
 *     * @tile_width: `gint`, for tile size
 *     * @tile_height: `gint`, for tile size
 *     * @pyramid: `gboolean`, write an image pyramid
 *     * @bitdepth: `gint`, change bit depth to 1,2, or 4 bit
 *     * @miniswhite: `gboolean`, write 1-bit images as MINISWHITE
 *     * @resunit: [enum@ForeignTiffResunit] for resolution unit
 *     * @xres: `gdouble`, horizontal resolution in pixels/mm
 *     * @yres: `gdouble`, vertical resolution in pixels/mm
 *     * @bigtiff: `gboolean`, write a BigTiff file
 *     * @properties: `gboolean`, set `TRUE` to write an IMAGEDESCRIPTION tag
 *     * @region_shrink: [enum@RegionShrink] How to shrink each 2x2 region.
 *     * @level: `gint`, Zstd or Deflate (zlib) compression level
 *     * @lossless: `gboolean`, WebP lossless mode
 *     * @depth: [enum@ForeignDzDepth] how deep to make the pyramid
 *     * @subifd: `gboolean`, write pyr layers as sub-ifds
 *     * @premultiply: `gboolean`, write premultiplied alpha
 *
 * ::: seealso
 *     [method@Image.tiffsave], [method@Image.write_to_file].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_tiffsave_buffer(VipsImage *in, void **buf, size_t *len, ...)
{
	va_list ap;
	VipsArea *area;
	int result;

	area = NULL;

	va_start(ap, len);
	result = vips_call_split("tiffsave_buffer", ap, in, &area);
	va_end(ap);

	if (!result &&
		area) {
		if (buf) {
			*buf = area->data;
			area->free_fn = NULL;
		}
		if (len)
			*len = area->length;

		vips_area_unref(area);
	}

	return result;
}

/**
 * vips_tiffsave_target: (method)
 * @in: image to save
 * @target: save image to this target
 * @...: `NULL`-terminated list of optional named arguments
 *
 * As [method@Image.tiffsave], but save to a target.
 *
 * ::: tip "Optional arguments"
 *     * @compression: [enum@ForeignTiffCompression], write with this
 *       compression
 *     * @Q: `gint`, quality factor
 *     * @predictor: [enum@ForeignTiffPredictor], use this predictor
 *     * @tile: `gboolean`, set `TRUE` to write a tiled tiff
 *     * @tile_width: `gint`, for tile size
 *     * @tile_height: `gint`, for tile size
 *     * @pyramid: `gboolean`, write an image pyramid
 *     * @bitdepth: `gint`, change bit depth to 1,2, or 4 bit
 *     * @miniswhite: `gboolean`, write 1-bit images as MINISWHITE
 *     * @resunit: [enum@ForeignTiffResunit] for resolution unit
 *     * @xres: `gdouble`, horizontal resolution in pixels/mm
 *     * @yres: `gdouble`, vertical resolution in pixels/mm
 *     * @bigtiff: `gboolean`, write a BigTiff file
 *     * @properties: `gboolean`, set `TRUE` to write an IMAGEDESCRIPTION tag
 *     * @region_shrink: [enum@RegionShrink] How to shrink each 2x2 region.
 *     * @level: `gint`, Zstd or Deflate (zlib) compression level
 *     * @lossless: `gboolean`, WebP lossless mode
 *     * @depth: [enum@ForeignDzDepth] how deep to make the pyramid
 *     * @subifd: `gboolean`, write pyr layers as sub-ifds
 *     * @premultiply: `gboolean`, write premultiplied alpha
 *
 * ::: seealso
 *     [method@Image.tiffsave], [method@Image.write_to_target].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_tiffsave_target(VipsImage *in, VipsTarget *target, ...)
{
	va_list ap;
	int result;

	va_start(ap, target);
	result = vips_call_split("tiffsave_target", ap, in, target);
	va_end(ap);

	return result;
}
