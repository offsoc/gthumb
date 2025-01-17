/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2001 The Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#ifndef TYPEDEFS_H
#define TYPEDEFS_H


#include <glib.h>

#define FILE_PREFIX    "file://"
#define CATALOG_PREFIX "catalog://"
#define SEARCH_PREFIX  "search://"

#define FILE_PREFIX_L    7
#define CATALOG_PREFIX_L 10
#define SEARCH_PREFIX_L  9

#define RC_DIR                    ".gnome2/gthumb"

#define RC_CATALOG_DIR            ".gnome2/gthumb/collections"
#define RC_COMMENTS_DIR           ".gnome2/gthumb/comments"
#define RC_BOOKMARKS_FILE         ".gnome2/gthumb/bookmarks"
#define RC_HISTORY_FILE           ".gnome2/gthumb/history"
#define RC_CATEGORIES_FILE        ".gnome2/gthumb/categories"

#define OLD_RC_CATALOG_DIR        ".gqview/collections"
#define OLD_RC_COMMENTS_DIR       ".gqview/comments"
#define OLD_RC_BOOKMARKS_FILE     ".gqview/bookmarks"
#define OLD_RC_HISTORY_FILE       ".gqview/history"
#define OLD_RC_CATEGORIES_FILE    ".gqview/categories"

#define CACHE_DIR                 ".thumbnails"

#define CACHE_THUMB_EXT           ".png"
#define CATALOG_EXT               ".gqv"
#define COMMENT_EXT               ".xml"

#define CLIST_WIDTH_DEFAULT       100
#define CLIST_HEIGHT_DEFAULT      100

#define CLIST_ROW_PAD             5
#define CLIST_IMAGE_ROW_PAD       2

#define BOOKMARKS_MENU_MAX_LENGTH 35
#define HISTORY_LIST_MAX_LENGTH   35

typedef enum { /*< skip >*/
	GTH_DIRECTION_FORWARD = 0,
	GTH_DIRECTION_REVERSE = 1,
	GTH_DIRECTION_RANDOM  = 2,
} GthDirectionType;


typedef enum { /*< skip >*/
	GTH_TOOLBAR_STYLE_SYSTEM = 0,
	GTH_TOOLBAR_STYLE_TEXT_BELOW,
	GTH_TOOLBAR_STYLE_TEXT_BESIDE,
	GTH_TOOLBAR_STYLE_ICONS,
	GTH_TOOLBAR_STYLE_TEXT
} GthToolbarStyle;


typedef enum { /*< skip >*/
	GTH_SIDEBAR_NO_LIST = 0,
	GTH_SIDEBAR_DIR_LIST,
	GTH_SIDEBAR_CATALOG_LIST,
	GTH_SIDEBAR_NUM_CONTENT_TYPES
} GthSidebarContent;


typedef enum { /*< skip >*/
	GTH_VIEW_MODE_VOID,               /* No text at all. */
	GTH_VIEW_MODE_LABEL,              /* Display label. */
	GTH_VIEW_MODE_COMMENTS,           /* Display comment. */
 	GTH_VIEW_MODE_COMMENTS_OR_TEXT,   /* When a comment is present do
					   * not display text. */
	GTH_VIEW_MODE_ALL                 /* Display comment and text. */
} GthViewMode;


typedef enum { /*< skip >*/
	GTH_VIEW_AS_LIST = 0,
	GTH_VIEW_AS_THUMBNAILS
} GthViewAs;


typedef enum { /*< skip >*/
	GTH_VISIBILITY_NONE = 0,
	GTH_VISIBILITY_FULL,
	GTH_VISIBILITY_PARTIAL,
	GTH_VISIBILITY_PARTIAL_TOP,
	GTH_VISIBILITY_PARTIAL_BOTTOM
} GthVisibility;


typedef enum { /*< skip >*/
	GTH_PRINT_UNIT_MM = 0,
	GTH_PRINT_UNIT_IN
} GthPrintUnit;


/* keep the order of the items in sync with the order of the 
 * sort_by_radio_list structure in the file menu.h */
typedef enum { /*< skip >*/
	GTH_SORT_METHOD_NONE,
	GTH_SORT_METHOD_BY_NAME,
	GTH_SORT_METHOD_BY_PATH,
	GTH_SORT_METHOD_BY_SIZE,
	GTH_SORT_METHOD_BY_TIME
} GthSortMethod;


typedef enum { /*< skip >*/
	GTH_CLICK_POLICY_FOLLOW_NAUTILUS = 0,
	GTH_CLICK_POLICY_SINGLE,
	GTH_CLICK_POLICY_DOUBLE
} GthClickPolicy;


typedef enum { /*< skip >*/
	GTH_OVERWRITE_SKIP,
	GTH_OVERWRITE_RENAME,
	GTH_OVERWRITE_ASK,
	GTH_OVERWRITE_OVERWRITE,
} GthOverwriteMode;


typedef enum { /*< skip >*/
	GTH_CAPTION_COMMENT             = 1 << 0,
	GTH_CAPTION_FILE_PATH           = 1 << 1,
	GTH_CAPTION_FILE_NAME           = 1 << 2,
	GTH_CAPTION_FILE_SIZE           = 1 << 3,
	GTH_CAPTION_IMAGE_DIM           = 1 << 4,
	GTH_CAPTION_EXIF_EXPOSURE_TIME  = 1 << 5,
	GTH_CAPTION_EXIF_EXPOSURE_MODE  = 1 << 6,
	GTH_CAPTION_EXIF_FLASH          = 1 << 7,
	GTH_CAPTION_EXIF_SHUTTER_SPEED  = 1 << 8,
	GTH_CAPTION_EXIF_APERTURE_VALUE = 1 << 9,
	GTH_CAPTION_EXIF_FOCAL_LENGTH   = 1 << 10,
	GTH_CAPTION_EXIF_DATE_TIME      = 1 << 11,
	GTH_CAPTION_EXIF_CAMERA_MODEL   = 1 << 12
} GthCaptionFields;

 
typedef enum { /*< skip >*/
	GTH_FRAME_STYLE_NONE               = 1 << 0,
	GTH_FRAME_STYLE_SIMPLE             = 1 << 1,
	GTH_FRAME_STYLE_SIMPLE_WITH_SHADOW = 1 << 2,
	GTH_FRAME_STYLE_SHADOW             = 1 << 3,
	GTH_FRAME_STYLE_SLIDE              = 1 << 4,
	GTH_FRAME_STYLE_SHADOW_IN          = 1 << 5,
	GTH_FRAME_STYLE_SHADOW_OUT         = 1 << 6
} GthFrameStyle;


typedef enum { /*< skip >*/
	GTH_PREVIEW_CONTENT_IMAGE,
	GTH_PREVIEW_CONTENT_DATA,
	GTH_PREVIEW_CONTENT_COMMENT,
} GthPreviewContent;


typedef enum { /*< skip >*/
	GTH_CROP_RATIO_NONE,
	GTH_CROP_RATIO_SQUARE,
	GTH_CROP_RATIO_IMAGE,
	GTH_CROP_RATIO_DISPLAY,
	GTH_CROP_RATIO_4_3,
	GTH_CROP_RATIO_4_6,
	GTH_CROP_RATIO_5_7,
	GTH_CROP_RATIO_8_10,
	GTH_CROP_RATIO_CUSTOM,
} GthCropRatio;


typedef void (*ErrorFunc)      (gpointer data);

typedef void (*DoneFunc)       (gpointer data);

typedef void (*ProgressFunc)   (gfloat percent, 
				gpointer data);

typedef void (*AreaReadyFunc)  (guint x,
				guint y, 
				guint w, 
				guint h, 
				gpointer data);


#endif /* TYPEDEFS_H */

