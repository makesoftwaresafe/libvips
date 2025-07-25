/* foreign file formats base class
 *
 * 7/2/12
 * 	- add support for sequential reads
 * 18/6/12
 * 	- flatten alpha with vips_flatten()
 * 28/5/13
 * 	- auto rshift down to 8 bits during save
 * 19/1/14
 * 	- pack and unpack rad to scrgb
 * 18/8/14
 * 	- fix conversion to 16-bit RGB, thanks John
 * 18/6/15
 * 	- forward progress signals from load
 * 23/5/16
 * 	- remove max-alpha stuff, this is now automatic
 * 12/6/17
 * 	- transform cmyk->rgb if there's an embedded profile
 * 16/6/17
 * 	- add page_height
 * 1/1/18
 * 	- META_SEQ support moved here
 * 5/3/18
 * 	- block _start if one start fails, see #893
 * 1/4/18
 * 	- drop incompatible ICC profiles before save
 * 24/7/21
 * 	- add fail_on
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
#define DEBUG
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /*HAVE_CONFIG_H*/
#include <glib/gi18n-lib.h>

#include <stdio.h>
#include <stdlib.h>

#include <vips/vips.h>
#include <vips/internal.h>
#include <vips/debug.h>

#include "pforeign.h"

/**
 * VipsForeign:
 *
 * An abstract base class to load and save images in a variety of formats.
 *
 * ## Load and save
 *
 * You can load and save from and to files, memory areas, and the libvips IO
 * abstractions, [class@Source] and [class@Target].
 *
 * Use [func@Foreign.find_load] [func@Foreign.find_load_buffer] and
 * [func@Foreign.find_load_source] to find a loader for an object. Use
 * [func@Foreign.find_save], [func@Foreign.find_save_buffer] and
 * [func@Foreign.find_save_target] to find a saver for a format. You can then
 * run these operations using [func@call] and friends to perform the load or
 * save.
 *
 * [method@Image.write_to_file] and [ctor@Image.new_from_file] and friends use
 * these functions to automate file load and save.
 *
 * You can also invoke the operations directly, for example:
 *
 * ```c
 * vips_tiffsave(my_image, "frank.anything",
 *     "compression", VIPS_FOREIGN_TIFF_COMPRESSION_JPEG,
 *     NULL);
 * ```
 *
 * ## Image metadata
 *
 * All loaders attach all image metadata as libvips properties on load.
 *
 * You can change metadata with [method@Image.set_int] and friends.
 *
 * During save, you can use `keep` to specify which metadata to retain,
 * defaults to all, see [flags@ForeignKeep]. Setting `profile` will
 * automatically keep the ICC profile.
 *
 * ## Many page images
 *
 * By default, libvips will only load the first page of many page or animated
 * images. Use `page` and `n` to set the start page and the number of pages to
 * load. Set `n` to -1 to load all pages.
 *
 * Many page images are loaded as a tall, thin strip of pages.
 *
 * Use [method@Image.get_page_height] and [method@Image.get_n_pages] to find
 * the page height and number of pages of a loaded image.
 *
 * Use `page_height` to set the page height for image save.
 *
 * ## Alpha save
 *
 * Not all image formats support alpha. If you try to save an image with an
 * alpha channel to a format that does not support it, the alpha will be
 * automatically flattened out. Use `background` (default 0) to set the colour
 * that alpha should be flattened against.
 *
 * ## Adding new formats
 *
 * To add support for a new file format to vips, simply define a new subclass
 * of [class@ForeignLoad] or [class@ForeignSave].
 *
 * If you define a new operation which is a subclass of [class@Foreign],
 * support for it automatically appears in all libvips user-interfaces. It
 * will also be transparently supported by [ctor@Image.new_from_file] and
 * friends.
 */

/**
 * VipsForeignLoad:
 *
 * An abstract base class to load images in a variety of formats.
 *
 * ## Writing a new loader
 *
 * Add a new loader to libvips by subclassing [class@ForeignLoad]. Subclasses
 * need to implement at least [vfunc@ForeignLoad.header].
 *
 * [vfunc@ForeignLoad.header] must set at least the header fields of `out`.
 * [vfunc@ForeignLoad.load], if defined, must load the pixels to `real`.
 *
 * The suffix list is used to select a format to save a file in, and to pick a
 * loader if you don't define [func@Foreign.is_a].
 *
 * You should also define [property@Object:nickname] and
 * [property@Object:description] in [class@Object].
 *
 * As a complete example, here's code for a PNG loader, minus the actual
 * calls to libpng.
 *
 * ```c
 * typedef struct _VipsForeignLoadPng {
 *     VipsForeignLoad parent_object;
 *
 *     char *filename;
 * } VipsForeignLoadPng;
 *
 * typedef VipsForeignLoadClass VipsForeignLoadPngClass;
 *
 * G_DEFINE_TYPE(VipsForeignLoadPng, vips_foreign_load_png,
 *     VIPS_TYPE_FOREIGN_LOAD);
 *
 * static VipsForeignFlags
 * vips_foreign_load_png_get_flags_filename(const char *filename)
 * {
 *     VipsForeignFlags flags;
 *
 *     flags = 0;
 *     if (vips__png_isinterlaced(filename))
 *          flags = VIPS_FOREIGN_PARTIAL;
 *     else
 *          flags = VIPS_FOREIGN_SEQUENTIAL;
 *
 *     return flags;
 * }
 *
 * static VipsForeignFlags
 * vips_foreign_load_png_get_flags(VipsForeignLoad *load)
 * {
 *   VipsForeignLoadPng *png = (VipsForeignLoadPng *) load;
 *
 *   return vips_foreign_load_png_get_flags_filename(png->filename);
 * }
 *
 * static int
 * vips_foreign_load_png_header(VipsForeignLoad *load)
 * {
 *     VipsForeignLoadPng *png = (VipsForeignLoadPng *) load;
 *
 *     if (vips__png_header(png->filename, load->out))
 *         return -1;
 *
 *     return 0;
 * }
 *
 * static int
 * vips_foreign_load_png_load(VipsForeignLoad *load)
 * {
 *     VipsForeignLoadPng *png = (VipsForeignLoadPng *) load;
 *
 *     if (vips__png_read(png->filename, load->real))
 *         return -1;
 *
 *     return 0;
 * }
 *
 * static void
 * vips_foreign_load_png_class_init(VipsForeignLoadPngClass *class)
 * {
 *     GObjectClass *gobject_class = G_OBJECT_CLASS(class);
 *     VipsObjectClass *object_class = (VipsObjectClass *) class;
 *     VipsForeignClass *foreign_class = (VipsForeignClass *) class;
 *     VipsForeignLoadClass *load_class = (VipsForeignLoadClass *) class;
 *
 *     gobject_class->set_property = vips_object_set_property;
 *     gobject_class->get_property = vips_object_get_property;
 *
 *     object_class->nickname = "pngload";
 *     object_class->description = _("load png from file");
 *
 *     foreign_class->suffs = vips__png_suffs;
 *
 *     load_class->is_a = vips__png_ispng;
 *     load_class->get_flags_filename =
 *         vips_foreign_load_png_get_flags_filename;
 *     load_class->get_flags = vips_foreign_load_png_get_flags;
 *     load_class->header = vips_foreign_load_png_header;
 *     load_class->load = vips_foreign_load_png_load;
 *
 *     VIPS_ARG_STRING(class, "filename", 1,
 *         _("Filename"),
 *         _("Filename to load from"),
 *         VIPS_ARGUMENT_REQUIRED_INPUT,
 *         G_STRUCT_OFFSET(VipsForeignLoadPng, filename),
 *         NULL);
 * }
 *
 * static void
 * vips_foreign_load_png_init(VipsForeignLoadPng *png)
 * {
 * }
 * ```
 */

/**
 * VipsForeignSave:
 *
 * An abstract base class to save images in a variety of formats.
 *
 * ## Writing a new saver
 *
 * Call your saver in the class' [vfunc@Object.build] method after chaining up.
 * The prepared image should be ready for you to save in `ready`.
 *
 * As a complete example, here's the code for the CSV saver, minus the calls
 * to the actual save routines.
 *
 * ```c
 * typedef struct _VipsForeignSaveCsv {
 *     VipsForeignSave parent_object;
 *
 *     char *filename;
 *     const char *separator;
 * } VipsForeignSaveCsv;
 *
 * typedef VipsForeignSaveClass VipsForeignSaveCsvClass;
 *
 * G_DEFINE_TYPE(VipsForeignSaveCsv, vips_foreign_save_csv,
 *   VIPS_TYPE_FOREIGN_SAVE);
 *
 * static int
 * vips_foreign_save_csv_build(VipsObject *object)
 * {
 *     VipsForeignSave *save = (VipsForeignSave *) object;
 *     VipsForeignSaveCsv *csv = (VipsForeignSaveCsv *) object;
 *
 *     if (VIPS_OBJECT_CLASS(vips_foreign_save_csv_parent_class)
 *             ->build(object))
 *         return -1;
 *
 *     if (vips__csv_write(save->ready, csv->filename, csv->separator))
 *   	  return -1;
 *
 *     return 0;
 * }
 *
 * static void
 * vips_foreign_save_csv_class_init(VipsForeignSaveCsvClass *class)
 * {
 *     GObjectClass *gobject_class = G_OBJECT_CLASS(class);
 *     VipsObjectClass *object_class = (VipsObjectClass *) class;
 *     VipsForeignClass *foreign_class = (VipsForeignClass *) class;
 *     VipsForeignSaveClass *save_class = (VipsForeignSaveClass *) class;
 *
 *     gobject_class->set_property = vips_object_set_property;
 *     gobject_class->get_property = vips_object_get_property;
 *
 *     object_class->nickname = "csvsave";
 *     object_class->description = _("save image to csv file");
 *     object_class->build = vips_foreign_save_csv_build;
 *
 *     foreign_class->suffs = vips__foreign_csv_suffs;
 *
 *     save_class->saveable = VIPS_FOREIGN_SAVEABLE_MONO;
 *     // no need to define ->format_table, we don't want the input
 *     // cast for us
 *
 *     VIPS_ARG_STRING(class, "filename", 1,
 *         _("Filename"),
 *         _("Filename to save to"),
 *         VIPS_ARGUMENT_REQUIRED_INPUT,
 *         G_STRUCT_OFFSET(VipsForeignSaveCsv, filename),
 *         NULL);
 *
 *     VIPS_ARG_STRING(class, "separator", 13,
 *         _("Separator"),
 *         _("Separator characters"),
 *         VIPS_ARGUMENT_OPTIONAL_INPUT,
 *         G_STRUCT_OFFSET(VipsForeignSaveCsv, separator),
 *         "\t");
 * }
 *
 * static void
 * vips_foreign_save_csv_init(VipsForeignSaveCsv *csv)
 * {
 *     csv->separator = g_strdup("\t");
 * }
 * ```
 */

/* Use this to link images to the load operation that made them.
 */
static GQuark vips__foreign_load_operation = 0;

/**
 * VipsForeignFlags:
 * @VIPS_FOREIGN_NONE: no flags set
 * @VIPS_FOREIGN_PARTIAL: the image may be read lazilly
 * @VIPS_FOREIGN_BIGENDIAN: image pixels are most-significant byte first
 * @VIPS_FOREIGN_SEQUENTIAL: top-to-bottom lazy reading
 *
 * Some hints about the image loader.
 *
 * [flags@Vips.ForeignFlags.PARTIAL] means that the image can be read directly
 * from the file without needing to be unpacked to a temporary image first.
 *
 * [flags@Vips.ForeignFlags.SEQUENTIAL] means that the loader supports lazy
 * reading, but only top-to-bottom (sequential) access. Formats like PNG can
 * read sets of scanlines, for example, but only in order.
 *
 * If neither PARTIAL or SEQUENTIAL is set, the loader only supports whole
 * image read. Setting both PARTIAL and SEQUENTIAL is an error.
 *
 * [flags@Vips.ForeignFlags.BIGENDIAN] means that image pixels are
 * most-significant byte first. Depending on the native byte order of the
 * host machine, you may need to swap bytes. See [method@Image.copy].
 */

G_DEFINE_ABSTRACT_TYPE(VipsForeign, vips_foreign, VIPS_TYPE_OPERATION);

static void
vips_foreign_summary_class(VipsObjectClass *object_class, VipsBuf *buf)
{
	VipsForeignClass *class = VIPS_FOREIGN_CLASS(object_class);
	VipsOperationClass *operation_class =
		VIPS_OPERATION_CLASS(object_class);

	VIPS_OBJECT_CLASS(vips_foreign_parent_class)
		->summary_class(object_class, buf);

	if (class->suffs) {
		const char **p;

		vips_buf_appends(buf, " (");
		for (p = class->suffs; *p; p++) {
			vips_buf_appendf(buf, "%s", *p);
			if (p[1])
				vips_buf_appends(buf, ", ");
		}
		vips_buf_appends(buf, ")");
	}

	vips_buf_appendf(buf, ", priority=%d", class->priority);

	if (operation_class->flags & VIPS_OPERATION_UNTRUSTED)
		vips_buf_appendf(buf, ", untrusted");
	if (operation_class->flags & VIPS_OPERATION_BLOCKED)
		vips_buf_appendf(buf, ", blocked");
}

static void
vips_foreign_class_init(VipsForeignClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "foreign";
	object_class->description = _("load and save image files");
	object_class->summary_class = vips_foreign_summary_class;
}

static void
vips_foreign_init(VipsForeign *object)
{
}

/* To iterate over supported files we build a temp list of subclasses of
 * VipsForeign, sort by priority, iterate, and free.
 */

static void *
file_add_class(VipsForeignClass *class, GSList **files)
{
	VipsOperationClass *operation_class = VIPS_OPERATION_CLASS(class);

	// don't consider blocked classes ... we don't want eg. sniffers to run
	if (operation_class->flags & VIPS_OPERATION_BLOCKED)
		return NULL;

	// exclude "rawload" as it has a different API.
	if (vips_isprefix("rawload", VIPS_OBJECT_CLASS(class)->nickname))
		return NULL;

	/* Append so we don't reverse the list of files. Sort will
	 * not reorder items of equal priority.
	 */
	*files = g_slist_append(*files, class);

	return NULL;
}

static gint
file_compare(VipsForeignClass *a, VipsForeignClass *b, void *user_data)
{
	return b->priority - a->priority;
}

/**
 * vips_foreign_map:
 * @base: base class to search below (eg. "VipsForeignLoad")
 * @fn: (scope call): function to apply to each [class@Foreign]
 * @a: user data
 * @b: user data
 *
 * Apply a function to every [class@Foreign] that VIPS knows about. Foreigns
 * are presented to the function in priority order.
 *
 * Like all VIPS map functions, if @fn returns `NULL`, iteration continues. If
 * it returns non-`NULL`, iteration terminates and that value is returned. The
 * map function returns `NULL` if all calls return `NULL`.
 *
 * ::: seealso
 *     [func@slist_map2].
 *
 * Returns: (transfer none): the result of iteration
 */
void *
vips_foreign_map(const char *base, VipsSListMap2Fn fn, void *a, void *b)
{
	GSList *files;
	void *result;

	files = NULL;
	(void) vips_class_map_all(g_type_from_name(base),
		(VipsClassMapFn) file_add_class, (void *) &files);

	files = g_slist_sort(files, (GCompareFunc) file_compare);
#ifdef DEBUG
	{
		GSList *p;

		printf("vips_foreign_map: search order\n");
		for (p = files; p; p = p->next) {
			VipsForeignClass *class = (VipsForeignClass *) p->data;

			printf("\t%s\n", VIPS_OBJECT_CLASS(class)->nickname);
		}
	}
#endif /*DEBUG*/
	result = vips_slist_map2(files, fn, a, b);

	g_slist_free(files);

	return result;
}

/* Abstract base class for image load.
 */

G_DEFINE_ABSTRACT_TYPE(VipsForeignLoad, vips_foreign_load, VIPS_TYPE_FOREIGN);

static void
vips_foreign_load_dispose(GObject *gobject)
{
	VipsForeignLoad *load = VIPS_FOREIGN_LOAD(gobject);

	VIPS_UNREF(load->real);

	G_OBJECT_CLASS(vips_foreign_load_parent_class)->dispose(gobject);
}

static void
vips_foreign_load_summary_class(VipsObjectClass *object_class, VipsBuf *buf)
{
	VipsForeignLoadClass *class = VIPS_FOREIGN_LOAD_CLASS(object_class);

	VIPS_OBJECT_CLASS(vips_foreign_load_parent_class)
		->summary_class(object_class, buf);

	if (!G_TYPE_IS_ABSTRACT(G_TYPE_FROM_CLASS(class))) {
		if (class->is_a)
			vips_buf_appends(buf, ", is_a");
		if (class->is_a_buffer)
			vips_buf_appends(buf, ", is_a_buffer");
		if (class->is_a_source)
			vips_buf_appends(buf, ", is_a_source");
		if (class->get_flags)
			vips_buf_appends(buf, ", get_flags");
		if (class->get_flags_filename)
			vips_buf_appends(buf, ", get_flags_filename");
		if (class->header)
			vips_buf_appends(buf, ", header");
		if (class->load)
			vips_buf_appends(buf, ", load");

		/* You can omit ->load(), you must not omit ->header().
		 */
		g_assert(class->header);
	}
}

/* Can this VipsForeign open this file?
 */
static void *
vips_foreign_find_load_sub(VipsForeignLoadClass *load_class,
	const char *filename, void *b)
{
	VipsObjectClass *object_class = VIPS_OBJECT_CLASS(load_class);
	VipsForeignClass *class = VIPS_FOREIGN_CLASS(load_class);

	/* Ignore the buffer and source loaders.
	 */
	if (g_str_has_suffix(object_class->nickname, "_buffer") ||
		g_str_has_suffix(object_class->nickname, "_source"))
		return NULL;

#ifdef DEBUG
	printf("vips_foreign_find_load_sub: %s\n",
		VIPS_OBJECT_CLASS(class)->nickname);
#endif /*DEBUG*/

	/* Try to sniff the filetype from the first few bytes, if we can,
	 * otherwise fall back to checking the filename suffix.
	 */
	if (load_class->is_a) {
		if (load_class->is_a(filename))
			return load_class;

#ifdef DEBUG
		printf("vips_foreign_find_load_sub: is_a failed\n");
#endif /*DEBUG*/
	}
	else if (class->suffs) {
		if (vips_filename_suffix_match(filename, class->suffs))
			return load_class;
	}
	else
		g_warning("loader %s has no is_a method and no suffix list",
			object_class->nickname);

	return NULL;
}

/**
 * vips_foreign_find_load:
 * @filename: file to find a loader for
 *
 * Searches for an operation you could use to load @filename. Any trailing
 * options on @filename are stripped and ignored.
 *
 * ::: seealso
 *     [func@Foreign.find_load_buffer], [ctor@Image.new_from_file].
 *
 * Returns: the name of an operation on success, `NULL` on error
 */
const char *
vips_foreign_find_load(const char *name)
{
	char filename[VIPS_PATH_MAX];
	char option_string[VIPS_PATH_MAX];
	VipsForeignLoadClass *load_class;

	vips__filename_split8(name, filename, option_string);

	/* Very common, so make a better error message for this case.
	 */
	if (!vips_existsf("%s", filename)) {
		vips_error("VipsForeignLoad",
			_("file \"%s\" does not exist"), name);
		return NULL;
	}
	if (vips_isdirf("%s", filename)) {
		vips_error("VipsForeignLoad",
			_("\"%s\" is a directory"), name);
		return NULL;
	}

	if (!(load_class = (VipsForeignLoadClass *) vips_foreign_map(
			  "VipsForeignLoad",
			  (VipsSListMap2Fn) vips_foreign_find_load_sub,
			  (void *) filename, NULL))) {
		vips_error("VipsForeignLoad",
			_("\"%s\" is not a known file format"), name);
		return NULL;
	}

#ifdef DEBUG
	printf("vips_foreign_find_load: selected %s\n",
		VIPS_OBJECT_CLASS(load_class)->nickname);
#endif /*DEBUG*/

	return G_OBJECT_CLASS_NAME(load_class);
}

/* Kept for compat with earlier version of the vip8 API. Use
 * vips_image_new_from_file() now.
 */

int
vips_foreign_load(const char *name, VipsImage **out, ...)
{
	char filename[VIPS_PATH_MAX];
	char option_string[VIPS_PATH_MAX];
	const char *operation_name;
	va_list ap;
	int result;

	vips__filename_split8(name, filename, option_string);
	if (!(operation_name = vips_foreign_find_load(filename)))
		return -1;

	va_start(ap, out);
	result = vips_call_split_option_string(operation_name, option_string,
		ap, filename, out);
	va_end(ap);

	return result;
}

/* Can this VipsForeign open this buffer?
 */
static void *
vips_foreign_find_load_buffer_sub(VipsForeignLoadClass *load_class,
	const void **buf, size_t *len)
{
	VipsObjectClass *object_class = VIPS_OBJECT_CLASS(load_class);

	/* Skip non-buffer loaders.
	 */
	if (!g_str_has_suffix(object_class->nickname, "_buffer"))
		return NULL;

	if (load_class->is_a_buffer) {
		if (load_class->is_a_buffer(*buf, *len))
			return load_class;
	}
	else
		g_warning("loader %s has no is_a_buffer method",
			object_class->nickname);

	return NULL;
}

/**
 * vips_foreign_find_load_buffer:
 * @data: (array length=size) (element-type guint8) (transfer none): start of
 * memory buffer
 * @size: (type gsize): number of bytes in @data
 *
 * Searches for an operation you could use to load a memory buffer. To see the
 * range of buffer loaders supported by your vips, try something like:
 *
 * 	vips -l | grep load_buffer
 *
 * ::: seealso
 *     [ctor@Image.new_from_buffer].
 *
 * Returns: (transfer none): the name of an operation on success, `NULL` on
 * error.
 */
const char *
vips_foreign_find_load_buffer(const void *data, size_t size)
{
	VipsForeignLoadClass *load_class;

	if (!(load_class = (VipsForeignLoadClass *) vips_foreign_map(
			  "VipsForeignLoad",
			  (VipsSListMap2Fn) vips_foreign_find_load_buffer_sub,
			  &data, &size))) {
		vips_error("VipsForeignLoad",
			"%s", _("buffer is not in a known format"));
		return NULL;
	}

	return G_OBJECT_CLASS_NAME(load_class);
}

/* Can this VipsForeign open this source?
 */
static void *
vips_foreign_find_load_source_sub(void *item, void *a, void *b)
{
	VipsObjectClass *object_class = VIPS_OBJECT_CLASS(item);
	VipsForeignLoadClass *load_class = VIPS_FOREIGN_LOAD_CLASS(item);
	VipsSource *source = VIPS_SOURCE(a);

	/* Skip non-source loaders.
	 */
	if (!g_str_has_suffix(object_class->nickname, "_source"))
		return NULL;

	if (load_class->is_a_source) {
		/* We may have done a _read() rather than a _sniff() in one of
		 * the is_a testers. Always rewind.
		 */
		(void) vips_source_rewind(source);

		if (load_class->is_a_source(source))
			return load_class;
	}
	else
		g_warning("loader %s has no is_a_source method",
			object_class->nickname);

	return NULL;
}

/**
 * vips_foreign_find_load_source:
 * @source: source to load from
 *
 * Searches for an operation you could use to load a source. To see the
 * range of source loaders supported by your vips, try something like:
 *
 * 	vips -l | grep load_source
 *
 * ::: seealso
 *     [ctor@Image.new_from_source].
 *
 * Returns: (transfer none): the name of an operation on success, `NULL` on
 * error.
 */
const char *
vips_foreign_find_load_source(VipsSource *source)
{
	VipsForeignLoadClass *load_class;

	if (!(load_class = (VipsForeignLoadClass *) vips_foreign_map(
			  "VipsForeignLoad",
			  vips_foreign_find_load_source_sub,
			  source, NULL))) {
		vips_error("VipsForeignLoad",
			"%s", _("source is not in a known format"));
		return NULL;
	}

	return G_OBJECT_CLASS_NAME(load_class);
}

/**
 * vips_foreign_is_a:
 * @loader: name of loader to use for test
 * @filename: file to test
 *
 * Return `TRUE` if @filename can be loaded by @loader. @loader is something
 * like "tiffload" or "VipsForeignLoadTiff".
 *
 * Returns: `TRUE` if @filename can be loaded by @loader.
 */
gboolean
vips_foreign_is_a(const char *loader, const char *filename)
{
	const VipsObjectClass *class;
	VipsForeignLoadClass *load_class;

	if (!(class = vips_class_find("VipsForeignLoad", loader)))
		return FALSE;
	load_class = VIPS_FOREIGN_LOAD_CLASS(class);
	if (load_class->is_a &&
		load_class->is_a(filename))
		return TRUE;

	return FALSE;
}

/**
 * vips_foreign_is_a_buffer:
 * @loader: name of loader to use for test
 * @data: (array length=size) (element-type guint8): pointer to the buffer to test
 * @size: (type gsize): size of the buffer to test
 *
 * Return `TRUE` if @data can be loaded by @loader. @loader is something
 * like "tiffload_buffer" or "VipsForeignLoadTiffBuffer".
 *
 * Returns: `TRUE` if @data can be loaded by @loader.
 */
gboolean
vips_foreign_is_a_buffer(const char *loader, const void *data, size_t size)
{
	const VipsObjectClass *class;
	VipsForeignLoadClass *load_class;

	if (!(class = vips_class_find("VipsForeignLoad", loader)))
		return FALSE;
	load_class = VIPS_FOREIGN_LOAD_CLASS(class);
	if (load_class->is_a_buffer &&
		load_class->is_a_buffer(data, size))
		return TRUE;

	return FALSE;
}

/**
 * vips_foreign_is_a_source:
 * @loader: name of loader to use for test
 * @source: source to test
 *
 * Return `TRUE` if @source can be loaded by @loader. @loader is something
 * like "tiffload_source" or "VipsForeignLoadTiffSource".
 *
 * Returns: `TRUE` if @data can be loaded by @source.
 */
gboolean
vips_foreign_is_a_source(const char *loader, VipsSource *source)
{
	const VipsObjectClass *class;
	VipsForeignLoadClass *load_class;

	if (!(class = vips_class_find("VipsForeignLoad", loader)))
		return FALSE;
	load_class = VIPS_FOREIGN_LOAD_CLASS(class);
	if (load_class->is_a_source &&
		load_class->is_a_source(source))
		return TRUE;

	return FALSE;
}

/**
 * vips_foreign_flags:
 * @loader: name of loader to use for test
 * @filename: file to test
 *
 * Return the flags for @filename using @loader.
 * @loader is something like "tiffload" or "VipsForeignLoadTiff".
 *
 * Returns: the flags for @filename.
 */
VipsForeignFlags
vips_foreign_flags(const char *loader, const char *filename)
{
	const VipsObjectClass *class;

	if ((class = vips_class_find("VipsForeignLoad", loader))) {
		VipsForeignLoadClass *load_class =
			VIPS_FOREIGN_LOAD_CLASS(class);

		if (load_class->get_flags_filename)
			return load_class->get_flags_filename(filename);
	}

	return 0;
}

static VipsObject *
vips_foreign_load_new_from_string(const char *string)
{
	const char *file_op;
	GType type;
	VipsForeignLoad *load;

	if (!(file_op = vips_foreign_find_load(string)))
		return NULL;
	type = g_type_from_name(file_op);
	g_assert(type);

	load = VIPS_FOREIGN_LOAD(g_object_new(type, NULL));
	g_object_set(load,
		"filename", string,
		NULL);

	return VIPS_OBJECT(load);
}

static VipsImage *
vips_foreign_load_temp(VipsForeignLoad *load)
{
	const guint64 disc_threshold = vips_get_disc_threshold();
	const guint64 image_size = VIPS_IMAGE_SIZEOF_IMAGE(load->out);

	/* ->memory used to be called ->disc and default TRUE. If it's been
	 * forced FALSE, set memory TRUE.
	 */
	if (!load->disc)
		load->memory = TRUE;

	if (load->memory) {
#ifdef DEBUG
		printf("vips_foreign_load_temp: forced memory temp\n");
#endif /*DEBUG*/

		return vips_image_new_memory();
	}

	/* If this is a partial operation, we can open directly.
	 */
	if (load->flags & VIPS_FOREIGN_PARTIAL) {
#ifdef DEBUG
		printf("vips_foreign_load_temp: partial temp\n");
#endif /*DEBUG*/

		return vips_image_new();
	}

	/* If it can do sequential access and it's been requested, we can open
	 * directly.
	 */
	if ((load->flags & VIPS_FOREIGN_SEQUENTIAL) &&
		load->access != VIPS_ACCESS_RANDOM) {
#ifdef DEBUG
		printf("vips_foreign_load_temp: partial sequential temp\n");
#endif /*DEBUG*/

		return vips_image_new();
	}

	/* We open via disc if the uncompressed image will be larger than
	 * vips_get_disc_threshold().
	 */
	if (image_size > disc_threshold) {
#ifdef DEBUG
		printf("vips_foreign_load_temp: disc temp\n");
#endif /*DEBUG*/

		return vips_image_new_temp_file("%s.v");
	}

#ifdef DEBUG
	printf("vips_foreign_load_temp: fallback memory temp\n");
#endif /*DEBUG*/

	/* Otherwise, fall back to a memory buffer.
	 */
	return vips_image_new_memory();
}

/* Check two images for compatibility: their geometries need to match.
 */
static gboolean
vips_foreign_load_iscompat(VipsImage *a, VipsImage *b)
{
	if (a->Xsize != b->Xsize ||
		a->Ysize != b->Ysize ||
		a->Bands != b->Bands ||
		a->Coding != b->Coding ||
		a->BandFmt != b->BandFmt) {
		vips_error("VipsForeignLoad", "%s",
			_("images do not match between header and load"));
		return FALSE;
	}

	return TRUE;
}

/* Our start function ... do the lazy open, if necessary, and return a region
 * on the new image.
 */
static void *
vips_foreign_load_start(VipsImage *out, void *a, void *b)
{
	VipsForeignLoad *load = VIPS_FOREIGN_LOAD(b);
	VipsForeignLoadClass *class = VIPS_FOREIGN_LOAD_GET_CLASS(load);

	/* If this start has failed before in another thread, we can fail now.
	 */
	if (load->error)
		return NULL;

	if (!load->real) {
		if (!(load->real = vips_foreign_load_temp(load)))
			return NULL;

#ifdef DEBUG
		printf("vips_foreign_load_start: triggering ->load\n");
#endif /*DEBUG*/

		/* Read the image in. This may involve a long computation and
		 * will finish with load->real holding the decompressed image.
		 *
		 * We want our caller to be able to see this computation on
		 * @out, so eval signals on ->real need to appear on ->out.
		 */
		load->real->progress_signal = load->out;

		/* Note the load object on the image. Loaders can use
		 * this to signal invalidate if they hit a load error. See
		 * vips_foreign_load_invalidate() below.
		 */
		g_object_set_qdata(G_OBJECT(load->real),
			vips__foreign_load_operation, load);

		/* Load the image and check the result.
		 *
		 * ->header() read the header into @out, load will read the
		 * image into @real. They must match exactly in size, bands,
		 * format and coding for the copy to work.
		 *
		 * Some versions of ImageMagick give different results between
		 * Ping and Load for some formats, for example.
		 *
		 * If the load fails, we need to stop.
		 */
		if (class->load(load) ||
			vips_image_pio_input(load->real) ||
			!vips_foreign_load_iscompat(load->real, out)) {
			vips_operation_invalidate(VIPS_OPERATION(load));
			load->error = TRUE;

			return NULL;
		}

		/* We have to tell vips that out depends on real. We've set
		 * the demand hint below, but not given an input there.
		 */
		if (vips_image_pipelinev(load->out, load->out->dhint,
				load->real, NULL))
			return NULL;
	}

	return vips_region_new(load->real);
}

/* Just pointer-copy.
 */
static int
vips_foreign_load_generate(VipsRegion *out_region,
	void *seq, void *a, void *b, gboolean *stop)
{
	VipsRegion *ir = (VipsRegion *) seq;

	VipsRect *r = &out_region->valid;

	/* Ask for input we need.
	 */
	if (vips_region_prepare(ir, r))
		return -1;

	/* Attach output region to that.
	 */
	if (vips_region_region(out_region, ir, r, r->left, r->top))
		return -1;

	return 0;
}

static int
vips_foreign_load_build(VipsObject *object)
{
	VipsObjectClass *class = VIPS_OBJECT_GET_CLASS(object);
	VipsForeignLoad *load = VIPS_FOREIGN_LOAD(object);
	VipsForeignLoadClass *fclass = VIPS_FOREIGN_LOAD_GET_CLASS(object);

	VipsForeignFlags flags;

#ifdef DEBUG
	printf("vips_foreign_load_build:\n");
#endif /*DEBUG*/

	flags = 0;
	if (fclass->get_flags)
		flags |= fclass->get_flags(load);

	if ((flags & VIPS_FOREIGN_PARTIAL) &&
		(flags & VIPS_FOREIGN_SEQUENTIAL)) {
		g_warning("VIPS_FOREIGN_PARTIAL and VIPS_FOREIGN_SEQUENTIAL "
				  "both set -- using SEQUENTIAL");
		flags ^= VIPS_FOREIGN_PARTIAL;
	}

	g_object_set(load, "flags", flags, NULL);

	/* We must block caching of seq loaders running in seq mode. A seq
	 * loader in random mode is fine, since we'll read to ram or a temp
	 * file.
	 */
	if ((load->flags & VIPS_FOREIGN_SEQUENTIAL) &&
		load->access != VIPS_ACCESS_RANDOM)
		load->nocache = TRUE;

	/* The deprecated "fail" field sets fail_on warning.
	 */
	if (vips_object_argument_isset(object, "fail") &&
		!vips_object_argument_isset(object, "fail_on"))
		load->fail_on = load->fail
			? VIPS_FAIL_ON_WARNING
			: VIPS_FAIL_ON_NONE;

	if (VIPS_OBJECT_CLASS(vips_foreign_load_parent_class)->build(object))
		return -1;

	if (load->sequential)
		g_warning("ignoring deprecated \"sequential\" mode -- "
				  "please use \"access\" instead");

	g_object_set(object, "out", vips_image_new(), NULL);

	vips_image_set_string(load->out,
		VIPS_META_LOADER, class->nickname);

#ifdef DEBUG
	printf("vips_foreign_load_build: triggering ->header\n");
#endif /*DEBUG*/

	/* Read the header into @out.
	 */
	if (fclass->header &&
		fclass->header(load))
		return -1;

	/* If there's no ->load() method then the header read has done
	 * everything. Otherwise, it's just set fields and we must also
	 * load pixels.
	 *
	 * Delay the load until the first pixel is requested by doing the work
	 * in the start function of the copy.
	 */
	if (fclass->load) {
#ifdef DEBUG
		printf("vips_foreign_load_build: delaying read ...\n");
#endif /*DEBUG*/

		/* ->header() should set the dhint. It'll default to the safe
		 * SMALLTILE if header() did not set it.
		 */
		if (vips_image_pipelinev(load->out, load->out->dhint, NULL))
			return -1;

		/* Then 'start' creates the real image and 'gen' fetches
		 * pixels for @out from @real on demand.
		 */
		if (vips_image_generate(load->out,
				vips_foreign_load_start,
				vips_foreign_load_generate,
				vips_stop_one,
				NULL, load))
			return -1;
	}

	/* Tell downstream if seq mode was requested.
	 */
	if (load->access != VIPS_ACCESS_RANDOM)
		vips_image_set_int(load->out, VIPS_META_SEQUENTIAL, 1);

	return 0;
}

static VipsOperationFlags
vips_foreign_load_operation_get_flags(VipsOperation *operation)
{
	VipsForeignLoad *load = VIPS_FOREIGN_LOAD(operation);

	VipsOperationFlags flags;

	flags = VIPS_OPERATION_CLASS(vips_foreign_load_parent_class)
				->get_flags(operation);
	if (load->nocache)
		flags |= VIPS_OPERATION_NOCACHE;
	if (load->revalidate)
		flags |= VIPS_OPERATION_REVALIDATE;

	return flags;
}

static void
vips_foreign_load_class_init(VipsForeignLoadClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsOperationClass *operation_class = (VipsOperationClass *) class;

	gobject_class->dispose = vips_foreign_load_dispose;
	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->build = vips_foreign_load_build;
	object_class->summary_class = vips_foreign_load_summary_class;
	object_class->new_from_string = vips_foreign_load_new_from_string;
	object_class->nickname = "load";
	object_class->description = _("loaders");

	operation_class->get_flags = vips_foreign_load_operation_get_flags;

	VIPS_ARG_IMAGE(class, "out", 2,
		_("Output"),
		_("Output image"),
		VIPS_ARGUMENT_REQUIRED_OUTPUT,
		G_STRUCT_OFFSET(VipsForeignLoad, out));

	VIPS_ARG_FLAGS(class, "flags", 106,
		_("Flags"),
		_("Flags for this file"),
		VIPS_ARGUMENT_OPTIONAL_OUTPUT,
		G_STRUCT_OFFSET(VipsForeignLoad, flags),
		VIPS_TYPE_FOREIGN_FLAGS, VIPS_FOREIGN_NONE);

	VIPS_ARG_BOOL(class, "memory", 107,
		_("Memory"),
		_("Force open via memory"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignLoad, memory),
		FALSE);

	VIPS_ARG_ENUM(class, "access", 108,
		_("Access"),
		_("Required access pattern for this file"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignLoad, access),
		VIPS_TYPE_ACCESS, VIPS_ACCESS_RANDOM);

	VIPS_ARG_ENUM(class, "fail_on", 109,
		_("Fail on"),
		_("Error level to fail on"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignLoad, fail_on),
		VIPS_TYPE_FAIL_ON, VIPS_FAIL_ON_NONE);

	VIPS_ARG_BOOL(class, "revalidate", 110,
		_("Revalidate"),
		_("Don't use a cached result for this operation"),
		VIPS_ARGUMENT_OPTIONAL_INPUT | VIPS_ARGUMENT_NON_HASHABLE,
		G_STRUCT_OFFSET(VipsForeignLoad, revalidate),
		FALSE);

	VIPS_ARG_BOOL(class, "sequential", 111,
		_("Sequential"),
		_("Sequential read only"),
		VIPS_ARGUMENT_OPTIONAL_INPUT | VIPS_ARGUMENT_DEPRECATED,
		G_STRUCT_OFFSET(VipsForeignLoad, sequential),
		FALSE);

	VIPS_ARG_BOOL(class, "fail", 112,
		_("Fail"),
		_("Fail on first warning"),
		VIPS_ARGUMENT_OPTIONAL_INPUT | VIPS_ARGUMENT_DEPRECATED,
		G_STRUCT_OFFSET(VipsForeignLoad, fail),
		FALSE);

	VIPS_ARG_BOOL(class, "disc", 113,
		_("Disc"),
		_("Open to disc"),
		VIPS_ARGUMENT_OPTIONAL_INPUT | VIPS_ARGUMENT_DEPRECATED,
		G_STRUCT_OFFSET(VipsForeignLoad, disc),
		TRUE);
}

static void
vips_foreign_load_init(VipsForeignLoad *load)
{
	load->disc = TRUE;
	load->access = VIPS_ACCESS_RANDOM;
	load->fail_on = VIPS_FAIL_ON_NONE;
}

/*
 * Loaders can call this
 */

/**
 * vips_foreign_load_invalidate: (method)
 * @image: image to invalidate
 *
 * Loaders can call this on the image they are making if they see a read error
 * from the load library. It signals "invalidate" on the load operation and
 * will cause it to be dropped from cache.
 *
 * If we know a file will cause a read error, we don't want to cache the
 * failing operation, we want to make sure the image will really be opened
 * again if our caller tries again. For example, a broken file might be
 * replaced by a working one.
 */
void
vips_foreign_load_invalidate(VipsImage *image)
{
	VipsOperation *operation;

#ifdef DEBUG
	printf("vips_foreign_load_invalidate: %p\n", image);
#endif /*DEBUG*/

	if ((operation = g_object_get_qdata(G_OBJECT(image),
			 vips__foreign_load_operation))) {
		vips_operation_invalidate(operation);
	}
}

/* Abstract base class for image savers.
 */

G_DEFINE_ABSTRACT_TYPE(VipsForeignSave, vips_foreign_save, VIPS_TYPE_FOREIGN);

static void
vips_foreign_save_dispose(GObject *gobject)
{
	VipsForeignSave *save = VIPS_FOREIGN_SAVE(gobject);

	VIPS_UNREF(save->ready);

	G_OBJECT_CLASS(vips_foreign_save_parent_class)->dispose(gobject);
}

static void
vips_foreign_save_summary_class(VipsObjectClass *object_class, VipsBuf *buf)
{
	VipsForeignSaveClass *class = VIPS_FOREIGN_SAVE_CLASS(object_class);

	VIPS_OBJECT_CLASS(vips_foreign_save_parent_class)
		->summary_class(object_class, buf);

	GValue value = { 0 };
	g_value_init(&value, VIPS_TYPE_FOREIGN_SAVEABLE);
	g_value_set_flags(&value, class->saveable);
	vips_buf_appends(buf, ", ");
	vips_buf_appendgv(buf, &value);
	g_value_unset(&value);
}

static VipsObject *
vips_foreign_save_new_from_string(const char *string)
{
	const char *file_op;
	GType type;
	VipsForeignSave *save;

	if (!(file_op = vips_foreign_find_save(string)))
		return NULL;
	type = g_type_from_name(file_op);
	g_assert(type);

	save = VIPS_FOREIGN_SAVE(g_object_new(type, NULL));
	g_object_set(save,
		"filename", string,
		NULL);

	return VIPS_OBJECT(save);
}

/* Apply a set of saveable flags.
 *
 *  - unpack rad and labq
 *	- if the saver supports mono and we have a mono-looking image, we are done
 *	- if the saver supports CMYK and we have a CMYK-looking image, we are done
 *	- if this is a CMYK-looking image, import to XYZ
 *	- if the saver supports rgb, go to rgb
 *	- if the saver supports cmyk, go to cmyk
 *	- if the saver supports mono, go to mono
 *
 * we output 16 bit images if the source is 16 bits ... a later stage
 * uses the format[] table to cut this down to the size the saver wants
 */
static int
vips_foreign_apply_saveable(VipsImage *in, VipsImage **ready,
	VipsForeignSaveable saveable)
{
	// is this a 16-bit source image
	gboolean sixteenbit = in->BandFmt == VIPS_FORMAT_USHORT;

	VipsImage *out;
	VipsInterpretation interpretation;

	/* in holds a reference to the output of our chain as we build it.
	 */
	g_object_ref(in);

	/* ANY? we are done.
	 */
	if (saveable == VIPS_FOREIGN_SAVEABLE_ANY) {
		*ready = in;
		return 0;
	}

	/* If this is VIPS_CODING_LABQ, we can go straight to RGB.
	 */
	if (in->Coding == VIPS_CODING_LABQ) {
		if (vips_LabQ2sRGB(in, &out, NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;
	}

	/* If this is VIPS_CODING_RAD, we unpack to float. This could be
	 * scRGB or XYZ.
	 */
	if (in->Coding == VIPS_CODING_RAD) {
		if (vips_rad2float(in, &out, NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;
	}

	/* If this is a mono-ish looking image and our saver supports mono, we
	 * are done. We are not too strict about what a mono image is! We need to
	 * work for things like "extract_band 1" on an RGB image.
	 */
	if ((saveable & VIPS_FOREIGN_SAVEABLE_MONO) &&
		in->Bands < 3) {
		*ready = in;
		return 0;
	}

	/* CMYK image? Use the sanity-checked interpretation value.
	 */
	if (vips_image_guess_interpretation(in) == VIPS_INTERPRETATION_CMYK &&
		in->Bands >= 4) {
		/* If our saver supports CMYK we are done, otherwise import to XYZ.
		 */
		if (saveable & VIPS_FOREIGN_SAVEABLE_CMYK) {
			*ready = in;
			return 0;
		}

		if (vips_icc_import(in, &out,
				"pcs", VIPS_PCS_XYZ,
				"embedded", TRUE,
				"input_profile", "cmyk",
				NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;
	}

	/* If the saver supports RGB, go to RGB, or RGB16 if this is a ushort
	 * source.
	 */
	if (saveable & VIPS_FOREIGN_SAVEABLE_RGB) {
		interpretation = sixteenbit ?
			VIPS_INTERPRETATION_RGB16 : VIPS_INTERPRETATION_sRGB;

		if (vips_colourspace(in, &out, interpretation, NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;

		*ready = in;
		return 0;
	}

	/* If the saver supports CMYK, go to CMYK, 16 bits if this is a ushort
	 * source.
	 */
	if (saveable & VIPS_FOREIGN_SAVEABLE_CMYK) {
		if (vips_icc_export(in, &out,
			"output-profile", "cmyk",
			"depth", sixteenbit ? 16 : 8,
			NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;

		*ready = in;
		return 0;
	}

	/* If the saver supports mono, go to B_W, or GREY16 if this is a ushort
	 * source.
	 */
	if (saveable & VIPS_FOREIGN_SAVEABLE_MONO) {
		interpretation = sixteenbit ?
			VIPS_INTERPRETATION_GREY16 : VIPS_INTERPRETATION_B_W;

		if (vips_colourspace(in, &out, interpretation, NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;

		*ready = in;
		return 0;
	}

	vips_error("VipsForeignSave", _("saver does not support any output type"));
	return -1;
}

/* Do all the colourspace conversions to get an image ready for saving. Don't
 * finalize alpha or numeric format.
 */
int
vips__foreign_convert_saveable(VipsImage *in, VipsImage **ready,
	VipsForeignSaveable saveable, VipsBandFormat *format, VipsForeignCoding coding,
	VipsArrayDouble *background)
{
	VipsImage *out;

	/* in holds a reference to the output of our chain as we build it.
	 */
	g_object_ref(in);

	g_assert(format);

	/* For coded images, can this class save the coding we are in now?
	 * Nothing to do.
	 */
	if ((in->Coding == VIPS_CODING_LABQ &&
			(coding & VIPS_FOREIGN_CODING_LABQ)) ||
		(in->Coding == VIPS_CODING_RAD &&
			(coding & VIPS_FOREIGN_CODING_RAD))) {
		*ready = in;
		return 0;
	}

	/* For uncoded images, if this saver supports ANY and this
	 * format, we have nothing to do.
	 */
	if (in->Coding == VIPS_CODING_NONE &&
		(saveable == VIPS_FOREIGN_SAVEABLE_ANY) &&
		format[in->BandFmt] == in->BandFmt) {
		*ready = in;
		return 0;
	}

	/* Otherwise ... we need to decode and then (possibly) recode at the
	 * end.
	 */

	/* Apply saveable conversions to get mono/rgb/cmyk.
	 */
	if (vips_foreign_apply_saveable(in, &out, saveable)) {
		g_object_unref(in);
		return -1;
	}
	g_object_unref(in);
	in = out;

	/* Flatten alpha, if the saver does not support it.
	 */
	if (in->Coding == VIPS_CODING_NONE &&
		vips_image_hasalpha(in) &&
		!(saveable & VIPS_FOREIGN_SAVEABLE_ALPHA)) {
		if (vips_flatten(in, &out,
				"background", background,
				NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;
	}

	/* There might be more than one alpha ... drop any remaining excess
	 * bands.
	 */
	if (in->Coding == VIPS_CODING_NONE) {
		int max_bands;

		// use a sanity-checked interpretation
		switch (vips_image_guess_interpretation(in)) {
		case VIPS_INTERPRETATION_B_W:
		case VIPS_INTERPRETATION_GREY16:
			max_bands = 1;
			break;

		case VIPS_INTERPRETATION_RGB:
		case VIPS_INTERPRETATION_CMC:
		case VIPS_INTERPRETATION_LCH:
		case VIPS_INTERPRETATION_LABS:
		case VIPS_INTERPRETATION_sRGB:
		case VIPS_INTERPRETATION_YXY:
		case VIPS_INTERPRETATION_XYZ:
		case VIPS_INTERPRETATION_LAB:
		case VIPS_INTERPRETATION_RGB16:
		case VIPS_INTERPRETATION_scRGB:
		case VIPS_INTERPRETATION_HSV:
			max_bands = 3;
			break;

		case VIPS_INTERPRETATION_CMYK:
			max_bands = 4;
			break;

		default:
			max_bands = 0;
			break;
		}

		if (saveable == VIPS_FOREIGN_SAVEABLE_ANY)
			max_bands = in->Bands;
		else if (saveable & VIPS_FOREIGN_SAVEABLE_ALPHA)
			max_bands += 1;

		if (max_bands > 0 &&
			in->Bands > max_bands) {
			if (vips_extract_band(in, &out, 0,
					"n", max_bands,
					NULL)) {
				g_object_unref(in);
				return -1;
			}
			g_object_unref(in);
			in = out;
		}
	}

	/* Convert to the format the saver likes.
	 */
	if (in->Coding == VIPS_CODING_NONE) {
		/* If the saver does not support 16-bit output, automatically
		 * shift it down. This is the behaviour we want for saving an
		 * RGB16 image as JPEG, for example.
		 */
		gboolean needs_shift =
			!vips_band_format_is8bit(in->BandFmt) &&
			vips_band_format_is8bit(format[in->BandFmt]);

		if (vips_cast(in, &out, format[in->BandFmt],
			"shift", needs_shift,
			NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;
	}

	/* Does this class want a coded image?
	 */
	if ((in->Coding == VIPS_CODING_NONE &&
			(coding & VIPS_FOREIGN_CODING_NONE)) ||
		(in->Coding == VIPS_CODING_LABQ &&
			(coding & VIPS_FOREIGN_CODING_LABQ)) ||
		(in->Coding == VIPS_CODING_RAD &&
			(coding & VIPS_FOREIGN_CODING_RAD))) {
		/* Already there, nothing to do.
		 */
	}
	else if (coding & VIPS_FOREIGN_CODING_LABQ) {
		if (vips_Lab2LabQ(in, &out, NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;
	}
	else if (coding & VIPS_FOREIGN_CODING_RAD) {
		if (vips_float2rad(in, &out, NULL)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;
	}
	else if (coding & VIPS_FOREIGN_CODING_NONE) {
		if (vips_image_decode(in, &out)) {
			g_object_unref(in);
			return -1;
		}
		g_object_unref(in);
		in = out;
	}

	*ready = in;

	return 0;
}

static void *
vips_foreign_save_remove_metadata(VipsImage *image,
	const char *field, GValue *value, void *user_data)
{
	VipsForeignKeep keep = *((VipsForeignKeep *) user_data);

	// we are only interested in metadata
	if (!vips_isprefix("png-comment-", field) &&
		!vips_isprefix("magickprofile-", field) &&
		strcmp(field, VIPS_META_IMAGEDESCRIPTION) != 0 &&
		!g_str_has_suffix(field, "-data"))
		return NULL;

	if ((strcmp(field, VIPS_META_EXIF_NAME) == 0 &&
			(keep & VIPS_FOREIGN_KEEP_EXIF)) ||
		(strcmp(field, VIPS_META_XMP_NAME) == 0 &&
			(keep & VIPS_FOREIGN_KEEP_XMP)) ||
		(strcmp(field, VIPS_META_IPTC_NAME) == 0 &&
			(keep & VIPS_FOREIGN_KEEP_IPTC)) ||
		(strcmp(field, VIPS_META_ICC_NAME) == 0 &&
			(keep & VIPS_FOREIGN_KEEP_ICC)) ||
		(keep & VIPS_FOREIGN_KEEP_OTHER))
		return NULL;

	if (!vips_image_remove(image, field))
		return image;

	return NULL;
}

int
vips__foreign_update_metadata(VipsImage *in,
	VipsForeignKeep keep)
{
	/* Rebuild exif from tags, if we'll be saving it.
	 */
	if ((keep & VIPS_FOREIGN_KEEP_EXIF) &&
		vips__exif_update(in))
		return -1;

	/* Remove metadata, if any.
	 */
	if (keep != VIPS_FOREIGN_KEEP_ALL &&
		vips_image_map(in, vips_foreign_save_remove_metadata, &keep))
		return -1;

	/* Some format libraries, like libpng, will throw a hard error if the
	 * profile is inappropriate for this image type. With profiles inherited
	 * from a source image, this can happen all the time, so we
	 * want to silently drop the profile in this case.
	 */
	if ((keep & VIPS_FOREIGN_KEEP_ICC) &&
		vips_image_get_typeof(in, VIPS_META_ICC_NAME)) {
		const void *data;
		size_t length;

		if (!vips_image_get_blob(in, VIPS_META_ICC_NAME, &data, &length) &&
			!vips_icc_is_compatible_profile(in, data, length) &&
			!vips_image_remove(in, VIPS_META_ICC_NAME))
			return -1;
	}

	return 0;
}

static int
vips_foreign_save_build(VipsObject *object)
{
	VipsForeignSave *save = VIPS_FOREIGN_SAVE(object);

	/* The deprecated "strip" field sets "keep" to none.
	 */
	if (vips_object_argument_isset(object, "strip") &&
		!vips_object_argument_isset(object, "keep"))
		save->keep = save->strip
			? VIPS_FOREIGN_KEEP_NONE
			: VIPS_FOREIGN_KEEP_ALL;

	/* Keep ICC profile by default when a user profile has been set.
	 */
	if ((save->keep & VIPS_FOREIGN_KEEP_ICC) == 0 &&
		vips_object_argument_isset(object, "profile"))
		save->keep |= VIPS_FOREIGN_KEEP_ICC;

	if (save->in) {
		VipsForeignSaveClass *class = VIPS_FOREIGN_SAVE_GET_CLASS(save);
		VipsImage *ready;
		VipsImage *x;

		if (vips__foreign_convert_saveable(save->in, &ready,
				class->saveable, class->format_table, class->coding,
				save->background))
			return -1;

		/* Updating metadata, need to copy the image.
		 */
		if (vips_copy(ready, &x, NULL)) {
			VIPS_UNREF(ready);
			return -1;
		}
		VIPS_UNREF(ready);
		ready = x;

		if (vips__foreign_update_metadata(ready, save->keep)) {
			VIPS_UNREF(ready);
			return -1;
		}

		if (save->page_height)
			vips_image_set_int(ready, VIPS_META_PAGE_HEIGHT,
				save->page_height);

		VIPS_UNREF(save->ready);
		save->ready = ready;
	}

	return VIPS_OBJECT_CLASS(vips_foreign_save_parent_class)->build(object);
}

#define UC VIPS_FORMAT_UCHAR
#define C VIPS_FORMAT_CHAR
#define US VIPS_FORMAT_USHORT
#define S VIPS_FORMAT_SHORT
#define UI VIPS_FORMAT_UINT
#define I VIPS_FORMAT_INT
#define F VIPS_FORMAT_FLOAT
#define X VIPS_FORMAT_COMPLEX
#define D VIPS_FORMAT_DOUBLE
#define DX VIPS_FORMAT_DPCOMPLEX

static VipsBandFormat vips_foreign_save_format_table[10] = {
	/* Band format:  UC  C  US  S  UI  I  F  X  D  DX */
	/* Promotion: */ UC, C, US, S, UI, I, F, X, D, DX
};

static void
vips_foreign_save_class_init(VipsForeignSaveClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsOperationClass *operation_class = (VipsOperationClass *) class;

	gobject_class->dispose = vips_foreign_save_dispose;
	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->build = vips_foreign_save_build;
	object_class->summary_class = vips_foreign_save_summary_class;
	object_class->new_from_string = vips_foreign_save_new_from_string;
	object_class->nickname = "save";
	object_class->description = _("savers");

	/* All savers are sequential by definition. Things like tiled tiff
	 * write and interlaced png write, which are not, add extra caches
	 * on their input.
	 */
	operation_class->flags |= VIPS_OPERATION_SEQUENTIAL;

	/* Must not cache savers.
	 */
	operation_class->flags |= VIPS_OPERATION_NOCACHE;

	/* Default to no coding allowed.
	 */
	class->coding = VIPS_FOREIGN_CODING_NONE;

	/* Default to no cast on save.
	 */
	class->format_table = vips_foreign_save_format_table;

	VIPS_ARG_IMAGE(class, "in", 0,
		_("Input"),
		_("Image to save"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsForeignSave, in));

	VIPS_ARG_FLAGS(class, "keep", 100,
		_("Keep"),
		_("Which metadata to retain"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSave, keep),
		VIPS_TYPE_FOREIGN_KEEP,
		VIPS_FOREIGN_KEEP_ALL);

	VIPS_ARG_BOXED(class, "background", 101,
		_("Background"),
		_("Background value"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSave, background),
		VIPS_TYPE_ARRAY_DOUBLE);

	VIPS_ARG_INT(class, "page_height", 102,
		_("Page height"),
		_("Set page height for multipage save"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSave, page_height),
		0, VIPS_MAX_COORD, 0);

	VIPS_ARG_STRING(class, "profile", 103,
		_("Profile"),
		_("Filename of ICC profile to embed"),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET(VipsForeignSave, profile),
		NULL);

	VIPS_ARG_BOOL(class, "strip", 104,
		_("Strip"),
		_("Strip all metadata from image"),
		VIPS_ARGUMENT_OPTIONAL_INPUT | VIPS_ARGUMENT_DEPRECATED,
		G_STRUCT_OFFSET(VipsForeignSave, strip),
		FALSE);
}

static void
vips_foreign_save_init(VipsForeignSave *save)
{
	save->keep = VIPS_FOREIGN_KEEP_ALL;
	save->background = vips_array_double_newv(1, 0.0);
}

/* Can we write this filename with this class?
 */
static void *
vips_foreign_find_save_sub(VipsForeignSaveClass *save_class,
	const char *filename, void *b)
{
	VipsObjectClass *object_class = VIPS_OBJECT_CLASS(save_class);
	VipsForeignClass *class = VIPS_FOREIGN_CLASS(save_class);

	const char **p;

	/* All savers needs suffs defined since we use the suff to pick the
	 * saver.
	 */
	if (!class->suffs) {
		g_warning("no suffix defined for %s", object_class->nickname);
		return NULL;
	}

	/* Skip non-file savers.
	 */
	if (g_str_has_suffix(object_class->nickname, "_buffer") ||
		g_str_has_suffix(object_class->nickname, "_target"))
		return NULL;

	/* vips_foreign_find_save() has already removed any options from the
	 * end of the filename, so we can test directly against the suffix.
	 */
	for (p = class->suffs; *p; p++)
		if (vips_iscasepostfix(filename, *p))
			return save_class;

	return NULL;
}

/**
 * vips_foreign_find_save:
 * @filename: name to find a saver for
 *
 * Searches for an operation you could use to write to @filename.
 * Any trailing options on @filename are stripped and ignored.
 *
 * ::: seealso
 *     [func@Foreign.find_save_buffer], [method@Image.write_to_file].
 *
 * Returns: (nullable): the name of an operation on success, `NULL` on error
 */
const char *
vips_foreign_find_save(const char *name)
{
	char filename[VIPS_PATH_MAX];
	char option_string[VIPS_PATH_MAX];
	VipsForeignSaveClass *save_class;

	vips__filename_split8(name, filename, option_string);

	if (!(save_class = (VipsForeignSaveClass *) vips_foreign_map(
			  "VipsForeignSave",
			  (VipsSListMap2Fn) vips_foreign_find_save_sub,
			  (void *) filename, NULL))) {
		vips_error("VipsForeignSave",
			_("\"%s\" is not a known file format"), name);

		return NULL;
	}

	return G_OBJECT_CLASS_NAME(save_class);
}

static void *
vips_foreign_get_suffixes_count_cb(VipsForeignSaveClass *save_class,
	void *a, void *b)
{
	VipsForeignClass *foreign_class = VIPS_FOREIGN_CLASS(save_class);
	int *n_fields = (int *) a;

	int i;

	if (foreign_class->suffs)
		for (i = 0; foreign_class->suffs[i]; i++)
			*n_fields += 1;

	return NULL;
}

static void *
vips_foreign_get_suffixes_add_cb(VipsForeignSaveClass *save_class,
	void *a, void *b)
{
	VipsForeignClass *foreign_class = VIPS_FOREIGN_CLASS(save_class);
	gchar ***p = (gchar ***) a;

	int i;

	if (foreign_class->suffs)
		for (i = 0; foreign_class->suffs[i]; i++) {
			**p = g_strdup(foreign_class->suffs[i]);
			*p += 1;
		}

	return NULL;
}

/**
 * vips_foreign_get_suffixes:
 *
 * Get a `NULL`-terminated array listing all the supported suffixes.
 *
 * This is not the same as all the supported file types, since libvips
 * detects image format for load by testing the first few bytes.
 *
 * Use [func@Foreign.find_load] to detect type for a specific file.
 *
 * Free the return result with [func@GLib.strfreev].
 *
 * Returns: (transfer full) (array): all supported file extensions, as a
 * `NULL`-terminated array.
 */
gchar **
vips_foreign_get_suffixes(void)
{
	int n_suffs;
	gchar **suffs;
	gchar **p;

	n_suffs = 0;
	(void) vips_foreign_map(
		"VipsForeignSave",
		(VipsSListMap2Fn) vips_foreign_get_suffixes_count_cb,
		&n_suffs, NULL);

	suffs = g_new0(gchar *, n_suffs + 1);
	p = suffs;
	(void) vips_foreign_map(
		"VipsForeignSave",
		(VipsSListMap2Fn) vips_foreign_get_suffixes_add_cb,
		&p, NULL);

	return suffs;
}

/* Kept for early vips8 API compat.
 */

int
vips_foreign_save(VipsImage *in, const char *name, ...)
{
	char filename[VIPS_PATH_MAX];
	char option_string[VIPS_PATH_MAX];
	const char *operation_name;
	va_list ap;
	int result;

	vips__filename_split8(name, filename, option_string);

	if (!(operation_name = vips_foreign_find_save(filename)))
		return -1;

	va_start(ap, name);
	result = vips_call_split_option_string(operation_name, option_string,
		ap, in, filename);
	va_end(ap);

	return result;
}

/* Can this class write this filetype to a target?
 */
static void *
vips_foreign_find_save_target_sub(VipsForeignSaveClass *save_class,
	const char *suffix, void *b)
{
	VipsObjectClass *object_class = VIPS_OBJECT_CLASS(save_class);
	VipsForeignClass *class = VIPS_FOREIGN_CLASS(save_class);

	/* All concrete savers needs suffs, since we use the suff to pick the
	 * saver.
	 */
	if (!G_TYPE_IS_ABSTRACT(G_TYPE_FROM_CLASS(class)) &&
		!class->suffs)
		g_warning("no suffix defined for %s", object_class->nickname);

	if (!G_TYPE_IS_ABSTRACT(G_TYPE_FROM_CLASS(class)) &&
		class->suffs &&
		g_str_has_suffix(object_class->nickname, "_target") &&
		vips_filename_suffix_match(suffix, class->suffs))
		return save_class;

	return NULL;
}

/**
 * vips_foreign_find_save_target:
 * @suffix: format to find a saver for
 *
 * Searches for an operation you could use to write to a target in @suffix
 * format.
 *
 * ::: seealso
 *     [method@Image.write_to_buffer].
 *
 * Returns: (nullable): the name of an operation on success, `NULL` on error
 */
const char *
vips_foreign_find_save_target(const char *name)
{
	char suffix[VIPS_PATH_MAX];
	char option_string[VIPS_PATH_MAX];
	VipsForeignSaveClass *save_class;

	vips__filename_split8(name, suffix, option_string);

	if (!(save_class = (VipsForeignSaveClass *) vips_foreign_map(
			  "VipsForeignSave",
			  (VipsSListMap2Fn) vips_foreign_find_save_target_sub,
			  (void *) suffix, NULL))) {
		vips_error("VipsForeignSave",
			_("\"%s\" is not a known target format"), name);

		return NULL;
	}

	return G_OBJECT_CLASS_NAME(save_class);
}

/* Can we write this buffer with this file type?
 */
static void *
vips_foreign_find_save_buffer_sub(VipsForeignSaveClass *save_class,
	const char *suffix, void *b)
{
	VipsObjectClass *object_class = VIPS_OBJECT_CLASS(save_class);
	VipsForeignClass *class = VIPS_FOREIGN_CLASS(save_class);

	/* All concrete savers needs suffs, since we use the suff to pick the
	 * saver.
	 */
	if (!G_TYPE_IS_ABSTRACT(G_TYPE_FROM_CLASS(class)) &&
		!class->suffs)
		g_warning("no suffix defined for %s", object_class->nickname);

	if (!G_TYPE_IS_ABSTRACT(G_TYPE_FROM_CLASS(class)) &&
		class->suffs &&
		g_str_has_suffix(object_class->nickname, "_buffer") &&
		vips_filename_suffix_match(suffix, class->suffs))
		return save_class;

	return NULL;
}

/**
 * vips_foreign_find_save_buffer:
 * @suffix: name to find a saver for
 *
 * Searches for an operation you could use to write to a buffer in @suffix
 * format.
 *
 * ::: seealso
 *     [method@Image.write_to_buffer].
 *
 * Returns: (nullable): the name of an operation on success, `NULL` on error
 */
const char *
vips_foreign_find_save_buffer(const char *name)
{
	char suffix[VIPS_PATH_MAX];
	char option_string[VIPS_PATH_MAX];
	VipsForeignSaveClass *save_class;

	vips__filename_split8(name, suffix, option_string);

	if (!(save_class = (VipsForeignSaveClass *) vips_foreign_map(
			  "VipsForeignSave",
			  (VipsSListMap2Fn) vips_foreign_find_save_buffer_sub,
			  (void *) suffix, NULL))) {
		vips_error("VipsForeignSave",
			_("\"%s\" is not a known buffer format"), name);

		return NULL;
	}

	return G_OBJECT_CLASS_NAME(save_class);
}

/* C API wrappers for loadable modules go here.
 */

/**
 * vips_heifload:
 * @filename: file to load
 * @out: (out): decompressed image
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Read a HEIF image file into a VIPS image.
 *
 * Use @page to select a page to render, numbering from zero. If neither @n
 * nor @page are set, @page defaults to the primary page, otherwise to 0.
 *
 * Use @n to select the number of pages to render. The default is 1. Pages are
 * rendered in a vertical column. Set to -1 to mean "until the end of the
 * document". Use [method@Image.grid] to reorganise pages.
 *
 * HEIF images have a primary image. The metadata item `heif-primary` gives
 * the page number of the primary.
 *
 * If @thumbnail is `TRUE`, then fetch a stored thumbnail rather than the
 * image.
 *
 * By default, input image dimensions are limited to 16384x16384.
 * If @unlimited is `TRUE`, this increases to the maximum of 65535x65535.
 *
 * The bitdepth of the heic image is recorded in the metadata item
 * `heif-bitdepth`.
 *
 * ::: tip "Optional arguments"
 *     * @page: `gint`, page (top-level image number) to read
 *     * @n: `gint`, load this many pages
 *     * @thumbnail: `gboolean`, fetch thumbnail instead of image
 *     * @unlimited: `gboolean`, remove all denial of service limits
 *
 * ::: seealso
 *     [ctor@Image.new_from_file].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_heifload(const char *filename, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("heifload", ap, filename, out);
	va_end(ap);

	return result;
}

/**
 * vips_heifload_buffer:
 * @buf: (array length=len) (element-type guint8): memory area to load
 * @len: (type gsize): size of memory area
 * @out: (out): image to write
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Read a HEIF image file into a VIPS image.
 * Exactly as [ctor@Image.heifload], but read from a memory buffer.
 *
 * You must not free the buffer while @out is active. The
 * [signal@Object::postclose] signal on @out is a good place to free.
 *
 * ::: tip "Optional arguments"
 *     * @page: `gint`, page (top-level image number) to read
 *     * @n: `gint`, load this many pages
 *     * @thumbnail: `gboolean`, fetch thumbnail instead of image
 *     * @unlimited: `gboolean`, remove all denial of service limits
 *
 * ::: seealso
 *     [ctor@Image.heifload].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_heifload_buffer(void *buf, size_t len, VipsImage **out, ...)
{
	va_list ap;
	VipsBlob *blob;
	int result;

	/* We don't take a copy of the data or free it.
	 */
	blob = vips_blob_new(NULL, buf, len);

	va_start(ap, out);
	result = vips_call_split("heifload_buffer", ap, blob, out);
	va_end(ap);

	vips_area_unref(VIPS_AREA(blob));

	return result;
}

/**
 * vips_heifload_source:
 * @source: source to load from
 * @out: (out): image to write
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Exactly as [ctor@Image.heifload], but read from a source.
 *
 * ::: tip "Optional arguments"
 *     * @page: `gint`, page (top-level image number) to read
 *     * @n: `gint`, load this many pages
 *     * @thumbnail: `gboolean`, fetch thumbnail instead of image
 *     * @unlimited: `gboolean`, remove all denial of service limits
 *
 * ::: seealso
 *     [ctor@Image.heifload].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_heifload_source(VipsSource *source, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("heifload_source", ap, source, out);
	va_end(ap);

	return result;
}

/**
 * vips_heifsave: (method)
 * @in: image to save
 * @filename: file to write to
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Write a VIPS image to a file in HEIF format.
 *
 * Use @Q to set the compression factor. Default 50, which seems to be roughly
 * what the iphone uses. Q 30 gives about the same quality as JPEG Q 75.
 *
 * Set @lossless `TRUE` to switch to lossless compression.
 *
 * Use @compression to set the compression format e.g. HEVC, AVC, AV1 to use. It defaults to AV1
 * if the target filename ends with ".avif", otherwise HEVC.
 *
 * Use @effort to control the CPU effort spent improving compression.
 * This is currently only applicable to AV1 encoders. Defaults to 4, 0 is
 * fastest, 9 is slowest.
 *
 * Chroma subsampling is normally automatically disabled for Q >= 90. You can
 * force the subsampling mode with @subsample_mode.
 *
 * Use @bitdepth to set the bitdepth of the output file. HEIC supports at
 * least 8, 10 and 12 bits; other codecs may support more or fewer options.
 *
 * Use @encoder to set the encode library to use, e.g. aom, SVT-AV1, rav1e etc.
 *
 * ::: tip "Optional arguments"
 *     * @Q: `gint`, quality factor
 *     * @bitdepth: `gint`, set write bit depth to 8, 10, or 12 bits
 *     * @lossless: `gboolean`, enable lossless encoding
 *     * @compression: [enum@ForeignHeifCompression], write with this
 *       compression
 *     * @effort: `gint`, encoding effort
 *     * @subsample_mode: [class@Foreign]Subsample, chroma subsampling mode
 *     * @encoder: [class@Foreign]HeifEncoder, select encoder to use
 *
 * ::: seealso
 *     [method@Image.write_to_file], [ctor@Image.heifload].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_heifsave(VipsImage *in, const char *filename, ...)
{
	va_list ap;
	int result;

	va_start(ap, filename);
	result = vips_call_split("heifsave", ap, in, filename);
	va_end(ap);

	return result;
}

/**
 * vips_heifsave_buffer: (method)
 * @in: image to save
 * @buf: (array length=len) (element-type guint8): return output buffer here
 * @len: (type gsize): return output length here
 * @...: `NULL`-terminated list of optional named arguments
 *
 * As [method@Image.heifsave], but save to a memory buffer.
 *
 * The address of the buffer is returned in @obuf, the length of the buffer in
 * @olen. You are responsible for freeing the buffer with [func@GLib.free]
 * when you are done with it.
 *
 * ::: tip "Optional arguments"
 *     * @Q: `gint`, quality factor
 *     * @bitdepth: `gint`, set write bit depth to 8, 10, or 12 bits
 *     * @lossless: `gboolean`, enable lossless encoding
 *     * @compression: [enum@ForeignHeifCompression], write with this
 *       compression
 *     * @effort: `gint`, encoding effort
 *     * @subsample_mode: [class@Foreign]Subsample, chroma subsampling mode
 *     * @encoder: [class@Foreign]HeifEncoder, select encoder to use
 *
 * ::: seealso
 *     [method@Image.heifsave], [method@Image.write_to_file].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_heifsave_buffer(VipsImage *in, void **buf, size_t *len, ...)
{
	va_list ap;
	VipsArea *area;
	int result;

	area = NULL;

	va_start(ap, len);
	result = vips_call_split("heifsave_buffer", ap, in, &area);
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
 * vips_heifsave_target: (method)
 * @in: image to save
 * @target: save image to this target
 * @...: `NULL`-terminated list of optional named arguments
 *
 * As [method@Image.heifsave], but save to a target.
 *
 * ::: tip "Optional arguments"
 *     * @Q: `gint`, quality factor
 *     * @bitdepth: `gint`, set write bit depth to 8, 10, or 12 bits
 *     * @lossless: `gboolean`, enable lossless encoding
 *     * @compression: [enum@ForeignHeifCompression], write with this
 *       compression
 *     * @effort: `gint`, encoding effort
 *     * @subsample_mode: [class@Foreign]Subsample, chroma subsampling mode
 *     * @encoder: [class@Foreign]HeifEncoder, select encoder to use
 *
 * ::: seealso
 *     [method@Image.heifsave], [method@Image.write_to_target].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_heifsave_target(VipsImage *in, VipsTarget *target, ...)
{
	va_list ap;
	int result;

	va_start(ap, target);
	result = vips_call_split("heifsave_target", ap, in, target);
	va_end(ap);

	return result;
}

/**
 * vips_jxlload:
 * @filename: file to load
 * @out: (out): decompressed image
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Read a JPEG-XL image.
 *
 * The JPEG-XL loader and saver are experimental features and may change
 * in future libvips versions.
 *
 * ::: seealso
 *     [ctor@Image.new_from_file].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_jxlload(const char *filename, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("jxlload", ap, filename, out);
	va_end(ap);

	return result;
}

/**
 * vips_jxlload_buffer:
 * @buf: (array length=len) (element-type guint8): memory area to load
 * @len: (type gsize): size of memory area
 * @out: (out): image to write
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Exactly as [ctor@Image.jxlload], but read from a buffer.
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_jxlload_buffer(void *buf, size_t len, VipsImage **out, ...)
{
	va_list ap;
	VipsBlob *blob;
	int result;

	/* We don't take a copy of the data or free it.
	 */
	blob = vips_blob_new(NULL, buf, len);

	va_start(ap, out);
	result = vips_call_split("jxlload_buffer", ap, blob, out);
	va_end(ap);

	vips_area_unref(VIPS_AREA(blob));

	return result;
}

/**
 * vips_jxlload_source:
 * @source: source to load from
 * @out: (out): decompressed image
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Exactly as [ctor@Image.jxlload], but read from a source.
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_jxlload_source(VipsSource *source, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("jxlload_source", ap, source, out);
	va_end(ap);

	return result;
}

/**
 * vips_jxlsave: (method)
 * @in: image to save
 * @filename: file to write to
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Write a VIPS image to a file in JPEG-XL format.
 *
 * The JPEG-XL loader and saver are experimental features and may change
 * in future libvips versions.
 *
 * @tier sets the overall decode speed the encoder will target. Minimum is 0
 * (highest quality), and maximum is 4 (lowest quality). Default is 0.
 *
 * @distance sets the target maximum encoding error. Minimum is 0
 * (highest quality), and maximum is 15 (lowest quality). Default is 1.0
 * (visually lossless).
 *
 * As a convenience, you can also use @Q to set @distance. @Q uses
 * approximately the same scale as regular JPEG.
 *
 * Set @lossless to enable lossless compression.
 *
 * ::: tip "Optional arguments"
 *     * @tier: `gint`, decode speed tier
 *     * @distance: `gdouble`, maximum encoding error
 *     * @effort: `gint`, encoding effort
 *     * @lossless: `gboolean`, enables lossless compression
 *     * @Q: `gint`, quality setting
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_jxlsave(VipsImage *in, const char *filename, ...)
{
	va_list ap;
	int result;

	va_start(ap, filename);
	result = vips_call_split("jxlsave", ap, in, filename);
	va_end(ap);

	return result;
}

/**
 * vips_jxlsave_buffer: (method)
 * @in: image to save
 * @buf: (array length=len) (element-type guint8): return output buffer here
 * @len: (type gsize): return output length here
 * @...: `NULL`-terminated list of optional named arguments
 *
 * As [method@Image.jxlsave], but save to a memory buffer.
 *
 * ::: tip "Optional arguments"
 *     * @tier: `gint`, decode speed tier
 *     * @distance: `gdouble`, maximum encoding error
 *     * @effort: `gint`, encoding effort
 *     * @lossless: `gboolean`, enables lossless compression
 *     * @Q: `gint`, quality setting
 *
 * ::: seealso
 *     [method@Image.jxlsave], [method@Image.write_to_target].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_jxlsave_buffer(VipsImage *in, void **buf, size_t *len, ...)
{
	va_list ap;
	VipsArea *area;
	int result;

	area = NULL;

	va_start(ap, len);
	result = vips_call_split("jxlsave_buffer", ap, in, &area);
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
 * vips_jxlsave_target: (method)
 * @in: image to save
 * @target: save image to this target
 * @...: `NULL`-terminated list of optional named arguments
 *
 * As [method@Image.jxlsave], but save to a target.
 *
 * ::: tip "Optional arguments"
 *     * @tier: `gint`, decode speed tier
 *     * @distance: `gdouble`, maximum encoding error
 *     * @effort: `gint`, encoding effort
 *     * @lossless: `gboolean`, enables lossless compression
 *     * @Q: `gint`, quality setting
 *
 * ::: seealso
 *     [method@Image.jxlsave], [method@Image.write_to_target].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_jxlsave_target(VipsImage *in, VipsTarget *target, ...)
{
	va_list ap;
	int result;

	va_start(ap, target);
	result = vips_call_split("jxlsave_target", ap, in, target);
	va_end(ap);

	return result;
}

/**
 * vips_pdfload:
 * @filename: file to load
 * @out: (out): output image
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Render a PDF file into a VIPS image.
 *
 * The output image is always RGBA -- CMYK PDFs will be
 * converted. If you need CMYK bitmaps, you should use [ctor@Image.magickload]
 * instead.
 *
 * Use @page to select a page to render, numbering from zero.
 *
 * Use @n to select the number of pages to render. The default is 1. Pages are
 * rendered in a vertical column, with each individual page aligned to the
 * left. Set to -1 to mean "until the end of the document". Use
 * [method@Image.grid] to change page layout.
 *
 * Use @dpi to set the rendering resolution. The default is 72. Additionally,
 * you can scale by setting @scale. If you set both, they combine.
 *
 * Use @background to set the background RGBA colour. The default is 255
 * (solid white), use eg. 0 for a transparent background.
 *
 * Use @password to supply a decryption password.
 *
 * When using pdfium, the region of a page to render can be selected with
 * @page_box, defaulting to the crop box.
 *
 * The operation fills a number of header fields with metadata, for example
 * "pdf-author". They may be useful.
 *
 * This function only reads the image header and does not render any pixel
 * data. Rendering occurs when pixels are accessed.
 *
 * ::: tip "Optional arguments"
 *     * @page: `gint`, load this page, numbered from zero
 *     * @n: `gint`, load this many pages
 *     * @dpi: `gdouble`, render at this DPI
 *     * @scale: `gdouble`, scale render by this factor
 *     * @background: [struct@ArrayDouble], background colour
 *     * @page_box: [enum@ForeignPdfPageBox], use this page box (pdfium only)
 *
 * ::: seealso
 *     [ctor@Image.new_from_file], [ctor@Image.magickload].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_pdfload(const char *filename, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("pdfload", ap, filename, out);
	va_end(ap);

	return result;
}

/**
 * vips_pdfload_buffer:
 * @buf: (array length=len) (element-type guint8): memory area to load
 * @len: (type gsize): size of memory area
 * @out: (out): image to write
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Read a PDF-formatted memory buffer into a VIPS image. Exactly as
 * [ctor@Image.pdfload], but read from memory.
 *
 * You must not free the buffer while @out is active. The
 * [signal@Object::postclose] signal on @out is a good place to free.
 *
 * ::: tip "Optional arguments"
 *     * @page: `gint`, load this page, numbered from zero
 *     * @n: `gint`, load this many pages
 *     * @dpi: `gdouble`, render at this DPI
 *     * @scale: `gdouble`, scale render by this factor
 *     * @background: [struct@ArrayDouble], background colour
 *     * @page_box: [enum@ForeignPdfPageBox], use this page box (pdfium only)
 *
 * ::: seealso
 *     [ctor@Image.pdfload].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_pdfload_buffer(void *buf, size_t len, VipsImage **out, ...)
{
	va_list ap;
	VipsBlob *blob;
	int result;

	/* We don't take a copy of the data or free it.
	 */
	blob = vips_blob_new(NULL, buf, len);

	va_start(ap, out);
	result = vips_call_split("pdfload_buffer", ap, blob, out);
	va_end(ap);

	vips_area_unref(VIPS_AREA(blob));

	return result;
}

/**
 * vips_pdfload_source:
 * @source: source to load from
 * @out: (out): image to write
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Exactly as [ctor@Image.pdfload], but read from a source.
 *
 * ::: tip "Optional arguments"
 *     * @page: `gint`, load this page, numbered from zero
 *     * @n: `gint`, load this many pages
 *     * @dpi: `gdouble`, render at this DPI
 *     * @scale: `gdouble`, scale render by this factor
 *     * @background: [struct@ArrayDouble], background colour
 *     * @page_box: [enum@ForeignPdfPageBox], use this page box (pdfium only)
 *
 * ::: seealso
 *     [ctor@Image.pdfload]
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_pdfload_source(VipsSource *source, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("pdfload_source", ap, source, out);
	va_end(ap);

	return result;
}

/**
 * vips_openslideload:
 * @filename: file to load
 * @out: (out): decompressed image
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Read a virtual slide supported by the OpenSlide library into a VIPS image.
 * OpenSlide supports images in Aperio, Hamamatsu, MIRAX, Sakura, Trestle,
 * and Ventana formats.
 *
 * To facilitate zooming, virtual slide formats include multiple scaled-down
 * versions of the high-resolution image.  These are typically called
 * "levels".  By default, [ctor@Image.openslideload] reads the
 * highest-resolution level (level 0).  Set @level to the level number you want.
 *
 * In addition to the slide image itself, virtual slide formats sometimes
 * include additional images, such as a scan of the slide's barcode.
 * OpenSlide calls these "associated images".  To read an associated image,
 * set @associated to the image's name.
 * A slide's associated images are listed in the
 * "slide-associated-images" metadata item.
 *
 * If you set @attach_associated, then all associated images are attached as
 * metadata items. Use [method@Image.get_image] on @out to retrieve them. Images
 * are attached as "openslide-associated-XXXXX", where XXXXX is the name of the
 * associated image.
 *
 * By default, the output of this operator is RGBA. Set @rgb to enable RGB
 * output.
 *
 * ::: tip "Optional arguments"
 *     * @level: `gint`, load this level
 *     * @associated: `gchararray`, load this associated image
 *     * @attach_associated: `gboolean`, attach all associated images as metadata
 *     * @autocrop: `gboolean`, crop to image bounds
 *     * @rgb: `gboolean`, output RGB (not RGBA) pixels
 *
 * ::: seealso
 *     [ctor@Image.new_from_file].
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_openslideload(const char *filename, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("openslideload", ap, filename, out);
	va_end(ap);

	return result;
}

/**
 * vips_openslideload_source:
 * @source: source to load from
 * @out: (out): decompressed image
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Exactly as [ctor@Image.openslideload], but read from a source.
 *
 * ::: tip "Optional arguments"
 *     * @level: `gint`, load this level
 *     * @associated: `gchararray`, load this associated image
 *     * @attach_associated: `gboolean`, attach all associated images as metadata
 *     * @autocrop: `gboolean`, crop to image bounds
 *     * @rgb: `gboolean`, output RGB (not RGBA) pixels
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_openslideload_source(VipsSource *source, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("openslideload_source", ap, source, out);
	va_end(ap);

	return result;
}

/* Called from iofuncs to init all operations in this dir. Use a plugin system
 * instead?
 */
void
vips_foreign_operation_init(void)
{
	extern GType vips_foreign_load_rad_file_get_type(void);
	extern GType vips_foreign_load_rad_buffer_get_type(void);
	extern GType vips_foreign_load_rad_source_get_type(void);
	extern GType vips_foreign_save_rad_file_get_type(void);
	extern GType vips_foreign_save_rad_buffer_get_type(void);
	extern GType vips_foreign_save_rad_target_get_type(void);

	extern GType vips_foreign_load_mat_get_type(void);

	extern GType vips_foreign_load_ppm_file_get_type(void);
	extern GType vips_foreign_load_ppm_buffer_get_type(void);
	extern GType vips_foreign_load_ppm_source_get_type(void);
	extern GType vips_foreign_save_ppm_file_get_type(void);
	extern GType vips_foreign_save_pbm_target_get_type(void);
	extern GType vips_foreign_save_pgm_target_get_type(void);
	extern GType vips_foreign_save_ppm_target_get_type(void);
	extern GType vips_foreign_save_pfm_target_get_type(void);
	extern GType vips_foreign_save_pnm_target_get_type(void);

	extern GType vips_foreign_load_png_file_get_type(void);
	extern GType vips_foreign_load_png_buffer_get_type(void);
	extern GType vips_foreign_load_png_source_get_type(void);
	extern GType vips_foreign_save_png_file_get_type(void);
	extern GType vips_foreign_save_png_buffer_get_type(void);
	extern GType vips_foreign_save_png_target_get_type(void);

	extern GType vips_foreign_save_spng_file_get_type(void);
	extern GType vips_foreign_save_spng_buffer_get_type(void);
	extern GType vips_foreign_save_spng_target_get_type(void);

	extern GType vips_foreign_load_csv_file_get_type(void);
	extern GType vips_foreign_load_csv_source_get_type(void);
	extern GType vips_foreign_save_csv_file_get_type(void);
	extern GType vips_foreign_save_csv_target_get_type(void);

	extern GType vips_foreign_load_matrix_file_get_type(void);
	extern GType vips_foreign_load_matrix_source_get_type(void);
	extern GType vips_foreign_save_matrix_file_get_type(void);
	extern GType vips_foreign_save_matrix_target_get_type(void);
	extern GType vips_foreign_print_matrix_get_type(void);

	extern GType vips_foreign_load_fits_file_get_type(void);
	extern GType vips_foreign_load_fits_source_get_type(void);
	extern GType vips_foreign_save_fits_get_type(void);

	extern GType vips_foreign_load_analyze_get_type(void);

	extern GType vips_foreign_load_openexr_get_type(void);

	extern GType vips_foreign_load_openslide_file_get_type(void);
	extern GType vips_foreign_load_openslide_source_get_type(void);

	extern GType vips_foreign_load_vips_file_get_type(void);
	extern GType vips_foreign_load_vips_source_get_type(void);
	extern GType vips_foreign_save_vips_file_get_type(void);
	extern GType vips_foreign_save_vips_target_get_type(void);

	extern GType vips_foreign_load_jpeg_file_get_type(void);
	extern GType vips_foreign_load_jpeg_buffer_get_type(void);
	extern GType vips_foreign_load_jpeg_source_get_type(void);
	extern GType vips_foreign_save_jpeg_file_get_type(void);
	extern GType vips_foreign_save_jpeg_buffer_get_type(void);
	extern GType vips_foreign_save_jpeg_target_get_type(void);
	extern GType vips_foreign_save_jpeg_mime_get_type(void);

	extern GType vips_foreign_load_tiff_file_get_type(void);
	extern GType vips_foreign_load_tiff_buffer_get_type(void);
	extern GType vips_foreign_load_tiff_source_get_type(void);
	extern GType vips_foreign_save_tiff_file_get_type(void);
	extern GType vips_foreign_save_tiff_buffer_get_type(void);
	extern GType vips_foreign_save_tiff_target_get_type(void);

	extern GType vips_foreign_load_raw_get_type(void);
	extern GType vips_foreign_save_raw_file_get_type(void);
	extern GType vips_foreign_save_raw_buffer_get_type(void);
	extern GType vips_foreign_save_raw_target_get_type(void);

	extern GType vips_foreign_load_magick_file_get_type(void);
	extern GType vips_foreign_load_magick_buffer_get_type(void);
	extern GType vips_foreign_load_magick_source_get_type(void);
	extern GType vips_foreign_load_magick7_file_get_type(void);
	extern GType vips_foreign_load_magick7_buffer_get_type(void);
	extern GType vips_foreign_load_magick7_source_get_type(void);

	extern GType vips_foreign_save_magick_file_get_type(void);
	extern GType vips_foreign_save_magick_buffer_get_type(void);
	extern GType vips_foreign_save_magick_bmp_file_get_type(void);
	extern GType vips_foreign_save_magick_bmp_buffer_get_type(void);
	extern GType vips_foreign_save_magick_gif_file_get_type(void);
	extern GType vips_foreign_save_magick_gif_buffer_get_type(void);

	extern GType vips_foreign_save_dz_file_get_type(void);
	extern GType vips_foreign_save_dz_buffer_get_type(void);
	extern GType vips_foreign_save_dz_target_get_type(void);

	extern GType vips_foreign_load_webp_file_get_type(void);
	extern GType vips_foreign_load_webp_buffer_get_type(void);
	extern GType vips_foreign_load_webp_source_get_type(void);
	extern GType vips_foreign_save_webp_file_get_type(void);
	extern GType vips_foreign_save_webp_buffer_get_type(void);
	extern GType vips_foreign_save_webp_target_get_type(void);
	extern GType vips_foreign_save_webp_mime_get_type(void);

	extern GType vips_foreign_load_pdf_file_get_type(void);
	extern GType vips_foreign_load_pdf_buffer_get_type(void);
	extern GType vips_foreign_load_pdf_source_get_type(void);

	extern GType vips_foreign_load_svg_file_get_type(void);
	extern GType vips_foreign_load_svg_buffer_get_type(void);
	extern GType vips_foreign_load_svg_source_get_type(void);

	extern GType vips_foreign_load_jp2k_file_get_type(void);
	extern GType vips_foreign_load_jp2k_buffer_get_type(void);
	extern GType vips_foreign_load_jp2k_source_get_type(void);
	extern GType vips_foreign_save_jp2k_file_get_type(void);
	extern GType vips_foreign_save_jp2k_buffer_get_type(void);
	extern GType vips_foreign_save_jp2k_target_get_type(void);

	extern GType vips_foreign_load_jxl_file_get_type(void);
	extern GType vips_foreign_load_jxl_buffer_get_type(void);
	extern GType vips_foreign_load_jxl_source_get_type(void);
	extern GType vips_foreign_save_jxl_file_get_type(void);
	extern GType vips_foreign_save_jxl_buffer_get_type(void);
	extern GType vips_foreign_save_jxl_target_get_type(void);

	extern GType vips_foreign_load_heif_file_get_type(void);
	extern GType vips_foreign_load_heif_buffer_get_type(void);
	extern GType vips_foreign_load_heif_source_get_type(void);
	extern GType vips_foreign_save_heif_file_get_type(void);
	extern GType vips_foreign_save_heif_buffer_get_type(void);
	extern GType vips_foreign_save_heif_target_get_type(void);
	extern GType vips_foreign_save_avif_target_get_type(void);

	extern GType vips_foreign_load_nifti_file_get_type(void);
	extern GType vips_foreign_load_nifti_source_get_type(void);
	extern GType vips_foreign_save_nifti_get_type(void);

	extern GType vips_foreign_load_nsgif_file_get_type(void);
	extern GType vips_foreign_load_nsgif_buffer_get_type(void);
	extern GType vips_foreign_load_nsgif_source_get_type(void);

	extern GType vips_foreign_save_cgif_file_get_type(void);
	extern GType vips_foreign_save_cgif_buffer_get_type(void);
	extern GType vips_foreign_save_cgif_target_get_type(void);

	extern GType vips_foreign_load_dcraw_file_get_type(void);
	extern GType vips_foreign_load_dcraw_buffer_get_type(void);
	extern GType vips_foreign_load_dcraw_source_get_type(void);

	vips_foreign_load_csv_file_get_type();
	vips_foreign_load_csv_source_get_type();
	vips_foreign_save_csv_file_get_type();
	vips_foreign_save_csv_target_get_type();

	vips_foreign_load_matrix_file_get_type();
	vips_foreign_load_matrix_source_get_type();
	vips_foreign_save_matrix_file_get_type();
	vips_foreign_save_matrix_target_get_type();
	vips_foreign_print_matrix_get_type();

	vips_foreign_load_raw_get_type();
	vips_foreign_save_raw_file_get_type();
	vips_foreign_save_raw_buffer_get_type();
	vips_foreign_save_raw_target_get_type();

	vips_foreign_load_vips_file_get_type();
	vips_foreign_load_vips_source_get_type();
	vips_foreign_save_vips_file_get_type();
	vips_foreign_save_vips_target_get_type();

#ifdef HAVE_ANALYZE
	vips_foreign_load_analyze_get_type();
#endif /*HAVE_ANALYZE*/

#ifdef HAVE_PPM
	vips_foreign_load_ppm_file_get_type();
	vips_foreign_load_ppm_buffer_get_type();
	vips_foreign_load_ppm_source_get_type();
	vips_foreign_save_ppm_file_get_type();
	vips_foreign_save_pbm_target_get_type();
	vips_foreign_save_pgm_target_get_type();
	vips_foreign_save_ppm_target_get_type();
	vips_foreign_save_pfm_target_get_type();
	vips_foreign_save_pnm_target_get_type();
#endif /*HAVE_PPM*/

#ifdef HAVE_RADIANCE
	vips_foreign_load_rad_file_get_type();
	vips_foreign_load_rad_buffer_get_type();
	vips_foreign_load_rad_source_get_type();
	vips_foreign_save_rad_file_get_type();
	vips_foreign_save_rad_buffer_get_type();
	vips_foreign_save_rad_target_get_type();
#endif /*HAVE_RADIANCE*/

#if defined(HAVE_POPPLER) && !defined(POPPLER_MODULE)
	vips_foreign_load_pdf_file_get_type();
	vips_foreign_load_pdf_buffer_get_type();
	vips_foreign_load_pdf_source_get_type();
#endif /*defined(HAVE_POPPLER) && !defined(POPPLER_MODULE)*/

#ifdef HAVE_PDFIUM
	vips_foreign_load_pdf_file_get_type();
	vips_foreign_load_pdf_buffer_get_type();
	vips_foreign_load_pdf_source_get_type();
#endif /*HAVE_PDFIUM*/

#ifdef HAVE_RSVG
	vips_foreign_load_svg_file_get_type();
	vips_foreign_load_svg_buffer_get_type();
	vips_foreign_load_svg_source_get_type();
#endif /*HAVE_RSVG*/

#if defined(HAVE_LIBJXL) && !defined(LIBJXL_MODULE)
	vips_foreign_load_jxl_file_get_type();
	vips_foreign_load_jxl_buffer_get_type();
	vips_foreign_load_jxl_source_get_type();
	vips_foreign_save_jxl_file_get_type();
	vips_foreign_save_jxl_buffer_get_type();
	vips_foreign_save_jxl_target_get_type();
#endif /*defined(HAVE_LIBJXL) && !defined(LIBJXL_MODULE)*/

#ifdef HAVE_LIBOPENJP2
	vips_foreign_load_jp2k_file_get_type();
	vips_foreign_load_jp2k_buffer_get_type();
	vips_foreign_load_jp2k_source_get_type();
	vips_foreign_save_jp2k_file_get_type();
	vips_foreign_save_jp2k_buffer_get_type();
	vips_foreign_save_jp2k_target_get_type();
#endif /*HAVE_LIBOPENJP2*/

#ifdef HAVE_NSGIF
	vips_foreign_load_nsgif_file_get_type();
	vips_foreign_load_nsgif_buffer_get_type();
	vips_foreign_load_nsgif_source_get_type();
#endif /*HAVE_NSGIF*/

#ifdef HAVE_LIBRAW
	vips_foreign_load_dcraw_file_get_type();
	vips_foreign_load_dcraw_buffer_get_type();
	vips_foreign_load_dcraw_source_get_type();
#endif /*HAVE_LIBRAW*/

#ifdef HAVE_CGIF
	vips_foreign_save_cgif_file_get_type();
	vips_foreign_save_cgif_buffer_get_type();
	vips_foreign_save_cgif_target_get_type();
#endif /*HAVE_CGIF*/

#ifdef HAVE_LIBARCHIVE
	vips_foreign_save_dz_file_get_type();
	vips_foreign_save_dz_buffer_get_type();
	vips_foreign_save_dz_target_get_type();
#endif /*HAVE_LIBARCHIVE*/

#ifdef HAVE_PNG
	vips_foreign_load_png_file_get_type();
	vips_foreign_load_png_buffer_get_type();
	vips_foreign_load_png_source_get_type();
	vips_foreign_save_png_file_get_type();
	vips_foreign_save_png_buffer_get_type();
	vips_foreign_save_png_target_get_type();
#endif /*HAVE_PNG*/

#ifdef HAVE_SPNG
	vips_foreign_load_png_file_get_type();
	vips_foreign_load_png_buffer_get_type();
	vips_foreign_load_png_source_get_type();
	vips_foreign_save_spng_file_get_type();
	vips_foreign_save_spng_buffer_get_type();
	vips_foreign_save_spng_target_get_type();
#endif /*HAVE_SPNG*/

#ifdef HAVE_MATIO
	vips_foreign_load_mat_get_type();
#endif /*HAVE_MATIO*/

#ifdef HAVE_JPEG
	vips_foreign_load_jpeg_file_get_type();
	vips_foreign_load_jpeg_buffer_get_type();
	vips_foreign_load_jpeg_source_get_type();
	vips_foreign_save_jpeg_file_get_type();
	vips_foreign_save_jpeg_buffer_get_type();
	vips_foreign_save_jpeg_target_get_type();
	vips_foreign_save_jpeg_mime_get_type();
#endif /*HAVE_JPEG*/

#ifdef HAVE_LIBWEBP
	vips_foreign_load_webp_file_get_type();
	vips_foreign_load_webp_buffer_get_type();
	vips_foreign_load_webp_source_get_type();
	vips_foreign_save_webp_file_get_type();
	vips_foreign_save_webp_buffer_get_type();
	vips_foreign_save_webp_target_get_type();
	vips_foreign_save_webp_mime_get_type();
#endif /*HAVE_LIBWEBP*/

#ifdef HAVE_TIFF
	vips_foreign_load_tiff_file_get_type();
	vips_foreign_load_tiff_buffer_get_type();
	vips_foreign_load_tiff_source_get_type();
	vips_foreign_save_tiff_file_get_type();
	vips_foreign_save_tiff_buffer_get_type();
	vips_foreign_save_tiff_target_get_type();
#endif /*HAVE_TIFF*/

#if defined(HAVE_OPENSLIDE) && !defined(OPENSLIDE_MODULE)
	vips_foreign_load_openslide_file_get_type();
	vips_foreign_load_openslide_source_get_type();
#endif /*defined(HAVE_OPENSLIDE) && !defined(OPENSLIDE_MODULE)*/

#if defined(ENABLE_MAGICKLOAD) && !defined(MAGICK_MODULE)
#ifdef HAVE_MAGICK6
	vips_foreign_load_magick_file_get_type();
	vips_foreign_load_magick_buffer_get_type();
	vips_foreign_load_magick_source_get_type();
#endif /*HAVE_MAGICK6*/

#ifdef HAVE_MAGICK7
	vips_foreign_load_magick7_file_get_type();
	vips_foreign_load_magick7_buffer_get_type();
	vips_foreign_load_magick7_source_get_type();
#endif /*HAVE_MAGICK7*/
#endif /*defined(ENABLE_MAGICKLOAD) && !defined(MAGICK_MODULE)*/

#if defined(ENABLE_MAGICKSAVE) && !defined(MAGICK_MODULE)
	vips_foreign_save_magick_file_get_type();
	vips_foreign_save_magick_buffer_get_type();
	vips_foreign_save_magick_bmp_file_get_type();
	vips_foreign_save_magick_bmp_buffer_get_type();
	vips_foreign_save_magick_gif_file_get_type();
	vips_foreign_save_magick_gif_buffer_get_type();
#endif /*defined(ENABLE_MAGICKSAVE) && !defined(MAGICK_MODULE)*/

#ifdef HAVE_CFITSIO
	vips_foreign_load_fits_file_get_type();
	vips_foreign_load_fits_source_get_type();
	vips_foreign_save_fits_get_type();
#endif /*HAVE_CFITSIO*/

#ifdef HAVE_OPENEXR
	vips_foreign_load_openexr_get_type();
#endif /*HAVE_OPENEXR*/

#ifdef HAVE_NIFTI
	vips_foreign_load_nifti_file_get_type();
	vips_foreign_load_nifti_source_get_type();
	vips_foreign_save_nifti_get_type();
#endif /*HAVE_NIFTI*/

#if defined(HAVE_HEIF) && !defined(HEIF_MODULE)
	vips_foreign_load_heif_file_get_type();
	vips_foreign_load_heif_buffer_get_type();
	vips_foreign_load_heif_source_get_type();
	vips_foreign_save_heif_file_get_type();
	vips_foreign_save_heif_buffer_get_type();
	vips_foreign_save_heif_target_get_type();
	vips_foreign_save_avif_target_get_type();
#endif /*defined(HAVE_HEIF) && !defined(HEIF_MODULE)*/

	vips__foreign_load_operation =
		g_quark_from_static_string("vips-foreign-load-operation");
}
