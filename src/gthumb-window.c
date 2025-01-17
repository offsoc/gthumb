/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2001, 2003, 2004 Free Software Foundation, Inc.
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

#include <config.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#include <gdk/gdkkeysyms.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <libgnomeui/gnome-window-icon.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-result.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "actions.h"
#include "auto-completion.h"
#include "catalog.h"
#include "catalog-list.h"
#include "comments.h"
#include "dlg-image-prop.h"
#include "dlg-bookmarks.h"
#include "dlg-categories.h"
#include "dlg-comment.h"
#include "dlg-file-utils.h"
#include "dlg-save-image.h"
#include "e-combo-button.h"
#include "file-utils.h"
#include "jpeg-utils.h"
#include "gth-file-list.h"
#include "gth-toggle-button.h"
#include "gconf-utils.h"
#include "glib-utils.h"
#include "gthumb-window.h"
#include "gthumb-info-bar.h"
#include "gthumb-stock.h"
#include "gtk-utils.h"
#include "gth-file-view.h"
#include "gth-exif-data-viewer.h"
#include "image-viewer.h"
#include "main.h"
#include "nav-window.h"
#include "pixbuf-utils.h"
#include "thumb-cache.h"
#include "gth-exif-utils.h"
#include "ui.h"

#ifdef HAVE_LIBEXIF
#include <libexif/exif-data.h>
#include "jpegutils/jpeg-data.h"
#endif /* HAVE_LIBEXIF */

#include "icons/pixbufs.h"
#include "icons/nav_button.xpm"

#define ACTIVITY_DELAY         200
#define BUSY_CURSOR_DELAY      200
#define DISPLAY_PROGRESS_DELAY 750
#define HIDE_SIDEBAR_DELAY     150
#define LOAD_DIR_DELAY         75
#define PANE_MIN_SIZE          60
#define PROGRESS_BAR_WIDTH     60
#define SEL_CHANGED_DELAY      150
#define UPDATE_DIR_DELAY       500
#define VIEW_IMAGE_DELAY       25
#define AUTO_LOAD_DELAY        750
#define UPDATE_LAYOUT_DELAY    250

#define DEF_WIN_WIDTH          690
#define DEF_WIN_HEIGHT         460
#define DEF_SIDEBAR_SIZE       255
#define DEF_SIDEBAR_CONT_SIZE  190
#define DEF_SLIDESHOW_DELAY    4
#define PRELOADED_IMAGE_MAX_SIZE (1.5*1024*1024)
#define PRELOADED_IMAGE_MAX_DIM1 (3000*3000)
#define PRELOADED_IMAGE_MAX_DIM2 (1500*1500)

#define GLADE_EXPORTER_FILE    "gthumb_png_exporter.glade"
#define HISTORY_LIST_MENU      "/MenuBar/View/Go/HistoryList"
#define HISTORY_LIST_POPUP     "/HistoryListPopup"

enum {
	NAME_COLUMN,
	VALUE_COLUMN,
	POS_COLUMN,
	NUM_COLUMNS
};

enum {
	TARGET_PLAIN,
	TARGET_PLAIN_UTF8,
	TARGET_URILIST,
};


static GtkTargetEntry target_table[] = {
	{ "text/uri-list", 0, TARGET_URILIST },
	/*
	  { "text/plain;charset=UTF-8",    0, TARGET_PLAIN_UTF8 },
	  { "text/plain",    0, TARGET_PLAIN }
	*/
};

/* FIXME
   static GtkTargetEntry same_app_target_table[] = {
   { "text/uri-list", GTK_TARGET_SAME_APP, TARGET_URILIST },
   { "text/plain",    GTK_TARGET_SAME_APP, TARGET_PLAIN }
   };
*/


static GtkTreePath  *dir_list_tree_path = NULL;
static GtkTreePath  *catalog_list_tree_path = NULL;
static GList        *gth_file_list_drag_data = NULL;
static char         *dir_list_drag_data = NULL;





static void 
set_action_sensitive (GThumbWindow *window,
		      const char   *action_name,
		      gboolean      sensitive)
{
	GtkAction *action;
	action = gtk_action_group_get_action (window->actions, action_name);
	g_object_set (action, "sensitive", sensitive, NULL);
}


static void
set_action_active (GThumbWindow *window,
		   char         *action_name,
		   gboolean      is_active)
{
	GtkAction *action;
	action = gtk_action_group_get_action (window->actions, action_name);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), is_active);
}


static void
set_action_active_if_different (GThumbWindow *window,
				char         *action_name,
				gboolean      is_active)
{
	GtkAction       *action;
	GtkToggleAction *toggle_action;

	action = gtk_action_group_get_action (window->actions, action_name);
	toggle_action = GTK_TOGGLE_ACTION (action);

	if (gtk_toggle_action_get_active (toggle_action) != is_active)
		gtk_toggle_action_set_active (toggle_action, is_active);
}


static void
set_action_important (GThumbWindow *window,
		      char         *action_name,
		      gboolean      is_important)
{
	GtkAction *action;
	action = gtk_ui_manager_get_action (window->ui, action_name);
	if (action != NULL) {
		g_object_set (action, "is_important", is_important, NULL);
		g_object_unref (action);
	}
}





static void window_update_zoom_sensitivity (GThumbWindow *window);


static void
window_update_statusbar_zoom_info (GThumbWindow *window)
{
	const char *path;
	gboolean    image_is_visible;
	int         zoom;
	char       *text;

	window_update_zoom_sensitivity (window);

	/**/

	path = window->image_path;

	image_is_visible = (path != NULL) && !window->image_error && ((window->sidebar_visible && window->image_pane_visible && window->preview_content == GTH_PREVIEW_CONTENT_IMAGE) || ! window->sidebar_visible);

	if (! image_is_visible) {
		if (! GTK_WIDGET_VISIBLE (window->zoom_info_frame))
			return;
		gtk_widget_hide (window->zoom_info_frame);
		return;
	} 

	if (! GTK_WIDGET_VISIBLE (window->zoom_info_frame)) 
		gtk_widget_show (window->zoom_info_frame);

	zoom = (int) (IMAGE_VIEWER (window->viewer)->zoom_level * 100.0);
	text = g_strdup_printf (" %d%% ", zoom);
	gtk_label_set_markup (GTK_LABEL (window->zoom_info), text);
	g_free (text);
}


static void
window_update_statusbar_image_info (GThumbWindow *window)
{
	char        *text;
	char         time_txt[50], *utf8_time_txt;
	char        *size_txt;
	char        *file_size_txt;
	const char  *path;
	int          width, height;
	time_t       timer = 0;
	struct tm   *tm;
	gdouble      sec;

	path = window->image_path;

	if ((path == NULL) || window->image_error) {
		if (! GTK_WIDGET_VISIBLE (window->image_info_frame))
			return;
		gtk_widget_hide (window->image_info_frame);
		return;

	} else if (! GTK_WIDGET_VISIBLE (window->image_info_frame)) 
		gtk_widget_show (window->image_info_frame);

	if (!image_viewer_is_void(IMAGE_VIEWER (window->viewer))) {
		width = image_viewer_get_image_width (IMAGE_VIEWER (window->viewer));
		height = image_viewer_get_image_height (IMAGE_VIEWER (window->viewer)); 
	} else {
		width = 0;
		height = 0;
	}

#ifdef HAVE_LIBEXIF
	timer = get_exif_time (path);
#endif
	if (timer == 0)
		timer = get_file_mtime (path);
	tm = localtime (&timer);
	strftime (time_txt, 50, _("%d %B %Y, %H:%M"), tm);
	utf8_time_txt = g_locale_to_utf8 (time_txt, -1, 0, 0, 0);
	sec = g_timer_elapsed (image_loader_get_timer (IMAGE_VIEWER (window->viewer)->loader),  NULL);

	size_txt = g_strdup_printf (_("%d x %d pixels"), width, height);
	file_size_txt = gnome_vfs_format_file_size_for_display (get_file_size (path));

	/**/

	if (! window->image_modified)
		text = g_strdup_printf (" %s - %s - %s     ",
					size_txt,
					file_size_txt,
					utf8_time_txt);
	else
		text = g_strdup_printf (" %s - %s     ", 
					_("Modified"),
					size_txt);

	gtk_label_set_markup (GTK_LABEL (window->image_info), text);

	/**/

	g_free (size_txt);
	g_free (file_size_txt);
	g_free (text);
	g_free (utf8_time_txt);
}


static void
update_image_comment (GThumbWindow *window)
{
	CommentData   *cdata;
	char          *comment;
	GtkTextBuffer *text_buffer;

	text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (window->image_comment));

	if (window->image_path == NULL) {
		GtkTextIter start, end;
		gtk_text_buffer_get_bounds (text_buffer, &start, &end);
		gtk_text_buffer_delete (text_buffer, &start, &end);
                return;
	}

	cdata = comments_load_comment (window->image_path);

	if (cdata == NULL) {
		GtkTextIter  iter;
		const char  *click_here = _("[Press 'c' to add a comment]");
		GtkTextIter  start, end;
		GtkTextTag  *tag;

		gtk_text_buffer_set_text (text_buffer, click_here, strlen (click_here));
		gtk_text_buffer_get_iter_at_line (text_buffer, &iter, 0);
		gtk_text_buffer_place_cursor (text_buffer, &iter);

		gtk_text_buffer_get_bounds (text_buffer, &start, &end);
		tag = gtk_text_buffer_create_tag (text_buffer, NULL, 
						  "style", PANGO_STYLE_ITALIC,
						  NULL);
		gtk_text_buffer_apply_tag (text_buffer, tag, &start, &end);

		return;
	}

	comment = comments_get_comment_as_string (cdata, "\n\n", " - ");

	if (comment != NULL) {
		GtkTextIter iter;
		gtk_text_buffer_set_text (text_buffer, comment, strlen (comment));
		gtk_text_buffer_get_iter_at_line (text_buffer, &iter, 0);
		gtk_text_buffer_place_cursor (text_buffer, &iter);
	} else {
		GtkTextIter start, end;
		gtk_text_buffer_get_bounds (text_buffer, &start, &end);
		gtk_text_buffer_delete (text_buffer, &start, &end);
	}

	g_free (comment);
	comment_data_free (cdata);
}


static void
window_update_image_info (GThumbWindow *window)
{
	window_update_statusbar_image_info (window);
	window_update_statusbar_zoom_info (window);
	gth_exif_data_viewer_update (GTH_EXIF_DATA_VIEWER (window->exif_data_viewer), 
				     IMAGE_VIEWER (window->viewer),
				     window->image_path);
	update_image_comment (window);
}


static void
window_update_infobar (GThumbWindow *window)
{
	char       *text;
	const char *path;
	char       *escaped_name;
	char       *utf8_name;
	int         images, current;

	path = window->image_path;
	if (path == NULL) {
		gthumb_info_bar_set_text (GTHUMB_INFO_BAR (window->info_bar), 
					  NULL, NULL);
		return;
	}

	images = gth_file_list_get_length (window->file_list);

	current = gth_file_list_pos_from_path (window->file_list, path) + 1;

	utf8_name = g_filename_to_utf8 (file_name_from_path (path), -1, 0, 0, 0);
	escaped_name = g_markup_escape_text (utf8_name, -1);

	text = g_strdup_printf ("%d/%d - <b>%s</b> %s", 
				current, 
				images, 
				escaped_name,
				window->image_modified ? _("[modified]") : "");

	gthumb_info_bar_set_text (GTHUMB_INFO_BAR (window->info_bar), 
				  text, 
				  NULL);

	g_free (utf8_name);
	g_free (escaped_name);
	g_free (text);
}


static void 
window_update_title (GThumbWindow *window)
{
	char *info_txt      = NULL;
	char *path;
	char *modified;

	g_return_if_fail (window != NULL);

	path = window->image_path;
	modified = window->image_modified ? _("[modified]") : "";

	if (path == NULL) {
		if ((window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		    && (window->dir_list->path != NULL)) {
			char *path = g_filename_to_utf8 (window->dir_list->path, -1, NULL, NULL, NULL);
			if (path == NULL)
				path = g_strdup (_("(Invalid Name)"));
			info_txt = g_strconcat (path, " ", modified, NULL);
			g_free (path);

		} else if ((window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST)
			   && (window->catalog_path != NULL)) {
			const char *cat_name;
			char       *cat_name_no_ext;

			cat_name = file_name_from_path (window->catalog_path);
			cat_name_no_ext = g_strdup (cat_name);
			
			/* Cut out the file extension. */
			cat_name_no_ext[strlen (cat_name_no_ext) - 4] = 0;
			
			info_txt = g_filename_to_utf8 (cat_name_no_ext, -1, 0, 0, 0);
			g_free (cat_name_no_ext);
		} else
			info_txt = g_strdup_printf ("%s", _("gThumb"));
	} else {
		const char *image_name = g_filename_to_utf8 (file_name_from_path (path), -1, 0, 0, 0);

		if (image_name == NULL)
			image_name = "";

		if (window->image_catalog != NULL) {
			char *cat_name = g_filename_to_utf8 (file_name_from_path (window->image_catalog), -1, 0, 0, 0);

			/* Cut out the file extension. */
			cat_name[strlen (cat_name) - 4] = 0;
			
			info_txt = g_strdup_printf ("%s %s - %s", image_name, modified, cat_name);
			g_free (cat_name);
		} else 
			info_txt = g_strdup_printf ("%s %s", image_name, modified);
	}

	gtk_window_set_title (GTK_WINDOW (window->app), info_txt);
	g_free (info_txt);
}


static void
window_update_statusbar_list_info (GThumbWindow *window)
{
	char             *info, *size_txt, *sel_size_txt;
	char             *total_info, *selected_info;
	int               tot_n, sel_n;
	GnomeVFSFileSize  tot_size, sel_size;
	GList            *scan;
	GList            *selection;

	tot_n = 0;
	tot_size = 0;

	for (scan = window->file_list->list; scan; scan = scan->next) {
		FileData *fd = scan->data;
		tot_n++;
		tot_size += fd->size;
	}

	sel_n = 0;
	sel_size = 0;
	selection = gth_file_list_get_selection_as_fd (window->file_list);

	for (scan = selection; scan; scan = scan->next) {
		FileData *fd = scan->data;
		sel_n++;
		sel_size += fd->size;
	}

	file_data_list_free (selection);

	size_txt = gnome_vfs_format_file_size_for_display (tot_size);
	sel_size_txt = gnome_vfs_format_file_size_for_display (sel_size);

	if (tot_n == 0)
		total_info = g_strdup (_("No image"));
	else if (tot_n == 1)
		total_info = g_strdup_printf (_("1 image (%s)"),
					      size_txt);
	else
		total_info = g_strdup_printf (_("%d images (%s)"),
					      tot_n,
					      size_txt); 

	if (sel_n == 0)
		selected_info = g_strdup (" ");
	else if (sel_n == 1)
		selected_info = g_strdup_printf (_("1 selected (%s)"), 
						 sel_size_txt);
	else
		selected_info = g_strdup_printf (_("%d selected (%s)"), 
						 sel_n, 
						 sel_size_txt);

	info = g_strconcat (total_info, 
			    ((sel_n == 0) ? NULL : ", "),
			    selected_info, 
			    NULL);

	gtk_statusbar_pop (GTK_STATUSBAR (window->statusbar), window->list_info_cid);
	gtk_statusbar_push (GTK_STATUSBAR (window->statusbar),
			    window->list_info_cid, info);

	g_free (total_info);
	g_free (selected_info);
	g_free (size_txt);
	g_free (sel_size_txt);
	g_free (info);
}


static void
window_update_go_sensitivity (GThumbWindow *window)
{
	gboolean sensitive;

	sensitive = (window->history_current != NULL) && (window->history_current->next != NULL);
	set_action_sensitive (window, "Go_Back", sensitive);
	gtk_widget_set_sensitive (window->go_back_toolbar_button, sensitive);

	sensitive = (window->history_current != NULL) && (window->history_current->prev != NULL);
	set_action_sensitive (window, "Go_Forward", sensitive);
	gtk_widget_set_sensitive (window->go_fwd_toolbar_button, sensitive);
}


static void
window_update_zoom_sensitivity (GThumbWindow *window)
{
	gboolean image_is_visible;
	gboolean image_is_void;
	gboolean fit;
	int      zoom;

	image_is_visible = (window->image_path != NULL) && ((window->sidebar_visible && window->image_pane_visible && window->preview_content == GTH_PREVIEW_CONTENT_IMAGE) || ! window->sidebar_visible);
	image_is_void = image_viewer_is_void (IMAGE_VIEWER (window->viewer));
	zoom = (int) (IMAGE_VIEWER (window->viewer)->zoom_level * 100.0);
	fit = image_viewer_is_zoom_to_fit (IMAGE_VIEWER (window->viewer)) || image_viewer_is_zoom_to_fit_if_larger (IMAGE_VIEWER (window->viewer));

	set_action_sensitive (window, 
			      "View_Zoom100",
			      image_is_visible && !image_is_void && (zoom != 100));
	set_action_sensitive (window, 
			      "View_ZoomIn",
			      image_is_visible && !image_is_void && (zoom != 10000));
	set_action_sensitive (window, 
			      "View_ZoomOut",
			      image_is_visible && !image_is_void && (zoom != 5));
	set_action_sensitive (window, 
			      "View_ZoomFit",
			      image_is_visible && !image_is_void);
}


static void
window_update_sensitivity (GThumbWindow *window)
{
	GtkTreeIter iter;
	int         sidebar_content = window->sidebar_content;
	gboolean    sel_not_null;
	gboolean    only_one_is_selected;
	gboolean    image_is_void;
	gboolean    image_is_ani;
	gboolean    playing;
	gboolean    viewing_dir;
	gboolean    viewing_catalog;
	gboolean    is_catalog;
	gboolean    is_search;
	gboolean    not_fullscreen;
	gboolean    image_is_visible;
	int         image_pos;

	sel_not_null = gth_file_view_selection_not_null (window->file_list->view);
	only_one_is_selected = gth_file_view_only_one_is_selected (window->file_list->view);
	image_is_void = image_viewer_is_void (IMAGE_VIEWER (window->viewer));
	image_is_ani = image_viewer_is_animation (IMAGE_VIEWER (window->viewer));
	playing = image_viewer_is_playing_animation (IMAGE_VIEWER (window->viewer));
	viewing_dir = sidebar_content == GTH_SIDEBAR_DIR_LIST;
	viewing_catalog = sidebar_content == GTH_SIDEBAR_CATALOG_LIST; 
	not_fullscreen = ! window->fullscreen;
	image_is_visible = ! image_is_void && ((window->sidebar_visible && window->image_pane_visible && (window->preview_content == GTH_PREVIEW_CONTENT_IMAGE)) || ! window->sidebar_visible);

	if (window->image_path != NULL)
		image_pos = gth_file_list_pos_from_path (window->file_list, window->image_path);
	else
		image_pos = -1;

	window_update_go_sensitivity (window);

	/* Image popup menu. */

	set_action_sensitive (window, "Image_OpenWith", ! image_is_void && not_fullscreen);
	set_action_sensitive (window, "Image_Rename", ! image_is_void && not_fullscreen);
	set_action_sensitive (window, "Image_Duplicate", ! image_is_void && not_fullscreen);
	set_action_sensitive (window, "Image_Delete", ! image_is_void && not_fullscreen);
	set_action_sensitive (window, "Image_Copy", ! image_is_void && not_fullscreen);
	set_action_sensitive (window, "Image_Move", ! image_is_void && not_fullscreen);

	/* File menu. */

	set_action_sensitive (window, "File_OpenWith", sel_not_null && not_fullscreen);

	set_action_sensitive (window, "File_Save", ! image_is_void);
	set_action_sensitive (window, "File_Revert", ! image_is_void && window->image_modified);
	set_action_sensitive (window, "File_Print", ! image_is_void || sel_not_null);

	/* Edit menu. */

	set_action_sensitive (window, "Edit_RenameFile", only_one_is_selected && not_fullscreen);
	set_action_sensitive (window, "Edit_DuplicateFile", sel_not_null && not_fullscreen);
	set_action_sensitive (window, "Edit_DeleteFiles", sel_not_null && not_fullscreen);
	set_action_sensitive (window, "Edit_CopyFiles", sel_not_null && not_fullscreen);
	set_action_sensitive (window, "Edit_MoveFiles", sel_not_null && not_fullscreen);

	set_action_sensitive (window, "AlterImage_Rotate90", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Rotate90CC", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Flip", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Mirror", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Desaturate", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Resize", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_ColorBalance", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_HueSaturation", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_BrightnessContrast", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Invert", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Posterize", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Equalize", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_AdjustLevels", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_StretchContrast", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Normalize", ! image_is_void && ! image_is_ani && image_is_visible);
	set_action_sensitive (window, "AlterImage_Crop", ! image_is_void && ! image_is_ani && image_is_visible);

	window_update_zoom_sensitivity(window);
	set_action_sensitive (window, "View_PlayAnimation", image_is_ani);
	set_action_sensitive (window, "View_StepAnimation", image_is_ani && ! playing);

	set_action_sensitive (window, "Edit_EditComment", sel_not_null);
	set_action_sensitive (window, "Edit_DeleteComment", sel_not_null);
	set_action_sensitive (window, "Edit_EditCategories", sel_not_null);

	set_action_sensitive (window, "Edit_AddToCatalog", sel_not_null);
	set_action_sensitive (window, "Edit_RemoveFromCatalog", viewing_catalog && sel_not_null);

	set_action_sensitive (window, "Go_ToContainer", not_fullscreen && viewing_catalog && only_one_is_selected);

	set_action_sensitive (window, "Go_Stop", 
			       ((window->activity_ref > 0) 
				|| window->setting_file_list
				|| window->changing_directory
				|| window->file_list->doing_thumbs));

	/* Edit Catalog menu. */

	if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST) { 
		char *view_catalog;
		char *view_search;

		is_catalog = (viewing_catalog && catalog_list_get_selected_iter (window->catalog_list, &iter));

		set_action_sensitive (window, "EditCatalog_Rename", is_catalog);
		set_action_sensitive (window, "EditCatalog_Delete", is_catalog);
		set_action_sensitive (window, "EditCatalog_Move", is_catalog && ! catalog_list_is_dir (window->catalog_list, &iter));
		
		is_search = (is_catalog && (catalog_list_is_search (window->catalog_list, &iter)));
		set_action_sensitive (window, "EditCatalog_EditSearch", is_search);
		set_action_sensitive (window, "EditCatalog_RedoSearch", is_search);

		/**/

		is_catalog = (window->catalog_path != NULL) && (viewing_catalog && catalog_list_get_iter_from_path (window->catalog_list, window->catalog_path, &iter));

		set_action_sensitive (window, "EditCurrentCatalog_Rename", is_catalog);
		set_action_sensitive (window, "EditCurrentCatalog_Delete", is_catalog);
		set_action_sensitive (window, "EditCurrentCatalog_Move", is_catalog && ! catalog_list_is_dir (window->catalog_list, &iter));
		
		is_search = (is_catalog && (catalog_list_is_search (window->catalog_list, &iter)));
		set_action_sensitive (window, "EditCurrentCatalog_EditSearch", is_search);
		set_action_sensitive (window, "EditCurrentCatalog_RedoSearch", is_search);

		if (is_search) {
			view_catalog = "1";
			view_search = "0";
		} else {
			view_catalog = "0";
			view_search = "1";
		}
	} 

	/* View menu. */

	set_action_sensitive (window, "View_ShowImage", window->image_path != NULL);
	set_action_sensitive (window, "View_ImageProp", window->image_path != NULL);
	set_action_sensitive (window, "View_Fullscreen", window->image_path != NULL);
	set_action_sensitive (window, "View_ShowPreview", window->sidebar_visible);
	set_action_sensitive (window, "View_ShowInfo", ! window->sidebar_visible);
	set_action_sensitive (window, "View_PrevImage", image_pos > 0);
	set_action_sensitive (window, "View_NextImage", (image_pos != -1) && (image_pos < gth_file_view_get_images (window->file_list->view) - 1));

	/* Tools menu. */

	set_action_sensitive (window, "Tools_Slideshow", window->file_list->list != NULL);
	set_action_sensitive (window, "Tools_IndexImage", sel_not_null);
	set_action_sensitive (window, "Tools_WebExporter", sel_not_null);
	set_action_sensitive (window, "Tools_RenameSeries", sel_not_null);
	set_action_sensitive (window, "Tools_ConvertFormat", sel_not_null);
	set_action_sensitive (window, "Tools_ChangeDate", sel_not_null);
	set_action_sensitive (window, "Tools_JPEGRotate", sel_not_null);
	set_action_sensitive (window, "Wallpaper_Centered", ! image_is_void);
	set_action_sensitive (window, "Wallpaper_Tiled", ! image_is_void);
	set_action_sensitive (window, "Wallpaper_Scaled", ! image_is_void);
	set_action_sensitive (window, "Wallpaper_Stretched", ! image_is_void);

	set_action_sensitive (window, "Tools_JPEGRotate", sel_not_null);

	window_update_zoom_sensitivity (window);
}


static void
window_progress (gfloat   percent, 
		 gpointer data)
{
	GThumbWindow *window = data;

	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->progress), 
				       percent);

	if (percent == 0.0) 
		set_action_sensitive (window, "Go_Stop", 
				       ((window->activity_ref > 0) 
					|| window->setting_file_list
					|| window->changing_directory
					|| window->file_list->doing_thumbs));
}


static gboolean
load_progress (gpointer data)
{
	GThumbWindow *window = data;
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (window->progress));
	return TRUE;
}


static void
window_start_activity_mode (GThumbWindow *window)
{
	g_return_if_fail (window != NULL);

	if (window->activity_ref++ > 0)
		return;

	gtk_widget_show (window->progress);

	window->activity_timeout = g_timeout_add (ACTIVITY_DELAY, 
						  load_progress, 
						  window);
}


static void
window_stop_activity_mode (GThumbWindow *window)
{
	g_return_if_fail (window != NULL);

	if (--window->activity_ref > 0)
		return;

	gtk_widget_hide (window->progress);

	if (window->activity_timeout == 0)
		return;

	g_source_remove (window->activity_timeout);
	window->activity_timeout = 0;
	window_progress (0.0, window);
}


/* -- set file list -- */


typedef struct {
	GThumbWindow *window;
	DoneFunc      done_func;
	gpointer      done_func_data;
} WindowSetListData;


static gboolean
set_file_list__final_step_cb (gpointer data)
{
	GThumbWindow *window = data;

	if (StartInFullscreen) {
		StartInFullscreen = FALSE;
		fullscreen_start (fullscreen, window);
	}

	if (StartSlideshow) {
		StartSlideshow = FALSE;
		window_start_slideshow (window);
	} 

	if (HideSidebar) {
		HideSidebar = FALSE;
		window_hide_sidebar (window);
	}

	if (ImageToDisplay != NULL) {
		int pos = gth_file_list_pos_from_path (window->file_list, ImageToDisplay);
		if (pos != -1)
			gth_file_list_select_image_by_pos (window->file_list, pos);
		g_free (ImageToDisplay);
		ImageToDisplay = NULL;

	} else if (ViewFirstImage) {
		ViewFirstImage = FALSE;
		window_show_first_image (window, FALSE);
	}

	if (FirstStart)
		FirstStart = FALSE;

	return FALSE;
}


static void
window_set_file_list_continue (gpointer callback_data)
{
	WindowSetListData *data = callback_data;
	GThumbWindow      *window = data->window;

	window_stop_activity_mode (window);
	window_update_statusbar_list_info (window);
	window->setting_file_list = FALSE;

	window_update_sensitivity (window);

	if (data->done_func != NULL)
		(*data->done_func) (data->done_func_data);
	g_free (data);

	g_idle_add (set_file_list__final_step_cb, window);
}


typedef struct {
	WindowSetListData *wsl_data;
	GList             *list;
	GThumbWindow      *window;
} SetListInterruptedData; 


static void
set_list_interrupted_cb (gpointer callback_data)
{
	SetListInterruptedData *sli_data = callback_data;

	sli_data->window->setting_file_list = TRUE;
	gth_file_list_set_list (sli_data->window->file_list, 
				sli_data->list, 
				window_set_file_list_continue, 
				sli_data->wsl_data);

	sli_data->window->can_set_file_list = TRUE;
	
	g_list_foreach (sli_data->list, (GFunc) g_free, NULL);
	g_list_free (sli_data->list);
	g_free (sli_data);
}


static void
window_set_file_list (GThumbWindow *window, 
		      GList        *list,
		      DoneFunc      done_func,
		      gpointer      done_func_data)
{
	WindowSetListData *data;

	if (! window->can_set_file_list)
		return;

	window->can_set_file_list = FALSE;

	if (window->slideshow)
		window_stop_slideshow (window);

	data = g_new (WindowSetListData, 1);
	data->window = window;
	data->done_func = done_func;
	data->done_func_data = done_func_data;

	if (window->setting_file_list) {
		SetListInterruptedData *sli_data;
		GList                  *scan;

		sli_data = g_new (SetListInterruptedData, 1);

		sli_data->wsl_data = data;
		sli_data->window = window;

		sli_data->list = NULL;
		for (scan = list; scan; scan = scan->next) {
			char *path = g_strdup ((char*)(scan->data));
			sli_data->list = g_list_prepend (sli_data->list, path);
		}

		gth_file_list_interrupt_set_list (window->file_list,
						  set_list_interrupted_cb,
						  sli_data);
		return;
	}

	window->setting_file_list = TRUE;
	window_start_activity_mode (window);
	window_update_sensitivity (window);

	gth_file_list_set_list (window->file_list, list, 
				window_set_file_list_continue, data);

	window->can_set_file_list = TRUE;
}





void
window_update_file_list (GThumbWindow *window)
{
	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		window_go_to_directory (window, window->dir_list->path);

	else if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST) {
		char *catalog_path;

		catalog_path = catalog_list_get_selected_path (window->catalog_list);
		if (catalog_path == NULL)
			return;

		window_go_to_catalog (window, catalog_path);
		g_free (catalog_path);
	}
}


void
window_update_catalog_list (GThumbWindow *window)
{
	char *catalog_dir;
	char *base_dir;

	if (window->sidebar_content != GTH_SIDEBAR_CATALOG_LIST) 
		return;

	/* If the catalog still exists, show the directory it belongs to. */

	if ((window->catalog_path != NULL) 
	    && path_is_file (window->catalog_path)) {
		GtkTreeIter  iter;
		GtkTreePath *path;

		catalog_dir = remove_level_from_path (window->catalog_path);
		window_go_to_catalog_directory (window, catalog_dir);
		g_free (catalog_dir);

		if (! catalog_list_get_iter_from_path (window->catalog_list,
						       window->catalog_path, 
						       &iter))
			return;

		/* Select (without updating the file list) and view 
		 * the catalog. */

		catalog_list_select_iter (window->catalog_list, &iter);
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (window->catalog_list->list_store), &iter);
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (window->catalog_list->list_view),
					      path,
					      NULL,
					      TRUE,
					      0.5,
					      0.0);
		gtk_tree_path_free (path);
		return;
	} 

	/* No catalog selected. */

	if (window->catalog_path != NULL) {
		g_free (window->catalog_path);
		window->catalog_path = NULL;

		/* Update file list. */

		window_set_file_list (window, NULL, NULL, NULL);
	}

	g_return_if_fail (window->catalog_list->path != NULL);

	/* If directory exists then update. */

	if (path_is_dir (window->catalog_list->path)) {
		catalog_list_refresh (window->catalog_list);
		return;
	}

	/* Else go up one level until a directory exists. */

	base_dir = g_strconcat (g_get_home_dir(),
				"/",
				RC_CATALOG_DIR,
				NULL);
	catalog_dir = g_strdup (window->catalog_list->path);
	
	while ((strcmp (base_dir, catalog_dir) != 0)
	       && ! path_is_dir (catalog_dir)) {
		char *new_dir;
		
		new_dir = remove_level_from_path (catalog_dir);
		g_free (catalog_dir);
		catalog_dir = new_dir;
	}

	window_go_to_catalog_directory (window, catalog_dir);
	
	g_free (catalog_dir);
	g_free (base_dir);
}


/* -- bookmarks & history -- */


static void
activate_action_bookmark (GtkAction *action, 
			  gpointer   data)
{
	GThumbWindow *window = data;
	const char   *path;
	const char   *no_prefix_path;

	path = g_object_get_data (G_OBJECT (action), "path");

	no_prefix_path = pref_util_remove_prefix (path);
	
	if (pref_util_location_is_catalog (path) 
	    || pref_util_location_is_search (path)) 
		window_go_to_catalog (window, no_prefix_path);
	else 
		window_go_to_directory (window, no_prefix_path);	
}


static void
add_bookmark_menu_item (GThumbWindow   *window,
			GtkActionGroup *actions,
			uint            merge_id,
			Bookmarks      *bookmarks,
			char           *prefix,
			int             id,
			const char     *base_path,
			const char     *path)
{
	GtkAction    *action;
	char         *name;
	char         *label;
	char         *menu_name;
	char         *e_label;
	char         *e_tip;
	char         *utf8_s;
	const char   *stock_id;

	name = g_strdup_printf ("%s%d", prefix, id);

	menu_name = escape_underscore (bookmarks_get_menu_name (bookmarks, path));
	if (menu_name == NULL)
		menu_name = g_strdup (_("(Invalid Name)"));

	label = _g_strdup_with_max_size (menu_name, BOOKMARKS_MENU_MAX_LENGTH);
	utf8_s = g_locale_to_utf8 (label, -1, NULL, NULL, NULL);
	g_free (label);
	if (utf8_s == NULL)
		utf8_s = g_strdup (_("(Invalid Name)"));
	e_label = g_markup_escape_text (utf8_s, -1);
	g_free (utf8_s);

	utf8_s = g_locale_to_utf8 (bookmarks_get_menu_tip (bookmarks, path), -1, NULL, NULL, NULL);
	if (utf8_s == NULL)
		utf8_s = g_strdup (_("(Invalid Name)"));
	e_tip = g_markup_escape_text (utf8_s, -1);
	g_free (utf8_s);

	if (strcmp (menu_name, g_get_home_dir ()) == 0) 
		stock_id = GTK_STOCK_HOME;
	else if (folder_is_film (path))
		stock_id = GTHUMB_STOCK_FILM;
	else if (pref_util_location_is_catalog (path)) 
		stock_id = GTHUMB_STOCK_CATALOG;
	else if (pref_util_location_is_search (path))	
		stock_id = GTHUMB_STOCK_SEARCH;
	else
		stock_id = GTK_STOCK_OPEN;
	g_free (menu_name);

	/*
	else if (pref_util_location_is_catalog (path)) 
		pixbuf = gdk_pixbuf_new_from_inline (-1, catalog_16_rgba, FALSE, NULL);
	else if (pref_util_location_is_search (path))
		pixbuf = gdk_pixbuf_new_from_inline (-1, catalog_search_16_rgba, FALSE, NULL);
	else if (folder_is_film (path))
		pixbuf = gtk_widget_render_icon (window->app, GTHUMB_STOCK_FILM, GTK_ICON_SIZE_MENU, NULL);
	else
		pixbuf = get_folder_pixbuf (get_folder_pixbuf_size_for_menu (window->app));
	*/

	action = g_object_new (GTK_TYPE_ACTION,
			       "name", name,
			       "label", e_label,
			       "stock_id", stock_id,
			       NULL);
	g_free (e_label);

	if (e_tip != NULL) {
		g_object_set (action, "tooltip", e_tip, NULL);
		g_free (e_tip);
	}

	g_object_set_data_full (G_OBJECT (action), "path", g_strdup (path), (GFreeFunc)g_free);
	g_signal_connect (action, "activate", 
			  G_CALLBACK (activate_action_bookmark), 
			  window);

	gtk_action_group_add_action (actions, action);
	g_object_unref (action);

	gtk_ui_manager_add_ui (window->ui, 
			       merge_id, 
			       base_path,
			       name,
			       name,
			       GTK_UI_MANAGER_AUTO, 
			       FALSE);
	
	g_free (name);
}


void
window_update_bookmark_list (GThumbWindow *window)
{
	GList *scan, *names;
	int    i;

	/* Delete bookmarks menu. */

	if (window->bookmarks_merge_id != 0) {
		gtk_ui_manager_remove_ui (window->ui, window->bookmarks_merge_id);
		window->bookmarks_merge_id = 0;
	}

	gtk_ui_manager_ensure_update (window->ui);

	if (window->bookmark_actions != NULL) {
		gtk_ui_manager_remove_action_group (window->ui, window->bookmark_actions);
		g_object_unref (window->bookmark_actions);
		window->bookmark_actions = NULL;
	}

	/* Load and sort bookmarks */

	bookmarks_load_from_disk (preferences.bookmarks);
	if (preferences.bookmarks->list == NULL)
		return;

	names = g_list_copy (preferences.bookmarks->list);

	/* Update bookmarks menu. */

	if (window->bookmark_actions == NULL) {
		window->bookmark_actions = gtk_action_group_new ("BookmarkActions");
		gtk_ui_manager_insert_action_group (window->ui, window->bookmark_actions, 0);
	}

	if (window->bookmarks_merge_id == 0)
		window->bookmarks_merge_id = gtk_ui_manager_new_merge_id (window->ui);

	for (i = 0, scan = names; scan; scan = scan->next) {
		add_bookmark_menu_item (window,
					window->bookmark_actions,
					window->bookmarks_merge_id,
					preferences.bookmarks,
					"Bookmark",
					i++,
					"/MenuBar/Bookmarks/BookmarkList",
					scan->data);
	}

	window->bookmarks_length = i;

	g_list_free (names);

	/*gtk_ui_manager_ensure_update (window->ui);*/

	if (window->bookmarks_dlg != NULL)
		dlg_edit_bookmarks_update (window->bookmarks_dlg);
}


static void
window_update_history_list (GThumbWindow *window)
{
	GList *scan;
	int    i;

	window_update_go_sensitivity (window);

	/* Delete bookmarks menu. */

	if (window->history_merge_id != 0) {
		gtk_ui_manager_remove_ui (window->ui, window->history_merge_id);
		window->history_merge_id = 0;
	}

	gtk_ui_manager_ensure_update (window->ui);

	if (window->history_actions != NULL) {
		gtk_ui_manager_remove_action_group (window->ui, window->history_actions);
		g_object_unref (window->history_actions);
		window->history_actions = NULL;
	}

	/* Update history menu. */

	if (window->history->list == NULL)
		return;

	if (window->history_actions == NULL) {
		window->history_actions = gtk_action_group_new ("HistoryActions");
		gtk_ui_manager_insert_action_group (window->ui, window->history_actions, 0);
	}

	if (window->history_merge_id == 0)
		window->history_merge_id = gtk_ui_manager_new_merge_id (window->ui);

	for (i = 0, scan = window->history_current; scan && (i < eel_gconf_get_integer (PREF_MAX_HISTORY_LENGTH, 15)); scan = scan->next) {
		add_bookmark_menu_item (window,
					window->history_actions,
					window->history_merge_id,
					window->history,
					"History",
					i,
					HISTORY_LIST_POPUP,
					scan->data);
		i++;
	}
	window->history_length = i;

	/*gtk_ui_manager_ensure_update (window->ui);*/
}


/**/

static void window_make_current_image_visible (GThumbWindow *window);


static void
view_image_at_pos (GThumbWindow *window, 
		   int           pos)
{
	char *path;

	path = gth_file_list_path_from_pos (window->file_list, pos);
	if (path == NULL) 
		return;
	window_load_image (window, path);
	g_free (path);
}


/* -- activity -- */


static void
image_loader_progress_cb (ImageLoader *loader,
			  float        p, 
			  gpointer     data)
{
	GThumbWindow *window = data;
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->progress), p);
}


static void
image_loader_done_cb (ImageLoader *loader,
		      gpointer     data)
{
	GThumbWindow *window = data;
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->progress), 0.0);
}


static char *
get_command_name_from_sidebar_content (GThumbWindow *window)
{
	switch (window->sidebar_content) {
	case GTH_SIDEBAR_DIR_LIST:
		return "View_ShowFolders";
	case GTH_SIDEBAR_CATALOG_LIST:
		return "View_ShowCatalogs";
	default:
		return NULL;
	}

	return NULL;
}


static void
set_button_active_no_notify (GThumbWindow *window,
			     GtkWidget    *button,
			     gboolean      active)
{
	if (button == NULL)
		return;
	g_signal_handlers_block_by_data (button, window);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), active);
	g_signal_handlers_unblock_by_data (button, window);
}


static GtkWidget *
get_button_from_sidebar_content (GThumbWindow *window)
{
	switch (window->sidebar_content) {
	case GTH_SIDEBAR_DIR_LIST:
		return window->show_folders_toolbar_button;
	case GTH_SIDEBAR_CATALOG_LIST:
		return window->show_catalog_toolbar_button;
	default:
		return NULL;
	}

	return NULL;
}


static void
_window_set_sidebar (GThumbWindow *window,
		     int           sidebar_content)
{
	char   *cname;
	GError *error = NULL;

	cname = get_command_name_from_sidebar_content (window);
	if (cname != NULL) 
		set_action_active_if_different (window, cname, FALSE);
	set_button_active_no_notify (window,
				     get_button_from_sidebar_content (window),
				     FALSE);

	window->sidebar_content = sidebar_content;

	set_button_active_no_notify (window,
				     get_button_from_sidebar_content (window),
				     TRUE);

	cname = get_command_name_from_sidebar_content (window);
	if ((cname != NULL) && window->sidebar_visible)
		set_action_active_if_different (window, cname, TRUE);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (window->notebook), 
				       sidebar_content - 1);

	window_update_sensitivity (window);

	/**/

	if (window->sidebar_merge_id != 0)
		gtk_ui_manager_remove_ui (window->ui, window->sidebar_merge_id);
	gtk_ui_manager_ensure_update (window->ui);

	if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST) { 
		gboolean    is_catalog;
		gboolean    is_search;
		GtkTreeIter iter;
		
		is_catalog = (window->catalog_path != NULL) && (catalog_list_get_iter_from_path (window->catalog_list, window->catalog_path, &iter));
		is_search = (is_catalog && (catalog_list_is_search (window->catalog_list, &iter)));
		if (is_search)
			window->sidebar_merge_id = gtk_ui_manager_add_ui_from_string (window->ui, search_ui_info, -1, &error);
		else
			window->sidebar_merge_id = gtk_ui_manager_add_ui_from_string (window->ui, catalog_ui_info, -1, &error);
	} else 
		window->sidebar_merge_id = gtk_ui_manager_add_ui_from_string (window->ui, folder_ui_info, -1, &error);

	gtk_ui_manager_ensure_update (window->ui);
	gtk_widget_queue_resize (window->app);
}


static void
make_image_visible (GThumbWindow *window, 
		    int           pos)
{
	GthVisibility  visibility;

	if ((pos < 0) || (pos >= gth_file_view_get_images (window->file_list->view)))
		return;

	visibility = gth_file_view_image_is_visible (window->file_list->view, pos);
	if (visibility != GTH_VISIBILITY_FULL) {
		double offset = 0.5;
		
		switch (visibility) {
		case GTH_VISIBILITY_NONE:
			offset = 0.5; 
			break;
		case GTH_VISIBILITY_PARTIAL_TOP:
			offset = 0.0; 
			break;
		case GTH_VISIBILITY_PARTIAL_BOTTOM:
			offset = 1.0; 
			break;
		case GTH_VISIBILITY_PARTIAL:
		case GTH_VISIBILITY_FULL:
			offset = -1.0;
			break;
		}
		if (offset > -1.0)
			gth_file_view_moveto (window->file_list->view, pos, offset);
	}
}


/* -- window_save_pixbuf -- */


void
save_pixbuf__image_saved_step2 (gpointer data)
{
	GThumbWindow *window = data;
	int pos;

	if (window->image_path == NULL)
		return;

	window->image_modified = FALSE;
	window->saving_modified_image = FALSE;

	pos = gth_file_list_pos_from_path (window->file_list, 
					   window->image_path);
	if (pos != -1) {
		view_image_at_pos (window, pos);
		gth_file_list_select_image_by_pos (window->file_list, pos);
	}
}


static void
save_pixbuf__image_saved_cb (char     *filename,
			     gpointer  data)
{
	GThumbWindow *window = data;
	GList        *file_list;
	int           pos;

	if (filename == NULL) 
		return;

#ifdef HAVE_LIBEXIF
	if (window->exif_data != NULL) {
		JPEGData *jdata;

		jdata = jpeg_data_new_from_file (filename);
		if (jdata != NULL) {
			jpeg_data_set_exif_data (jdata, window->exif_data);
			jpeg_data_save_file (jdata, filename);
			jpeg_data_unref (jdata);
		}
	}
#endif /* HAVE_LIBEXIF */

	/**/

	if ((window->image_path != NULL)
	    && (strcmp (window->image_path, filename) == 0))
		window->image_modified = FALSE;

	file_list = g_list_prepend (NULL, filename);

	pos = gth_file_list_pos_from_path (window->file_list, filename);
	if (pos == -1) {
		char *destination = remove_level_from_path (filename);

		if ((window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		    && (window->dir_list->path != NULL) 
		    && (strcmp (window->dir_list->path, destination) == 0)) {
			g_free (window->image_path);
			window->image_path = g_strdup (filename);
			gth_file_list_add_list (window->file_list, 
						file_list, 
						save_pixbuf__image_saved_step2,
						window);
		} else
			save_pixbuf__image_saved_step2 (window);
		g_free (destination);

		all_windows_notify_files_created (file_list);

	} else {
		view_image_at_pos (window, pos);
		all_windows_notify_files_changed (file_list);
	}

	g_list_free (file_list);
}


void
window_save_pixbuf (GThumbWindow *window,
		    GdkPixbuf    *pixbuf)
{
	char *current_folder = NULL;

	if (window->image_path != NULL)
		current_folder = g_strdup (window->image_path);

	else if (window->dir_list->path != NULL)
		current_folder = g_strconcat (window->dir_list->path,
					      "/", 
					      NULL);

	dlg_save_image (GTK_WINDOW (window->app), 
			current_folder,
			pixbuf,
			save_pixbuf__image_saved_cb,
			window);

	g_free (current_folder);
}


static void
ask_whether_to_save__image_saved_cb (char     *filename,
				     gpointer  data)
{
	GThumbWindow *window = data;

	save_pixbuf__image_saved_cb (filename, data);

	if (window->image_saved_func != NULL)
		(*window->image_saved_func) (NULL, window);
}


static void
ask_whether_to_save__response_cb (GtkWidget    *dialog,
				  int           response_id,
				  GThumbWindow *window)
{
        gtk_widget_destroy (dialog);
	
        if (response_id == GTK_RESPONSE_YES) {
		dlg_save_image (GTK_WINDOW (window->app),
				window->image_path,
				image_viewer_get_current_pixbuf (IMAGE_VIEWER (window->viewer)),
				ask_whether_to_save__image_saved_cb,
				window);
		window->saving_modified_image = TRUE;

	} else {
		window->saving_modified_image = FALSE;
		window->image_modified = FALSE;
		if (window->image_saved_func != NULL)
			(*window->image_saved_func) (NULL, window);
	}
}


static gboolean
ask_whether_to_save (GThumbWindow   *window,
		     ImageSavedFunc  image_saved_func)
{
	GtkWidget *d;

	if (! eel_gconf_get_boolean (PREF_MSG_SAVE_MODIFIED_IMAGE, TRUE)) 
		return FALSE;
		
	d = _gtk_yesno_dialog_with_checkbutton_new (
			    GTK_WINDOW (window->app),
			    GTK_DIALOG_MODAL,
			    _("The current image has been modified, do you want to save it?"),
			    _("Do Not Save"),
			    GTK_STOCK_SAVE_AS,
			    _("_Do not display this message again"),
			    PREF_MSG_SAVE_MODIFIED_IMAGE);

	window->saving_modified_image = TRUE;
	window->image_saved_func = image_saved_func;
	g_signal_connect (G_OBJECT (d), "response",
			  G_CALLBACK (ask_whether_to_save__response_cb),
			  window);

	gtk_widget_show (d);

	return TRUE;
}


static void
real_set_void (char     *filename,
	       gpointer  data)
{
	GThumbWindow *window = data;

	if (!window->image_error) {
		g_free (window->image_path);
		window->image_path = NULL;
		window->image_mtime = 0;
		window->image_modified = FALSE;
	}


	image_viewer_set_void (IMAGE_VIEWER (window->viewer));

	window_update_statusbar_image_info (window);
 	window_update_image_info (window);
	window_update_title (window);
	window_update_infobar (window);
	window_update_sensitivity (window);

	if (window->image_prop_dlg != NULL)
		dlg_image_prop_update (window->image_prop_dlg);
}


static void
window_image_viewer_set_void (GThumbWindow *window)
{
	window->image_error = FALSE;
	if (window->image_modified)
		if (ask_whether_to_save (window, real_set_void))
			return;
	real_set_void (NULL, window);
}


static void
window_image_viewer_set_error (GThumbWindow *window)
{
	window->image_error = TRUE;
	if (window->image_modified)
		if (ask_whether_to_save (window, real_set_void))
			return;
	real_set_void (NULL, window);
}


static void
window_make_current_image_visible (GThumbWindow *window)
{
	char *path;
	int   pos;

	if (window->setting_file_list || window->changing_directory)
		return;

	path = image_viewer_get_image_filename (IMAGE_VIEWER (window->viewer));
	if (path == NULL)
		return;

	pos = gth_file_list_pos_from_path (window->file_list, path);
	g_free (path);

	if (pos == -1) 
		window_image_viewer_set_void (window);
	else
		make_image_visible (window, pos);
}



/* -- callbacks -- */


static void
close_window_cb (GtkWidget    *caller, 
		 GdkEvent     *event, 
		 GThumbWindow *window)
{
	window_close (window);
}


static gboolean
sel_change_update_cb (gpointer data)
{
	GThumbWindow *window = data;

	g_source_remove (window->sel_change_timeout);
	window->sel_change_timeout = 0;

	window_update_sensitivity (window);
	window_update_statusbar_list_info (window);

	return FALSE;
}


static int
file_selection_changed_cb (GtkWidget *widget, 
			   gpointer   data)
{
	GThumbWindow *window = data;

	if (window->sel_change_timeout != 0)
		g_source_remove (window->sel_change_timeout);

	window->sel_change_timeout = g_timeout_add (SEL_CHANGED_DELAY,
						    sel_change_update_cb,
						    window);

	if (window->comment_dlg != NULL)
		dlg_comment_update (window->comment_dlg);

	if (window->categories_dlg != NULL)
		dlg_categories_update (window->categories_dlg);


	return TRUE;
}


static void
gth_file_list_cursor_changed_cb (GtkWidget *widget,
				 int        pos,
				 gpointer   data)
{
	GThumbWindow *window = data;	
	char         *focused_image;

	if (window->setting_file_list || window->changing_directory) 
		return;

	focused_image = gth_file_list_path_from_pos (window->file_list, pos);

	if (focused_image == NULL)
		return;

	if ((window->image_path == NULL) 
	    || (strcmp (focused_image, window->image_path) != 0))
		view_image_at_pos (window, pos);

	g_free (focused_image);
}


static int
gth_file_list_button_press_cb (GtkWidget      *widget, 
			       GdkEventButton *event,
			       gpointer        data)
{
	GThumbWindow *window = data;

	if (event->type == GDK_3BUTTON_PRESS) 
		return FALSE;

	if ((event->button != 1) && (event->button != 3))
		return FALSE;

	if ((event->state & GDK_SHIFT_MASK)
	    || (event->state & GDK_CONTROL_MASK))
		return FALSE;

	if (event->button == 1) {
		int pos;

		pos = gth_file_view_get_image_at (window->file_list->view, event->x, event->y);

		if (pos == -1)
			return FALSE;

		if (event->type == GDK_2BUTTON_PRESS) 
			return FALSE;

		if (event->type == GDK_BUTTON_PRESS) {
			make_image_visible (window, pos);
			view_image_at_pos (window, pos);
			return FALSE;
		}

	} else if (event->button == 3) {
		int  pos;

		pos = gth_file_view_get_image_at (window->file_list->view, event->x, event->y);

		if (pos != -1) {
			if (! gth_file_list_is_selected (window->file_list, pos))
				gth_file_list_select_image_by_pos (window->file_list, pos);
		} else
			gth_file_list_unselect_all (window->file_list);

		window_update_sensitivity (window);

		gtk_menu_popup (GTK_MENU (window->file_popup_menu),
				NULL,                                   
				NULL,                                   
				NULL,
				NULL,
				3,                               
				event->time);

		return TRUE;
	}

	return FALSE;
}


static gboolean 
hide_sidebar_idle (gpointer data) 
{
	GThumbWindow *window = data;
	window_hide_sidebar (window);
	return FALSE;
}


static int 
gth_file_list_item_activated_cb (GtkWidget  *widget, 
				 int         idx,
				 gpointer    data)
{
	GThumbWindow *window = data;

	/* use a timeout to avoid that the viewer gets
	 * the button press event. */
	g_timeout_add (HIDE_SIDEBAR_DELAY, hide_sidebar_idle, window);
	
	return TRUE;
}


static void
dir_list_activated_cb (GtkTreeView       *tree_view,
		       GtkTreePath       *path,
		       GtkTreeViewColumn *column,
		       gpointer           data)
{
	GThumbWindow *window = data;
	char         *new_dir;

	new_dir = dir_list_get_path_from_tree_path (window->dir_list, path);
	window_go_to_directory (window, new_dir);
	g_free (new_dir);
}


/**/


static int
dir_list_button_press_cb (GtkWidget      *widget,
			  GdkEventButton *event,
			  gpointer        data)
{
	GThumbWindow *window     = data;
	GtkWidget    *treeview   = window->dir_list->list_view;
	GtkListStore *list_store = window->dir_list->list_store;
	GtkTreePath  *path;
	GtkTreeIter   iter;

	if (dir_list_tree_path != NULL) {
		gtk_tree_path_free (dir_list_tree_path);
		dir_list_tree_path = NULL;
	}

	if ((event->state & GDK_SHIFT_MASK) 
	    || (event->state & GDK_CONTROL_MASK))
		return FALSE;

	if ((event->button != 1) & (event->button != 3))
		return FALSE;

	if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview),
					     event->x, event->y,
					     &path, NULL, NULL, NULL))
		return FALSE;
	
	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (list_store), 
				       &iter, 
				       path)) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	dir_list_tree_path = gtk_tree_path_copy (path);
	gtk_tree_path_free (path);

	return FALSE;
}


static int
dir_list_button_release_cb (GtkWidget      *widget,
			    GdkEventButton *event,
			    gpointer        data)
{
	GThumbWindow *window     = data;
	GtkWidget    *treeview   = window->dir_list->list_view;
	GtkListStore *list_store = window->dir_list->list_store;
	GtkTreePath  *path;
	GtkTreeIter   iter;

	if ((event->state & GDK_SHIFT_MASK) 
	    || (event->state & GDK_CONTROL_MASK))
		return FALSE;

	if ((event->button != 1) & (event->button != 3))
		return FALSE;

	if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview),
					     event->x, event->y,
					     &path, NULL, NULL, NULL)) {
		if (event->button != 3)
			return FALSE;
		
		window_update_sensitivity (window);
		gtk_menu_popup (GTK_MENU (window->dir_list_popup_menu),
				NULL,
				NULL,
				NULL,
				NULL,
				3,
				event->time);

		return TRUE;
	}
	
	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (list_store), 
				       &iter, 
				       path)) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	if ((dir_list_tree_path == NULL)
	    || gtk_tree_path_compare (dir_list_tree_path, path) != 0) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	gtk_tree_path_free (path);

	if ((event->button == 1) 
	    && (pref_get_real_click_policy () == GTH_CLICK_POLICY_SINGLE)) {
		char *new_dir;

		new_dir = dir_list_get_path_from_iter (window->dir_list, 
						       &iter);
		window_go_to_directory (window, new_dir);
		g_free (new_dir);

		return FALSE;

	} else if (event->button == 3) {
		GtkTreeSelection *selection;
		char             *utf8_name;
		char             *name;

		utf8_name = dir_list_get_name_from_iter (window->dir_list, &iter);
		name = g_filename_from_utf8 (utf8_name, -1, NULL, NULL, NULL);
		g_free (utf8_name);

		if (strcmp (name, "..") == 0) {
			g_free (name);
			return TRUE;
		}
		g_free (name);

		/* Update selection. */

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->dir_list->list_view));
		if (selection == NULL)
			return FALSE;

		if (! gtk_tree_selection_iter_is_selected (selection, &iter)) {
			gtk_tree_selection_unselect_all (selection);
			gtk_tree_selection_select_iter (selection, &iter);
                }

		/* Popup menu. */

		window_update_sensitivity (window);
		gtk_menu_popup (GTK_MENU (window->dir_popup_menu),
				NULL,
				NULL,
				NULL,
				NULL,
				3,
				event->time);
		return TRUE;
	}

	return FALSE;
}


/* directory or catalog list selection changed. */

static void
dir_or_catalog_sel_changed_cb (GtkTreeSelection *selection,
			       gpointer          p)
{
	GThumbWindow *window = p;
	window_update_sensitivity (window);
}


static void
add_history_item (GThumbWindow *window,
		  const char   *path,
		  const char   *prefix)
{
	char *location;

	bookmarks_remove_from (window->history, window->history_current);

	location = g_strconcat (prefix, path, NULL);
	bookmarks_remove_all_instances (window->history, location);
	bookmarks_add (window->history, location, FALSE, FALSE);
	g_free (location);

	window->history_current = window->history->list;
}


static void
catalog_activate_continue (gpointer data)
{
	GThumbWindow *window = data;

	window_update_sensitivity (window);

	/* Add to history list if not present as last entry. */

	if ((window->go_op == WINDOW_GO_TO)
	    && ((window->history_current == NULL) 
		|| ((window->catalog_path != NULL)
		    && (strcmp (window->catalog_path, pref_util_remove_prefix (window->history_current->data)) != 0)))) {
		GtkTreeIter iter;
		gboolean    is_search;

		if (! catalog_list_get_iter_from_path (window->catalog_list,
						       window->catalog_path,
						       &iter)) { 
			window_image_viewer_set_void (window);
			return;
		}
		is_search = catalog_list_is_search (window->catalog_list, &iter);
		add_history_item (window,
				  window->catalog_path,
				  is_search ? SEARCH_PREFIX : CATALOG_PREFIX);
	} else 
		window->go_op = WINDOW_GO_TO;

	window_update_history_list (window);
	window_update_title (window);
	window_make_current_image_visible (window);
}


static void
catalog_activate (GThumbWindow *window, 
		  const char   *cat_path)
{
	if (path_is_dir (cat_path)) {
		window_go_to_catalog (window, NULL);
		window_go_to_catalog_directory (window, cat_path);

	} else {
		Catalog *catalog = catalog_new ();
		GError  *gerror;

		if (! catalog_load_from_disk (catalog, cat_path, &gerror)) {
			_gtk_error_dialog_from_gerror_run (GTK_WINDOW (window->app), &gerror);
			catalog_free (catalog);
			window_image_viewer_set_void (window);
			return;
		}

		window_set_file_list (window, 
				      catalog->list,
				      catalog_activate_continue,
				      window);

		catalog_free (catalog);
		if (window->catalog_path != cat_path) {
			if (window->catalog_path)
				g_free (window->catalog_path);
			window->catalog_path = g_strdup (cat_path);
		}
	}
}


static void
catalog_list_activated_cb (GtkTreeView       *tree_view,
			   GtkTreePath       *path,
			   GtkTreeViewColumn *column,
			   gpointer           data)
{
	GThumbWindow *window = data;
	char         *cat_path;

	cat_path = catalog_list_get_path_from_tree_path (window->catalog_list,
							 path);
	if (cat_path == NULL)
		return;
	catalog_activate (window, cat_path);
	g_free (cat_path);
}


static int
catalog_list_button_press_cb (GtkWidget      *widget, 
			      GdkEventButton *event,
			      gpointer        data)
{
	GThumbWindow *window     = data;
	GtkWidget    *treeview   = window->catalog_list->list_view;
	GtkListStore *list_store = window->catalog_list->list_store;
	GtkTreeIter   iter;
	GtkTreePath  *path;

	if (catalog_list_tree_path != NULL) {
		gtk_tree_path_free (catalog_list_tree_path);
		catalog_list_tree_path = NULL;
	}

	if ((event->state & GDK_SHIFT_MASK) 
	    || (event->state & GDK_CONTROL_MASK))
		return FALSE;

	if ((event->button != 1) & (event->button != 3))
		return FALSE;

	/* Get the path. */

	if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview),
					     event->x, event->y,
					     &path, NULL, NULL, NULL))
		return FALSE;

	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (list_store), 
				       &iter, 
				       path)) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	catalog_list_tree_path = gtk_tree_path_copy (path);
	gtk_tree_path_free (path);

	return FALSE;
}


static int
catalog_list_button_release_cb (GtkWidget      *widget, 
				GdkEventButton *event,
				gpointer        data)
{
	GThumbWindow *window     = data;
	GtkWidget    *treeview   = window->catalog_list->list_view;
	GtkListStore *list_store = window->catalog_list->list_store;
	GtkTreeIter   iter;
	GtkTreePath  *path;

	if ((event->state & GDK_SHIFT_MASK) 
	    || (event->state & GDK_CONTROL_MASK))
		return FALSE;

	if ((event->button != 1) & (event->button != 3))
		return FALSE;

	/* Get the path. */

	if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview),
					     event->x, event->y,
					     &path, NULL, NULL, NULL)) {
		if (event->button != 3)
			return FALSE;
		
		window_update_sensitivity (window);
		gtk_menu_popup (GTK_MENU (window->catalog_list_popup_menu),
				NULL,
				NULL,
				NULL,
				NULL,
				3,
				event->time);

		return TRUE;
	}

	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (list_store), 
				       &iter, 
				       path)) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	if (gtk_tree_path_compare (catalog_list_tree_path, path) != 0) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	gtk_tree_path_free (path);

	/**/

	if ((event->button == 1) && 
	    (pref_get_real_click_policy () == GTH_CLICK_POLICY_SINGLE)) {
		char *cat_path;

		cat_path = catalog_list_get_path_from_iter (window->catalog_list, &iter);
		g_return_val_if_fail (cat_path != NULL, FALSE);
		catalog_activate (window, cat_path);
		g_free (cat_path);

		return FALSE;

	} else if (event->button == 3) {
		GtkTreeSelection *selection;
		char             *utf8_name;
		char             *name;

		utf8_name = catalog_list_get_name_from_iter (window->catalog_list, &iter);
		name = g_filename_from_utf8 (utf8_name, -1, NULL, NULL, NULL);
		g_free (utf8_name);

		if (strcmp (name, "..") == 0) {
			g_free (name);
			return TRUE;
		}
		g_free (name);

		/* Update selection. */

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->catalog_list->list_view));
		if (selection == NULL)
			return FALSE;

		if (! gtk_tree_selection_iter_is_selected (selection, &iter)) {
			gtk_tree_selection_unselect_all (selection);
			gtk_tree_selection_select_iter (selection, &iter);
                }

		/* Popup menu. */
		
		window_update_sensitivity (window);
		if (catalog_list_is_dir (window->catalog_list, &iter))
			gtk_menu_popup (GTK_MENU (window->library_popup_menu),
					NULL,
					NULL,
					NULL,
					NULL,
					3,
					event->time);
		else
			gtk_menu_popup (GTK_MENU (window->catalog_popup_menu),
					NULL,
					NULL,
					NULL,
					NULL,
					3,
					event->time);
		
		return TRUE;
	}

	return FALSE;
}


/* -- location entry stuff --*/


static char *
get_location (GThumbWindow *window)
{
	char *text;
	char *text2;
	char *l;

	text = _gtk_entry_get_filename_text (GTK_ENTRY (window->location_entry));
	text2 = remove_special_dirs_from_path (text);
	g_free (text);

	if (text2 == NULL)
		return NULL;

	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		l = g_strdup (text2);
	else {
		if (strcmp (text2, "/") == 0) {
			char *base = get_catalog_full_path (NULL);
			l = g_strconcat (base, "/", NULL);
			g_free (base);
		} else {
			if (*text2 == '/')
				l = get_catalog_full_path (text2 + 1);
			else
				l = get_catalog_full_path (text2);
		}
	}
	g_free (text2);

	return l;
}


static void
set_location (GThumbWindow *window,
	      const char   *location)
{
	const char *l;
	char       *abs_location;

	abs_location = remove_special_dirs_from_path (location);
	if (abs_location == NULL)
		return;

	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		l = abs_location;
	else {
		char *base = get_catalog_full_path (NULL);

		if (strlen (abs_location) == strlen (base))
			l = "/";
		else
			l = abs_location + strlen (base);
		g_free (base);
	}

	if (l) {
		char *utf8_l;
		utf8_l = g_filename_to_utf8 (l, -1, NULL, NULL, NULL);
		gtk_entry_set_text (GTK_ENTRY (window->location_entry), utf8_l);
		gtk_editable_set_position (GTK_EDITABLE (window->location_entry), g_utf8_strlen (utf8_l, -1));
		g_free (utf8_l);
	} else
		gtk_entry_set_text (GTK_ENTRY (window->location_entry), NULL);

	g_free (abs_location);
}


static gboolean
location_is_new (GThumbWindow *window, 
		 const char   *text)
{
	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		return (window->dir_list->path != NULL)
			&& strcmp (window->dir_list->path, text);
	else
		return (window->catalog_list->path != NULL)
			&& strcmp (window->catalog_list->path, text);
}


static void
go_to_location (GThumbWindow *window, 
		const char   *text)
{
	window->focus_location_entry = TRUE;

	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		window_go_to_directory (window, text);
	else {
		window_go_to_catalog (window, NULL);
		window_go_to_catalog_directory (window, text);
	}
}


static gint
location_entry_key_press_cb (GtkWidget    *widget, 
			     GdkEventKey  *event,
			     GThumbWindow *window)
{
	char *path;
	int   n;

	g_return_val_if_fail (window != NULL, FALSE);
	
	switch (event->keyval) {
	case GDK_Return:
	case GDK_KP_Enter:
		path = get_location (window);
		if (path != NULL) {
			go_to_location (window, path);
			g_free (path);
		}
                return FALSE;

	case GDK_Tab:
		if ((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
			return FALSE;
		
		path = get_location (window);
		n = auto_compl_get_n_alternatives (path);

		if (n > 0) { 
			char *text;
			text = auto_compl_get_common_prefix ();

			if (n == 1) {
				auto_compl_hide_alternatives ();
				if (location_is_new (window, text))
					go_to_location (window, text);
				else {
					/* Add a separator at the end. */
					char *new_path;
					int   len = strlen (path);

					if (strcmp (path, text) != 0) {
						/* Reset the right name. */
						set_location (window, text);
						g_free (path);
						return TRUE;
					}
					
					/* Ending separator, do nothing. */
					if ((len <= 1) 
					    || (path[len - 1] == '/')) {
						g_free (path);
						return TRUE;
					}

					new_path = g_strconcat (path,
								"/",
								NULL);
					set_location (window, new_path);
					g_free (new_path);

					/* Re-Tab */
					gtk_widget_event (widget, (GdkEvent*)event);
				}
			} else {
				set_location (window, text);
				auto_compl_show_alternatives (window, widget);
			}

			if (text)
				g_free (text);
		}
		g_free (path);
		
		return TRUE;
	}
	
	return FALSE;
}


/* -- */


static void
image_loaded_cb (GtkWidget    *widget, 
		 GThumbWindow *window)
{
	window->image_mtime = get_file_mtime (window->image_path);
	window->image_modified = FALSE;

	window_update_image_info (window);
	window_update_infobar (window);
	window_update_title (window);
	window_update_sensitivity (window);

	if (window->image_prop_dlg != NULL)
		dlg_image_prop_update (window->image_prop_dlg);

#ifdef HAVE_LIBEXIF
	{
		JPEGData *jdata;

		if (window->exif_data != NULL) {
			exif_data_unref (window->exif_data);
			window->exif_data = NULL;
		}
		
		jdata = jpeg_data_new_from_file (window->image_path);
		if (jdata != NULL) {
			window->exif_data = jpeg_data_get_exif_data (jdata);
			jpeg_data_unref (jdata);
		}
	}
#endif /* HAVE_LIBEXIF */
}


static void
image_requested_error_cb (GtkWidget    *widget, 
			  GThumbWindow *window)
{
	window_image_viewer_set_error (window);
}


static void
image_requested_done_cb (GtkWidget    *widget, 
			 GThumbWindow *window)
{
	ImageLoader *loader;

	window->image_error = FALSE;
	loader = gthumb_preloader_get_loader (window->preloader, window->image_path);
	if (loader != NULL) 
		image_viewer_load_from_image_loader (IMAGE_VIEWER (window->viewer), loader);
}


static gint
zoom_changed_cb (GtkWidget    *widget, 
		 GThumbWindow *window)
{
	window_update_statusbar_zoom_info (window);
	return TRUE;	
}


static gint
size_changed_cb (GtkWidget    *widget, 
		 GThumbWindow *window)
{
	GtkAdjustment *vadj, *hadj;
	gboolean       hide_vscr, hide_hscr;

	vadj = IMAGE_VIEWER (window->viewer)->vadj;
	hadj = IMAGE_VIEWER (window->viewer)->hadj;

	hide_vscr = vadj->upper <= vadj->page_size;
	hide_hscr = hadj->upper <= hadj->page_size;

	if (hide_vscr && hide_hscr) {
		gtk_widget_hide (window->viewer_vscr); 
		gtk_widget_hide (window->viewer_hscr); 
		gtk_widget_hide (window->viewer_event_box);
	} else {
		gtk_widget_show (window->viewer_vscr); 
		gtk_widget_show (window->viewer_hscr); 
		gtk_widget_show (window->viewer_event_box);
	}

	return TRUE;	
}


void
window_show_image_data (GThumbWindow *window)
{
	gtk_widget_show (window->preview_widget_data);
	gtk_widget_show (window->preview_widget_comment);
	gtk_widget_show (window->preview_widget_data_comment);
	set_button_active_no_notify (window, window->preview_button_data, TRUE);

	window->image_data_visible = TRUE;
	set_action_active_if_different (window, "View_ShowInfo", TRUE);
}


void
window_hide_image_data (GThumbWindow *window)
{
	gtk_widget_hide (window->preview_widget_data_comment);
	set_button_active_no_notify (window, window->preview_button_data, FALSE);
	window->image_data_visible = FALSE;
	set_action_active_if_different (window, "View_ShowInfo", FALSE);
}


static void
toggle_image_data_visibility (GThumbWindow *window)
{
	if (window->sidebar_visible)
		return;

	if (window->image_data_visible) 
		window_hide_image_data (window);
	else
		window_show_image_data (window);
}


static void
change_image_preview_content (GThumbWindow *window)
{
	if (! window->sidebar_visible) 
		return;

	if (! window->image_pane_visible) {
		window_show_image_pane (window);
		return;
	}

	if (window->preview_content == GTH_PREVIEW_CONTENT_IMAGE) 
		window_set_preview_content (window, GTH_PREVIEW_CONTENT_DATA);
	
	else if (window->preview_content == GTH_PREVIEW_CONTENT_DATA) 
		window_set_preview_content (window, GTH_PREVIEW_CONTENT_COMMENT);
	
	else if (window->preview_content == GTH_PREVIEW_CONTENT_COMMENT) 
		window_set_preview_content (window, GTH_PREVIEW_CONTENT_IMAGE);
}


static void
show_image_preview (GThumbWindow *window)
{
	window->preview_visible = TRUE;
	window_show_image_pane (window);
}


static void
hide_image_preview (GThumbWindow *window)
{
	window->preview_visible = FALSE;
	window_hide_image_pane (window);
}


static void
toggle_image_preview_visibility (GThumbWindow *window)
{
	if (! window->sidebar_visible) 
		return;

	if (window->preview_visible) 
		hide_image_preview (window);
	 else 
		show_image_preview (window);
}


static void
window_enable_thumbs (GThumbWindow *window,
		      gboolean      enable)
{
	gth_file_list_enable_thumbs (window->file_list, enable);
	set_action_sensitive (window, "Go_Stop", 
			       ((window->activity_ref > 0) 
				|| window->setting_file_list
				|| window->changing_directory
				|| window->file_list->doing_thumbs));
}


static gint
key_press_cb (GtkWidget   *widget, 
	      GdkEventKey *event,
	      gpointer     data)
{
	GThumbWindow *window = data;
	ImageViewer  *viewer = IMAGE_VIEWER (window->viewer);
	gboolean      sel_not_null;
	gboolean      image_is_void;

	if (GTK_WIDGET_HAS_FOCUS (window->location_entry)) {
		if (gtk_widget_event (window->location_entry, (GdkEvent*)event))
			return TRUE;
		else
			return FALSE;
	}

	if (GTK_WIDGET_HAS_FOCUS (window->preview_button_image)
	    || GTK_WIDGET_HAS_FOCUS (window->preview_button_data)
	    || GTK_WIDGET_HAS_FOCUS (window->preview_button_comment))
		if (event->keyval == GDK_space)
			return FALSE;

	if (window->sidebar_visible
	    && (event->state & GDK_CONTROL_MASK)
	    && ((event->keyval == GDK_1)
		|| (event->keyval == GDK_2)
		|| (event->keyval == GDK_3))) {
		GthPreviewContent content;

		switch (event->keyval) {
		case GDK_1:
			content = GTH_PREVIEW_CONTENT_IMAGE;
			break;
		case GDK_2:
			content = GTH_PREVIEW_CONTENT_DATA;
			break;
		case GDK_3:
		default:
			content = GTH_PREVIEW_CONTENT_COMMENT;
			break;
		}

		if (window->preview_content == content) 
			toggle_image_preview_visibility (window);
		 else {
			if (! window->preview_visible)
				show_image_preview (window);
			window_set_preview_content (window, content);
		}
	}

	if ((event->state & GDK_CONTROL_MASK) || (event->state & GDK_MOD1_MASK))
		return FALSE;

	sel_not_null = gth_file_view_selection_not_null (window->file_list->view);
	image_is_void = image_viewer_is_void (IMAGE_VIEWER (window->viewer));

	switch (event->keyval) {
		/* Hide/Show sidebar. */
	case GDK_Return:
	case GDK_KP_Enter:
		if (window->sidebar_visible) 
			window_hide_sidebar (window);
		else
			window_show_sidebar (window);
		return TRUE;

		/* Change image pane content. */
	case GDK_i:
		toggle_image_data_visibility (window);
		return TRUE;

	case GDK_q:
		change_image_preview_content (window);
		return TRUE;

		/* Hide/Show image pane. */
	case GDK_Q:
		toggle_image_preview_visibility (window);
		return TRUE;

		/* Full screen view. */
	case GDK_v:
	case GDK_F11:
		if (window->image_path != NULL)
			fullscreen_start (fullscreen, window);
		return TRUE;

		/* View/hide thumbnails. */
	case GDK_t:
		eel_gconf_set_boolean (PREF_SHOW_THUMBNAILS, !window->file_list->enable_thumbs);
		return TRUE;

		/* Zoom in. */
	case GDK_plus:
	case GDK_equal:
	case GDK_KP_Add:
		image_viewer_zoom_in (viewer);
		return TRUE;

		/* Zoom out. */
	case GDK_minus:
	case GDK_KP_Subtract:
		image_viewer_zoom_out (viewer);
		return TRUE;
		
		/* Actual size. */
	case GDK_KP_Divide:
	case GDK_1:
	case GDK_z:
		image_viewer_set_zoom (viewer, 1.0);
		return TRUE;

		/* Set zoom to 2.0. */
	case GDK_2:
		image_viewer_set_zoom (viewer, 2.0);
		return TRUE;

		/* Set zoom to 3.0. */
	case GDK_3:
		image_viewer_set_zoom (viewer, 3.0);
		return TRUE;

		/* Zoom to fit */
	case GDK_x:
		activate_action_view_zoom_fit (NULL, window);
		return TRUE;

		/* Play animation */
	case GDK_g:
		set_action_active (window, "View_PlayAnimation", ! viewer->play_animation);
		if (viewer->play_animation)
			image_viewer_stop_animation (viewer);
		else
			image_viewer_start_animation (viewer);
		return TRUE;

		/* Step animation */
	case GDK_j:
		activate_action_view_step_animation (NULL, window);
		return TRUE;

		/* Show previous image. */
	case GDK_p:
	case GDK_b:
	case GDK_BackSpace:
		window_show_prev_image (window, FALSE);
		return TRUE;

		/* Show next image. */
	case GDK_n:
		window_show_next_image (window, FALSE);
		return TRUE;

	case GDK_space:
		if (! GTK_WIDGET_HAS_FOCUS (window->dir_list->list_view)
		    && ! GTK_WIDGET_HAS_FOCUS (window->catalog_list->list_view)) {
			window_show_next_image (window, FALSE);
			return TRUE;
		}
		break;

		/* Rotate image */
	case GDK_bracketright:
	case GDK_r: 
		activate_action_alter_image_rotate90 (NULL, window);
		return TRUE;
			
		/* Flip image */
	case GDK_l:
		activate_action_alter_image_flip (NULL, window);
		return TRUE;

		/* Mirror image */
	case GDK_m:
		activate_action_alter_image_mirror (NULL, window);
		return TRUE;

		/* Rotate image counter-clockwise */
	case GDK_bracketleft:
		activate_action_alter_image_rotate90cc (NULL, window);
		return TRUE;
			
		/* Delete selection. */
	case GDK_Delete: 
	case GDK_KP_Delete:
		if (! sel_not_null)
			break;
		
		if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
			activate_action_edit_delete_files (NULL, window);
		else if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST)
			activate_action_edit_remove_from_catalog (NULL, window);
		return TRUE;

		/* Open images. */
	case GDK_o:
		if (! sel_not_null)
			break;

		activate_action_file_open_with (NULL, window);
		return TRUE;

		/* Go up one level */
	case GDK_u:
		window_go_up (window);
		return TRUE;

		/* Go home */
	case GDK_h:
		activate_action_go_home (NULL, window);
		return TRUE;

		/* Edit comment */
	case GDK_c:
		if (! sel_not_null)
			break;

		activate_action_edit_edit_comment (NULL, window);
		return TRUE;

		/* Edit categories */
	case GDK_k:
		if (! sel_not_null)
			break;

		activate_action_edit_edit_categories (NULL, window);
		return TRUE;

	case GDK_Escape:
		if (window->image_pane_visible)
			window_hide_image_pane (window);
		return TRUE;

	default:
		break;
	}

	if ((event->keyval == GDK_F10) 
	    && (event->state & GDK_SHIFT_MASK)) {

		if ((window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST)
		    && GTK_WIDGET_HAS_FOCUS (window->catalog_list->list_view)) {
			GtkTreeSelection *selection;
			GtkTreeIter       iter;
			char             *name, *utf8_name;

			selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->catalog_list->list_view));
			if (! gtk_tree_selection_get_selected (selection, NULL, &iter))
				return FALSE;

			utf8_name = catalog_list_get_name_from_iter (window->catalog_list, &iter);
			name = g_filename_from_utf8 (utf8_name, -1, NULL, NULL, NULL);
			g_free (utf8_name);

			if (strcmp (name, "..") == 0) 
				return TRUE;

			if (catalog_list_is_dir (window->catalog_list, &iter))
				gtk_menu_popup (GTK_MENU (window->library_popup_menu),
						NULL,
						NULL,
						NULL,
						NULL,
						3,
						event->time);
			else
				gtk_menu_popup (GTK_MENU (window->catalog_popup_menu),
						NULL,
						NULL,
						NULL,
						NULL,
						3,
						event->time);
		
			return TRUE;
			
		} else if ((window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
			   && GTK_WIDGET_HAS_FOCUS (window->dir_list->list_view)) {
			GtkTreeSelection *selection;
			GtkTreeIter       iter;
			char             *name, *utf8_name;

			selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->dir_list->list_view));
			if (! gtk_tree_selection_get_selected (selection, NULL, &iter))
				return FALSE;

			utf8_name = dir_list_get_name_from_iter (window->dir_list, &iter);
			name = g_filename_from_utf8 (utf8_name, -1, NULL, NULL, NULL);
			g_free (utf8_name);

			if (strcmp (name, "..") == 0) 
				return TRUE;

			gtk_menu_popup (GTK_MENU (window->dir_popup_menu),
					NULL,
					NULL,
					NULL,
					NULL,
					3,
					event->time);
			
			return TRUE;
		} else if (GTK_WIDGET_HAS_FOCUS (gth_file_view_get_widget (window->file_list->view))) {
			window_update_sensitivity (window);
			
			gtk_menu_popup (GTK_MENU (window->file_popup_menu),
					NULL,
					NULL,
					NULL,
					NULL,
					3,                               
					event->time);
			
			return TRUE;
		}
	}

	return FALSE;
}


static gboolean
image_clicked_cb (GtkWidget    *widget, 
		  GThumbWindow *window)
{
	if (! window->setting_file_list && ! window->changing_directory)
		window_show_next_image (window, FALSE);
	return TRUE;
}


static int
image_button_press_cb (GtkWidget      *widget, 
		       GdkEventButton *event,
		       gpointer        data)
{
	GThumbWindow *window = data;

	switch (event->button) {
	case 1:
		break;
	case 2:
		break;
	case 3:
		if (window->fullscreen)
			gtk_menu_popup (GTK_MENU (window->fullscreen_image_popup_menu),
					NULL,
					NULL,
					NULL,
					NULL,
					3,
					event->time);
		else
			gtk_menu_popup (GTK_MENU (window->image_popup_menu),
					NULL,
					NULL,
					NULL,
					NULL,
					3,
					event->time);

		return TRUE;
	}

	return FALSE;
}


static int
image_button_release_cb (GtkWidget      *widget, 
			 GdkEventButton *event,
			 gpointer        data)
{
	GThumbWindow *window = data;

	switch (event->button) {
	case 2:
		window_show_prev_image (window, FALSE);
		return TRUE;
	default:
		break;
	}

	return FALSE;
}


static int
image_comment_button_press_cb (GtkWidget      *widget, 
			       GdkEventButton *event,
			       gpointer        data)
{
	GThumbWindow *window = data;

	if ((event->button == 1) && (event->type == GDK_2BUTTON_PRESS)) 
		if (gth_file_view_selection_not_null (window->file_list->view)) {
			activate_action_edit_edit_comment (NULL, window); 
			return TRUE;
		}
	return FALSE;
}


static gboolean
image_focus_changed_cb (GtkWidget     *widget,
			GdkEventFocus *event,
			gpointer       data)
{
	GThumbWindow *window = data;
	gboolean      viewer_visible;
	gboolean      data_visible;
	gboolean      comment_visible;

	viewer_visible  = ((window->sidebar_visible 
			    && window->preview_visible 
			    && (window->preview_content == GTH_PREVIEW_CONTENT_IMAGE))
			   || ! window->sidebar_visible);
	data_visible    = (window->sidebar_visible 
			   && window->preview_visible 
			   && (window->preview_content == GTH_PREVIEW_CONTENT_DATA));
	comment_visible = (window->sidebar_visible 
			   && window->preview_visible 
			   && (window->preview_content == GTH_PREVIEW_CONTENT_COMMENT));

	if (viewer_visible)
		gthumb_info_bar_set_focused (GTHUMB_INFO_BAR (window->info_bar),
					     GTK_WIDGET_HAS_FOCUS (window->viewer));
	else if (data_visible)
		gthumb_info_bar_set_focused (GTHUMB_INFO_BAR (window->info_bar),
					     GTK_WIDGET_HAS_FOCUS (gth_exif_data_viewer_get_view (GTH_EXIF_DATA_VIEWER (window->exif_data_viewer))));
	else if (comment_visible)
		gthumb_info_bar_set_focused (GTHUMB_INFO_BAR (window->info_bar),
					     GTK_WIDGET_HAS_FOCUS (window->image_comment));

	return FALSE;
}


/* -- drag & drop -- */


static GList *
get_file_list_from_url_list (char *url_list)
{
	GList *list = NULL;
	int    i;
	char  *url_start, *url_end;

	url_start = url_list;
	while (url_start[0] != '\0') {
		char *escaped;
		char *unescaped;

		if (strncmp (url_start, "file:", 5) == 0) {
			url_start += 5;
			if ((url_start[0] == '/') 
			    && (url_start[1] == '/')) url_start += 2;
		}

		i = 0;
		while ((url_start[i] != '\0')
		       && (url_start[i] != '\r')
		       && (url_start[i] != '\n')) i++;
		url_end = url_start + i;

		escaped = g_strndup (url_start, url_end - url_start);
		unescaped = gnome_vfs_unescape_string_for_display (escaped);
		g_free (escaped);

		list = g_list_prepend (list, unescaped);

		url_start = url_end;
		i = 0;
		while ((url_start[i] != '\0')
		       && ((url_start[i] == '\r')
			   || (url_start[i] == '\n'))) i++;
		url_start += i;
	}
	
	return g_list_reverse (list);
}


static void  
viewer_drag_data_get  (GtkWidget        *widget,
		       GdkDragContext   *context,
		       GtkSelectionData *selection_data,
		       guint             info,
		       guint             time,
		       gpointer          data)
{
	GThumbWindow *window = data;
	char         *path;

	if (IMAGE_VIEWER (window->viewer)->is_void) 
		return;

	path = image_viewer_get_image_filename (IMAGE_VIEWER (window->viewer));

	gtk_selection_data_set (selection_data,
				selection_data->target,
				8, 
				path, strlen (path));
	g_free (path);
}


static void  
viewer_drag_data_received  (GtkWidget          *widget,
			    GdkDragContext     *context,
			    int                 x,
			    int                 y,
			    GtkSelectionData   *data,
			    guint               info,
			    guint               time,
			    gpointer            extra_data)
{
	GThumbWindow *window = extra_data;
	Catalog      *catalog;
	char         *catalog_path;
	char         *catalog_name, *catalog_name_utf8;
	GList        *list;
	GList        *scan;
	GError       *gerror;
	gboolean      empty = TRUE;

	if (! ((data->length >= 0) && (data->format == 8))) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);

	list = get_file_list_from_url_list ((char *) data->data);

	/* Create a catalog with the Drag&Drop list. */

	catalog = catalog_new ();
	catalog_name_utf8 = g_strconcat (_("Dragged Images"),
					 CATALOG_EXT,
					 NULL);
	catalog_name = g_filename_from_utf8 (catalog_name_utf8, -1, 0, 0, 0);
	catalog_path = get_catalog_full_path (catalog_name);
	g_free (catalog_name);
	g_free (catalog_name_utf8);

	catalog_set_path (catalog, catalog_path);

	for (scan = list; scan; scan = scan->next) {
		char *filename = scan->data;
		if (path_is_file (filename)) {
			catalog_add_item (catalog, filename);
			empty = FALSE;
		}
	}

	if (! empty) {
		if (! catalog_write_to_disk (catalog, &gerror)) 
			_gtk_error_dialog_from_gerror_run (GTK_WINDOW (window->app), &gerror);
		else {
			/* View the Drag&Drop catalog. */
			ViewFirstImage = TRUE;
			window_go_to_catalog (window, catalog_path);
		}
	}

	catalog_free (catalog);
	path_list_free (list);
	g_free (catalog_path);
}


static gint
viewer_key_press_cb (GtkWidget   *widget, 
		     GdkEventKey *event,
		     gpointer     data)
{
	GThumbWindow *window = data;

	switch (event->keyval) {
	case GDK_Page_Up:
		window_show_prev_image (window, FALSE);
		return TRUE;

	case GDK_Page_Down:
		window_show_next_image (window, FALSE);
		return TRUE;

	case GDK_Home:
		window_show_first_image (window, FALSE);
		return TRUE;

	case GDK_End:
		window_show_last_image (window, FALSE);
		return TRUE;

	case GDK_F10:
		if (event->state & GDK_SHIFT_MASK) {
			gtk_menu_popup (GTK_MENU (window->image_popup_menu),
					NULL,
					NULL,
					NULL,
					NULL,
					3,
					event->time);
			return TRUE;
		}
	}
	
	return FALSE;
}


static gboolean
info_bar_clicked_cb (GtkWidget      *widget,
		     GdkEventButton *event,
		     GThumbWindow   *window)
{
	GtkWidget *widget_to_focus = window->viewer;

	if (window->sidebar_visible)
		switch (window->preview_content) {
		case GTH_PREVIEW_CONTENT_IMAGE:
			widget_to_focus = window->viewer;
			break;
		case GTH_PREVIEW_CONTENT_DATA:
			widget_to_focus = gth_exif_data_viewer_get_view (GTH_EXIF_DATA_VIEWER (window->exif_data_viewer));
			break;
		case GTH_PREVIEW_CONTENT_COMMENT:
			widget_to_focus = window->image_comment;
			break;
		}

	gtk_widget_grab_focus (widget_to_focus);

	return TRUE;
}


static GString*
make_url_list (GList    *list, 
	       int       target)
{
	GList      *scan;
	GString    *result;

	if (list == NULL)
		return NULL;

	result = g_string_new (NULL);
	for (scan = list; scan; scan = scan->next) {
		char *url;

		switch (target) {
		case TARGET_PLAIN:
			url = g_locale_from_utf8 (scan->data, -1, NULL, NULL, NULL);
			if (url == NULL) 
				continue;
			g_string_append (result, url);
			g_free (url);
			break;

		case TARGET_PLAIN_UTF8:
			g_string_append (result, scan->data);
			break;
			
		case TARGET_URILIST:
			url = gnome_vfs_get_uri_from_local_path (scan->data);
			if (url == NULL) 
				continue;
			g_string_append (result, url);
			g_free (url);
			break;
		}

		g_string_append (result, "\r\n");
	}
	
	return result;
}


static void
gth_file_list_drag_begin (GtkWidget          *widget,
			  GdkDragContext     *context,
			  gpointer            extra_data)
{	
	GThumbWindow *window = extra_data;

	debug (DEBUG_INFO, "Gth::FileList::DragBegin");

	if (gth_file_list_drag_data != NULL)
		path_list_free (gth_file_list_drag_data);
	gth_file_list_drag_data = gth_file_view_get_file_list_selection (window->file_list->view);
}


static void
gth_file_list_drag_end (GtkWidget          *widget,
			GdkDragContext     *context,
			gpointer            extra_data)
{	
	debug (DEBUG_INFO, "Gth::FileList::DragEnd");

	if (gth_file_list_drag_data != NULL)
		path_list_free (gth_file_list_drag_data);
	gth_file_list_drag_data = NULL;
}


static void  
gth_file_list_drag_data_get  (GtkWidget        *widget,
			      GdkDragContext   *context,
			      GtkSelectionData *selection_data,
			      guint             info,
			      guint             time,
			      gpointer          data)
{
	GString *url_list;

	debug (DEBUG_INFO, "Gth::FileList::DragDataGet");

	url_list = make_url_list (gth_file_list_drag_data, info);

	if (url_list == NULL) 
		return;

	gtk_selection_data_set (selection_data, 
				selection_data->target,
				8, 
				url_list->str, 
				url_list->len);

	g_string_free (url_list, TRUE);	
}


static void
move_items__continue (GnomeVFSResult result,
		      gpointer       data)
{
	GThumbWindow *window = data;

	if (result != GNOME_VFS_OK) 
		_gtk_error_dialog_run (GTK_WINDOW (window->app),
				       "%s %s",
				       _("Could not move the items:"), 
				       gnome_vfs_result_to_string (result));
}


static void
add_image_list_to_catalog (GThumbWindow *window,
			   const char   *catalog_path, 
			   GList        *list)
{
	Catalog *catalog;
	GError  *gerror;

	if ((catalog_path == NULL) || ! path_is_file (catalog_path)) 
		return;
	
	catalog = catalog_new ();
	
	if (! catalog_load_from_disk (catalog, catalog_path, &gerror)) 
		_gtk_error_dialog_from_gerror_run (GTK_WINDOW (window->app), &gerror);
	
	else {
		GList *scan;
		GList *files_added = NULL;
			
		for (scan = list; scan; scan = scan->next) {
			char *filename = scan->data;
			if (path_is_file (filename)) {
				catalog_add_item (catalog, filename);
				files_added = g_list_prepend (files_added, filename);
			}
		}
		
		if (! catalog_write_to_disk (catalog, &gerror)) 
			_gtk_error_dialog_from_gerror_run (GTK_WINDOW (window->app), &gerror);
		else 
			all_windows_notify_cat_files_added (catalog_path, files_added);
		
		g_list_free (files_added);
	}
	
	catalog_free (catalog);
}


static void  
image_list_drag_data_received  (GtkWidget          *widget,
				GdkDragContext     *context,
				int                 x,
				int                 y,
				GtkSelectionData   *data,
				guint               info,
				guint               time,
				gpointer            extra_data)
{
	GThumbWindow *window = extra_data;

	if (! ((data->length >= 0) && (data->format == 8))
	    || (((context->action & GDK_ACTION_COPY) != GDK_ACTION_COPY)
		&& ((context->action & GDK_ACTION_MOVE) != GDK_ACTION_MOVE))) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);

	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST) {
		char *dest_dir = window->dir_list->path;
		if (dest_dir != NULL) {
			GList *list;
			list = get_file_list_from_url_list ((char*) data->data);
			dlg_copy_items (window, 
					list,
					dest_dir,
					((context->action & GDK_ACTION_MOVE) == GDK_ACTION_MOVE),
					TRUE,
					FALSE,
					move_items__continue,
					window);
			path_list_free (list);
		}

	} else if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST) {
		GList *list = get_file_list_from_url_list ((char*) data->data);
		add_image_list_to_catalog (window, window->catalog_path, list);
		path_list_free (list);
	}
}


static void  
dir_list_drag_data_received  (GtkWidget          *widget,
			      GdkDragContext     *context,
			      int                 x,
			      int                 y,
			      GtkSelectionData   *data,
			      guint               info,
			      guint               time,
			      gpointer            extra_data)
{
	GThumbWindow            *window = extra_data;
	GtkTreePath             *pos_path;
	GtkTreeViewDropPosition  drop_pos;
	int                      pos;
	char                    *dest_dir = NULL;
	const char              *current_dir;

	debug (DEBUG_INFO, "DirList::DragDataReceived");

	if ((data->length < 0) 
	    || (data->format != 8)
	    || (((context->action & GDK_ACTION_COPY) != GDK_ACTION_COPY)
		&& ((context->action & GDK_ACTION_MOVE) != GDK_ACTION_MOVE))) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);

	g_return_if_fail (window->sidebar_content == GTH_SIDEBAR_DIR_LIST);

	/**/

	if (gtk_tree_view_get_dest_row_at_pos (GTK_TREE_VIEW (window->dir_list->list_view),
					       x, y,
					       &pos_path,
					       &drop_pos)) {
		pos = gtk_tree_path_get_indices (pos_path)[0];
		gtk_tree_path_free (pos_path);
	} else
		pos = -1;
	
	current_dir = window->dir_list->path;
	
	if (pos == -1) {
		if (current_dir != NULL)
			dest_dir = g_strdup (current_dir);
	} else
		dest_dir = dir_list_get_path_from_row (window->dir_list,
						       pos);
	
	/**/

	debug (DEBUG_INFO, "DirList::DragDataReceived: %s", dest_dir);
	
	if (dest_dir != NULL) {
		GList *list;
		list = get_file_list_from_url_list ((char*) data->data);

		dlg_copy_items (window, 
				list,
				dest_dir,
				((context->action & GDK_ACTION_MOVE) == GDK_ACTION_MOVE),
				TRUE,
				FALSE,
				move_items__continue,
				window);
		path_list_free (list);
	}
	
	g_free (dest_dir);
}


static gboolean
auto_load_directory_cb (gpointer data)
{
	GThumbWindow            *window = data;
	GtkTreePath             *pos_path;
	GtkTreeViewDropPosition  drop_pos;
	GtkTreeView             *list_view;
	char                    *new_path;

	list_view = GTK_TREE_VIEW (window->dir_list->list_view);

	g_source_remove (window->auto_load_timeout);

	gtk_tree_view_get_drag_dest_row (list_view, &pos_path, &drop_pos);
	new_path = dir_list_get_path_from_tree_path (window->dir_list, pos_path);
	if (new_path)  {
		window_go_to_directory (window, new_path);
		g_free(new_path);
	}

	gtk_tree_path_free (pos_path);

	window->auto_load_timeout = 0;

	return FALSE;
}


static gboolean
dir_list_drag_motion (GtkWidget          *widget,
		      GdkDragContext     *context,
		      gint                x,
		      gint                y,
		      guint               time,
		      gpointer            extra_data)
{
	GThumbWindow            *window = extra_data;
	GtkTreePath             *pos_path;
	GtkTreeViewDropPosition  drop_pos;
	GtkTreeView             *list_view;

	debug (DEBUG_INFO, "DirList::DragMotion");

	list_view = GTK_TREE_VIEW (window->dir_list->list_view);

	if (window->auto_load_timeout != 0) {
		g_source_remove (window->auto_load_timeout);
		window->auto_load_timeout = 0;
	}

	if (! gtk_tree_view_get_dest_row_at_pos (list_view,
						 x, y,
						 &pos_path,
						 &drop_pos)) 
		pos_path = gtk_tree_path_new_first ();

	else 
		window->auto_load_timeout = g_timeout_add (AUTO_LOAD_DELAY, 
							   auto_load_directory_cb, 
							   window);

	switch (drop_pos) {
	case GTK_TREE_VIEW_DROP_BEFORE:
	case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
		drop_pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
		break;

	case GTK_TREE_VIEW_DROP_AFTER:
	case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
		drop_pos = GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
		break;
	}

	gtk_tree_view_set_drag_dest_row (list_view, pos_path, drop_pos);
	gtk_tree_path_free (pos_path);

	return TRUE;
}


static void
dir_list_drag_begin (GtkWidget          *widget,
		     GdkDragContext     *context,
		     gpointer            extra_data)
{	
	GThumbWindow      *window = extra_data;
	GtkTreeIter        iter;
	GtkTreeSelection  *selection;

	debug (DEBUG_INFO, "DirList::DragBegin");

	if (dir_list_tree_path != NULL) {
		gtk_tree_path_free (dir_list_tree_path);
		dir_list_tree_path = NULL;
	}

	if (dir_list_drag_data != NULL) {
		g_free (dir_list_drag_data);
		dir_list_drag_data = NULL;
	}

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->dir_list->list_view));
	if (! gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	dir_list_drag_data = dir_list_get_path_from_iter (window->dir_list, &iter);
}


static void
dir_list_drag_end (GtkWidget          *widget,
		   GdkDragContext     *context,
		   gpointer            extra_data)
{	
	if (dir_list_drag_data != NULL) {
		g_free (dir_list_drag_data);
		dir_list_drag_data = NULL;
	}
}


static void
dir_list_drag_leave (GtkWidget          *widget,
		     GdkDragContext     *context,
		     guint               time,
		     gpointer            extra_data)
{	
	GThumbWindow  *window = extra_data;
	GtkTreeView   *list_view;

	debug (DEBUG_INFO, "DirList::DragLeave");

	if (window->auto_load_timeout != 0) {
		g_source_remove (window->auto_load_timeout);
		window->auto_load_timeout = 0;
	}

	list_view = GTK_TREE_VIEW (window->dir_list->list_view);
	gtk_tree_view_set_drag_dest_row  (list_view, NULL, 0);
}


static void  
dir_list_drag_data_get  (GtkWidget        *widget,
			 GdkDragContext   *context,
			 GtkSelectionData *selection_data,
			 guint             info,
			 guint             time,
			 gpointer          data)
{
        char *target;
	char *uri;

	debug (DEBUG_INFO, "DirList::DragDataGet");

	if (dir_list_drag_data == NULL)
		return;

        target = gdk_atom_name (selection_data->target);
        if (strcmp (target, "text/uri-list") != 0) {
		g_free (target);
		return;
	}
        g_free (target);

	uri = g_strconcat ("file://", dir_list_drag_data, "\n", NULL);
        gtk_selection_data_set (selection_data, 
                                selection_data->target,
                                8, 
                                dir_list_drag_data, 
                                strlen (uri));
	g_free (dir_list_drag_data);
	dir_list_drag_data = NULL;
	g_free (uri);
}


static void  
catalog_list_drag_data_received  (GtkWidget          *widget,
				  GdkDragContext     *context,
				  int                 x,
				  int                 y,
				  GtkSelectionData   *data,
				  guint               info,
				  guint               time,
				  gpointer            extra_data)
{
	GThumbWindow            *window = extra_data;
	GtkTreePath             *pos_path;
	GtkTreeViewDropPosition  drop_pos;
	int                      pos;
	char                    *catalog_path = NULL;

	if (! ((data->length >= 0) && (data->format == 8))) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);

	g_return_if_fail (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST);

	/**/

	if (gtk_tree_view_get_dest_row_at_pos (GTK_TREE_VIEW (window->catalog_list->list_view),
					       x, y,
					       &pos_path,
					       &drop_pos)) {
		pos = gtk_tree_path_get_indices (pos_path)[0];
		gtk_tree_path_free (pos_path);
	} else
		pos = -1;
	
	if (pos == -1) {
		if (window->catalog_path != NULL)
			catalog_path = g_strdup (window->catalog_path);
	} else
		catalog_path = catalog_list_get_path_from_row (window->catalog_list, pos);
	
	if (catalog_path != NULL) {
		GList *list = get_file_list_from_url_list ((char*) data->data);
		add_image_list_to_catalog (window, catalog_path, list);
		path_list_free (list);
		g_free (catalog_path);
	}
}


static gboolean
auto_load_library_cb (gpointer data)
{
	GThumbWindow            *window = data;
	GtkTreePath             *pos_path;
	GtkTreeViewDropPosition  drop_pos;
	GtkTreeView             *list_view;
	char                    *new_path;

	list_view = GTK_TREE_VIEW (window->catalog_list->list_view);

	g_source_remove (window->auto_load_timeout);
	window->auto_load_timeout = 0;

	gtk_tree_view_get_drag_dest_row (list_view, &pos_path, &drop_pos);
	new_path = catalog_list_get_path_from_tree_path (window->catalog_list, pos_path);

	if (new_path)  {
		if (path_is_dir (new_path))
			window_go_to_catalog_directory (window, new_path);
		g_free(new_path);
	}

	gtk_tree_path_free (pos_path);

	return FALSE;
}


static gboolean
catalog_list_drag_motion (GtkWidget          *widget,
			  GdkDragContext     *context,
			  gint                x,
			  gint                y,
			  guint               time,
			  gpointer            extra_data)
{
	GThumbWindow            *window = extra_data;
	GtkTreePath             *pos_path;
	GtkTreeViewDropPosition  drop_pos;
	GtkTreeView             *list_view;

	list_view = GTK_TREE_VIEW (window->catalog_list->list_view);

	if (window->auto_load_timeout != 0) {
		g_source_remove (window->auto_load_timeout);
		window->auto_load_timeout = 0;
	}

	if (! gtk_tree_view_get_dest_row_at_pos (list_view,
						 x, y,
						 &pos_path,
						 &drop_pos)) 
		pos_path = gtk_tree_path_new_first ();
	else 
		window->auto_load_timeout = g_timeout_add (AUTO_LOAD_DELAY, 
							   auto_load_library_cb, 
							   window);

	switch (drop_pos) {
	case GTK_TREE_VIEW_DROP_BEFORE:
	case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
		drop_pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
		break;

	case GTK_TREE_VIEW_DROP_AFTER:
	case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
		drop_pos = GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
		break;
	}

	gtk_tree_view_set_drag_dest_row  (list_view, pos_path, drop_pos);
	gtk_tree_path_free (pos_path);

	return TRUE;
}


static void
catalog_list_drag_begin (GtkWidget          *widget,
			 GdkDragContext     *context,
			 gpointer            extra_data)
{	
	if (catalog_list_tree_path != NULL) {
		gtk_tree_path_free (catalog_list_tree_path);
		catalog_list_tree_path = NULL;
	}
}


static void
catalog_list_drag_leave (GtkWidget          *widget,
			 GdkDragContext     *context,
			 guint               time,
			 gpointer            extra_data)
{	
	GThumbWindow *window = extra_data;
	GtkTreeView  *list_view;

	if (window->auto_load_timeout != 0) {
		g_source_remove (window->auto_load_timeout);
		window->auto_load_timeout = 0;
	}

	list_view = GTK_TREE_VIEW (window->catalog_list->list_view);
	gtk_tree_view_set_drag_dest_row  (list_view, NULL, 0);
}


/* -- */


static gboolean
set_busy_cursor_cb (gpointer data)
{
	GThumbWindow *window = data;
	GdkCursor    *cursor;

	if (window->busy_cursor_timeout != 0) {
		g_source_remove (window->busy_cursor_timeout);
		window->busy_cursor_timeout = 0;
	}

	cursor = gdk_cursor_new (GDK_WATCH);
	gdk_window_set_cursor (window->app->window, cursor);
	gdk_cursor_unref (cursor);

	return FALSE;
}


static void
file_list_busy_cb (GthFileList  *file_list,
		   GThumbWindow *window)
{
	if (! GTK_WIDGET_REALIZED (window->app))
		return;

	if (window->busy_cursor_timeout != 0)
		g_source_remove (window->busy_cursor_timeout);

	window->busy_cursor_timeout = g_timeout_add (BUSY_CURSOR_DELAY,
						     set_busy_cursor_cb,
						     window);
}


static void
file_list_idle_cb (GthFileList  *file_list,
		   GThumbWindow *window)
{
	GdkCursor *cursor;

	if (! GTK_WIDGET_REALIZED (window->app))
		return;

	if (window->busy_cursor_timeout != 0) {
		g_source_remove (window->busy_cursor_timeout);
		window->busy_cursor_timeout = 0;
	}

	cursor = gdk_cursor_new (GDK_LEFT_PTR);
	gdk_window_set_cursor (window->app->window, cursor);
	gdk_cursor_unref (cursor);
}


static gboolean
progress_cancel_cb (GtkButton    *button,
		    GThumbWindow *window)
{
	if (window->pixop != NULL)
		gth_pixbuf_op_stop (window->pixop);
	return TRUE;
}


static gboolean
progress_delete_cb (GtkWidget    *caller, 
		    GdkEvent     *event,
		    GThumbWindow *window)
{
	if (window->pixop != NULL)
		gth_pixbuf_op_stop (window->pixop);
	return TRUE;
}


/* -- setup toolbar custom widgets -- */


static gboolean
combo_button_activate_default_callback (EComboButton *combo_button,
					void *data)
{
	GThumbWindow *window = data;
	activate_action_go_back (NULL, window);
	return TRUE;
}


static void
setup_toolbar_go_back_button (GThumbWindow *window)
{
	GtkWidget *combo_button;
	GtkMenu   *menu;
	GdkPixbuf *icon;

	icon = gtk_widget_render_icon (window->app,
				       GTK_STOCK_GO_BACK,
				       GTK_ICON_SIZE_LARGE_TOOLBAR,
				       "");

	combo_button = e_combo_button_new ();

	menu = (GtkMenu*) gtk_ui_manager_get_widget (window->ui, "/HistoryListPopup");
	e_combo_button_set_menu (E_COMBO_BUTTON (combo_button), menu);
	e_combo_button_set_label (E_COMBO_BUTTON (combo_button), _("Back"));

	e_combo_button_set_icon (E_COMBO_BUTTON (combo_button), icon);
	g_object_unref (icon);

	gtk_widget_show (combo_button);

	e_combo_button_set_style (E_COMBO_BUTTON (combo_button), GTH_TOOLBAR_STYLE_ICONS);

	g_signal_connect (combo_button, "activate_default",
			  G_CALLBACK (combo_button_activate_default_callback),
			  window);

	window->go_back_toolbar_button = combo_button;

	gtk_tooltips_set_tip (window->tooltips, combo_button, _("Go to the previous visited location"), NULL);
}


static void
window_sync_menu_with_preferences (GThumbWindow *window)
{
	char *prop = "TranspTypeNone";

	set_action_active (window, "View_PlayAnimation", TRUE);

	switch (pref_get_zoom_quality ()) {
	case GTH_ZOOM_QUALITY_HIGH: prop = "View_ZoomQualityHigh"; break;
	case GTH_ZOOM_QUALITY_LOW:  prop = "View_ZoomQualityLow"; break;
	}
	set_action_active (window, prop, TRUE);

	set_action_active (window, "View_ShowFolders", FALSE);
	set_action_active (window, "View_ShowCatalogs", FALSE);

	set_action_active (window, "View_ShowPreview", eel_gconf_get_boolean (PREF_SHOW_PREVIEW, FALSE));
	set_action_active (window, "View_ShowInfo", eel_gconf_get_boolean (PREF_SHOW_IMAGE_DATA, FALSE));

	/* Sort type item. */

	switch (window->file_list->sort_method) {
	case GTH_SORT_METHOD_BY_NAME: prop = "SortByName"; break;
	case GTH_SORT_METHOD_BY_PATH: prop = "SortByPath"; break;
	case GTH_SORT_METHOD_BY_SIZE: prop = "SortBySize"; break;
	case GTH_SORT_METHOD_BY_TIME: prop = "SortByTime"; break;
	default: prop = "X"; break;
	}
	set_action_active (window, prop, TRUE);

	/* 'reversed' item. */

	set_action_active (window, prop, TRUE);
	set_action_active (window, "SortReversed", (window->file_list->sort_type != GTK_SORT_ASCENDING));
}


/* preferences change notification callbacks */


static void
pref_ui_layout_changed (GConfClient *client,
			guint        cnxn_id,
			GConfEntry  *entry,
			gpointer     user_data)
{
	GThumbWindow *window = user_data;
	window_notify_update_layout (window);
}


static void
pref_ui_toolbar_style_changed (GConfClient *client,
			       guint        cnxn_id,
			       GConfEntry  *entry,
			       gpointer     user_data)
{
	GThumbWindow *window = user_data;
	window_notify_update_toolbar_style (window);
}


static void
pref_ui_toolbar_visible_changed (GConfClient *client,
				 guint        cnxn_id,
				 GConfEntry  *entry,
				 gpointer     user_data)
{
	GThumbWindow *window = user_data;
	if (gconf_value_get_bool (gconf_entry_get_value (entry)))
		gtk_widget_show (window->toolbar->parent);
	else
		gtk_widget_hide (window->toolbar->parent);
}


static void
pref_ui_statusbar_visible_changed (GConfClient *client,
				   guint        cnxn_id,
				   GConfEntry  *entry,
				   gpointer     user_data)
{
	GThumbWindow *window = user_data;
	if (gconf_value_get_bool (gconf_entry_get_value (entry)))
		gtk_widget_show (window->statusbar);
	else
		gtk_widget_hide (window->statusbar);
}


static void
pref_show_thumbnails_changed (GConfClient *client,
			       guint        cnxn_id,
			       GConfEntry  *entry,
			       gpointer     user_data)
{
	GThumbWindow *window = user_data;

	window->file_list->enable_thumbs = eel_gconf_get_boolean (PREF_SHOW_THUMBNAILS, TRUE);
	window_enable_thumbs (window, window->file_list->enable_thumbs);
}


static void
pref_show_filenames_changed (GConfClient *client,
			     guint        cnxn_id,
			     GConfEntry  *entry,
			     gpointer     user_data)
{
	GThumbWindow  *window = user_data;
	gth_file_view_set_view_mode (window->file_list->view, pref_get_view_mode ());
}


static void
pref_show_comments_changed (GConfClient *client,
			    guint        cnxn_id,
			    GConfEntry  *entry,
			    gpointer     user_data)
{
	GThumbWindow  *window = user_data;
	gth_file_view_set_view_mode (window->file_list->view, pref_get_view_mode ());
}


static void
pref_show_hidden_files_changed (GConfClient *client,
				guint        cnxn_id,
				GConfEntry  *entry,
				gpointer     user_data)
{
	GThumbWindow *window = user_data;
	window_update_file_list (window);
}


static void
pref_thumbnail_size_changed (GConfClient *client,
			     guint        cnxn_id,
			     GConfEntry  *entry,
			     gpointer     user_data)
{
	GThumbWindow *window = user_data;

	gth_file_list_set_thumbs_size (window->file_list, eel_gconf_get_integer (PREF_THUMBNAIL_SIZE, 95));
}


static void
pref_click_policy_changed (GConfClient *client,
			   guint        cnxn_id,
			   GConfEntry  *entry,
			   gpointer     user_data)
{
	GThumbWindow *window = user_data;
	dir_list_update_underline (window->dir_list);
	catalog_list_update_underline (window->catalog_list);
}


static void
pref_zoom_quality_changed (GConfClient *client,
			   guint        cnxn_id,
			   GConfEntry  *entry,
			   gpointer     user_data)
{
	GThumbWindow *window = user_data;

	if (pref_get_zoom_quality () == GTH_ZOOM_QUALITY_HIGH) {
		set_action_active_if_different (window, "View_ZoomQualityHigh", 1);
		image_viewer_set_zoom_quality (IMAGE_VIEWER (window->viewer),
					       GTH_ZOOM_QUALITY_HIGH);
	} else {
		set_action_active_if_different (window, "View_ZoomQualityLow", 1);
		image_viewer_set_zoom_quality (IMAGE_VIEWER (window->viewer),
					       GTH_ZOOM_QUALITY_LOW);
		
	}

	image_viewer_update_view (IMAGE_VIEWER (window->viewer));
}


static void
pref_zoom_change_changed (GConfClient *client,
			  guint        cnxn_id,
			  GConfEntry  *entry,
			  gpointer     user_data)
{
	GThumbWindow *window = user_data;

	image_viewer_set_zoom_change (IMAGE_VIEWER (window->viewer),
				      pref_get_zoom_change ());
	image_viewer_update_view (IMAGE_VIEWER (window->viewer));
}


static void
pref_transp_type_changed (GConfClient *client,
			  guint        cnxn_id,
			  GConfEntry  *entry,
			  gpointer     user_data)
{
	GThumbWindow *window = user_data;
	
	image_viewer_set_transp_type (IMAGE_VIEWER (window->viewer),
				      pref_get_transp_type ());
	image_viewer_update_view (IMAGE_VIEWER (window->viewer));
}


static void
pref_check_type_changed (GConfClient *client,
			 guint        cnxn_id,
			 GConfEntry  *entry,
			 gpointer     user_data)
{
	GThumbWindow *window = user_data;

	image_viewer_set_check_type (IMAGE_VIEWER (window->viewer),
				     pref_get_check_type ());
	image_viewer_update_view (IMAGE_VIEWER (window->viewer));
}


static void
pref_check_size_changed (GConfClient *client,
			 guint        cnxn_id,
			 GConfEntry  *entry,
			 gpointer     user_data)
{
	GThumbWindow *window = user_data;

	image_viewer_set_check_size (IMAGE_VIEWER (window->viewer),
				     pref_get_check_size ());
	image_viewer_update_view (IMAGE_VIEWER (window->viewer));
}


static void
pref_black_background_changed (GConfClient *client,
			       guint        cnxn_id,
			       GConfEntry  *entry,
			       gpointer     user_data)
{
	GThumbWindow *window = user_data;

	image_viewer_set_black_background (IMAGE_VIEWER (window->viewer),
					   eel_gconf_get_boolean (PREF_BLACK_BACKGROUND, FALSE));
}


static GthFileList *
create_new_file_list (GThumbWindow *window)
{
	GthFileList *file_list;
	GtkWidget   *view_widget;

	file_list = gth_file_list_new ();
	view_widget = gth_file_view_get_widget (file_list->view);

	gtk_widget_set_size_request (file_list->root_widget,
				     PANE_MIN_SIZE,
				     PANE_MIN_SIZE);
	gth_file_list_set_progress_func (file_list, window_progress, window);

	gtk_drag_dest_set (file_list->root_widget,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, G_N_ELEMENTS (target_table),
			   GDK_ACTION_COPY);
	gtk_drag_source_set (file_list->root_widget,
			     GDK_BUTTON1_MASK,
			     target_table, G_N_ELEMENTS (target_table),
			     GDK_ACTION_MOVE | GDK_ACTION_COPY);
	/* FIXME
	gtk_drag_dest_set (file_list->root_widget,
			   GTK_DEST_DEFAULT_ALL,
			   same_app_target_table, G_N_ELEMENTS (same_app_target_table),
			   GDK_ACTION_MOVE);
	
	gtk_drag_source_set (file_list->root_widget,
			     GDK_BUTTON1_MASK,
			     same_app_target_table, G_N_ELEMENTS (same_app_target_table),
			     GDK_ACTION_MOVE);*/
	g_signal_connect (G_OBJECT (file_list->view),
			  "selection_changed",
			  G_CALLBACK (file_selection_changed_cb), 
			  window);
	g_signal_connect (G_OBJECT (file_list->view),
			  "cursor_changed",
			  G_CALLBACK (gth_file_list_cursor_changed_cb), 
			  window);
	g_signal_connect (G_OBJECT (file_list->view),
			  "item_activated",
			  G_CALLBACK (gth_file_list_item_activated_cb), 
			  window);

	/* FIXME
	g_signal_connect_after (G_OBJECT (view_widget),
				"button_press_event",
				G_CALLBACK (gth_file_list_button_press_cb), 
				window);
	*/

	g_signal_connect (G_OBJECT (view_widget),
			  "button_press_event",
			  G_CALLBACK (gth_file_list_button_press_cb), 
			  window);

	g_signal_connect (G_OBJECT (file_list->root_widget),
			  "drag_data_received",
			  G_CALLBACK (image_list_drag_data_received), 
			  window);
	g_signal_connect (G_OBJECT (file_list->root_widget),
			  "drag_begin",
			  G_CALLBACK (gth_file_list_drag_begin), 
			  window);
	g_signal_connect (G_OBJECT (file_list->root_widget),
			  "drag_end",
			  G_CALLBACK (gth_file_list_drag_end), 
			  window);
	g_signal_connect (G_OBJECT (file_list->root_widget),
			  "drag_data_get",
			  G_CALLBACK (gth_file_list_drag_data_get), 
			  window);

	g_signal_connect (G_OBJECT (file_list),
			  "busy",
			  G_CALLBACK (file_list_busy_cb),
			  window);
	g_signal_connect (G_OBJECT (file_list),
			  "idle",
			  G_CALLBACK (file_list_idle_cb),
			  window);

	return file_list;
}


static void
pref_view_as_changed (GConfClient *client,
		      guint        cnxn_id,
		      GConfEntry  *entry,
		      gpointer     user_data)
{
	GThumbWindow  *window = user_data;
	GthFileList   *file_list;
	GthSortMethod  sort_method;
	GtkSortType    sort_type;
	gboolean       enable_thumbs;

	sort_method = window->file_list->sort_method;
	sort_type = window->file_list->sort_type;
	enable_thumbs = window->file_list->enable_thumbs;

	/**/

	file_list = create_new_file_list (window);

	gtk_widget_destroy (window->file_list->root_widget);
	gtk_box_pack_start (GTK_BOX (window->file_list_pane), file_list->root_widget, TRUE, TRUE, 0);

	/*
	if (window->layout_type <= 1) {
		gtk_widget_destroy (GTK_PANED (window->content_pane)->child2);
		GTK_PANED (window->content_pane)->child2 = NULL;
		gtk_paned_pack2 (GTK_PANED (window->content_pane), file_list->root_widget, TRUE, FALSE);
	} else if (window->layout_type == 2) {
		gtk_widget_destroy (GTK_PANED (window->main_pane)->child2);
		GTK_PANED (window->main_pane)->child2 = NULL;
		gtk_paned_pack2 (GTK_PANED (window->main_pane), file_list->root_widget, TRUE, FALSE);
	} else if (window->layout_type == 3) {
		gtk_widget_destroy (GTK_PANED (window->content_pane)->child1);
		GTK_PANED (window->content_pane)->child1 = NULL;
		gtk_paned_pack1 (GTK_PANED (window->content_pane), file_list->root_widget, FALSE, FALSE);
	}
	*/

	gth_file_list_set_sort_method (file_list, sort_method);
	gth_file_list_set_sort_type (file_list, sort_type);
	gth_file_list_enable_thumbs (file_list, enable_thumbs);

	g_object_unref (window->file_list);
	window->file_list = file_list;

	gtk_widget_show_all (window->file_list->root_widget);
	window_update_file_list (window);
}


void
window_set_preview_content (GThumbWindow      *window,
			    GthPreviewContent  content)
{
	GtkWidget *widget_to_focus = window->viewer;

	window->preview_content = content;

	set_button_active_no_notify (window, window->preview_button_image, FALSE);
	set_button_active_no_notify (window, window->preview_button_data, FALSE);
	set_button_active_no_notify (window, window->preview_button_comment, FALSE);

	if (window->preview_content == GTH_PREVIEW_CONTENT_IMAGE) {
		gtk_widget_hide (window->preview_widget_data);
		gtk_widget_hide (window->preview_widget_comment);
		gtk_widget_hide (window->preview_widget_data_comment);
		gtk_widget_show (window->preview_widget_image);
		set_button_active_no_notify (window, window->preview_button_image, TRUE);

	} else if (window->preview_content == GTH_PREVIEW_CONTENT_DATA) {
		gtk_widget_hide (window->preview_widget_image);
		gtk_widget_hide (window->preview_widget_comment);
		gtk_widget_show (window->preview_widget_data);
		gtk_widget_show (window->preview_widget_data_comment);
		set_button_active_no_notify (window, window->preview_button_data, TRUE);

	} else if (window->preview_content == GTH_PREVIEW_CONTENT_COMMENT) {
		gtk_widget_hide (window->preview_widget_image);
		gtk_widget_hide (window->preview_widget_data);
		gtk_widget_show (window->preview_widget_comment);
		gtk_widget_show (window->preview_widget_data_comment);
		set_button_active_no_notify (window, window->preview_button_comment, TRUE);
	}

	switch (window->preview_content) {
	case GTH_PREVIEW_CONTENT_IMAGE:
		widget_to_focus = window->viewer;
		break;
	case GTH_PREVIEW_CONTENT_DATA:
		widget_to_focus = gth_exif_data_viewer_get_view (GTH_EXIF_DATA_VIEWER (window->exif_data_viewer));
		break;
	case GTH_PREVIEW_CONTENT_COMMENT:
		widget_to_focus = window->image_comment;
		break;
	}

	gtk_widget_grab_focus (widget_to_focus);

	window_update_statusbar_zoom_info (window);
	window_update_sensitivity (window);
}


static void
close_preview_image_button_cb (GtkToggleButton *button,
			       GThumbWindow    *window)
{
	if (window->sidebar_visible) {
		if (window->image_pane_visible)
			window_hide_image_pane (window);
	} else 
		window_show_sidebar (window);
}


static void
preview_button_cb (GThumbWindow      *window,
		   GtkToggleButton   *button,
		   GthPreviewContent  content)
{
	if (gtk_toggle_button_get_active (button)) 
		window_set_preview_content (window, content);
	else if (window->preview_content == content) 
		gtk_toggle_button_set_active (button, TRUE);
}


static void
preview_image_button_cb (GtkToggleButton *button,
			 GThumbWindow    *window)
{
	preview_button_cb (window, button, GTH_PREVIEW_CONTENT_IMAGE);
}


static void
preview_data_button_cb (GtkToggleButton *button,
			GThumbWindow    *window)
{
	if (window->sidebar_visible) {
		preview_button_cb (window, button, GTH_PREVIEW_CONTENT_DATA);
	} else {
		if (gtk_toggle_button_get_active (button))
			window_show_image_data (window);
		else
			window_hide_image_data (window);
	}
}


static void
preview_comment_button_cb (GtkToggleButton *button,
			   GThumbWindow    *window)
{
	preview_button_cb (window, button, GTH_PREVIEW_CONTENT_COMMENT);
}


static gboolean
initial_location_cb (gpointer data)
{
	GThumbWindow *window = data;
	const char   *starting_location = preferences_get_startup_location ();

	if (pref_util_location_is_catalog (starting_location)) 
		window_go_to_catalog (window, pref_util_get_catalog_location (starting_location));

	else {
		const char *path;

		if (pref_util_location_is_file (starting_location))
			path = pref_util_get_file_location (starting_location);

		else {  /* we suppose it is a directory name without prefix. */
			path = starting_location;
			if (! path_is_dir (path))
				return FALSE;
		}

		window_go_to_directory (window, path);
	}

	gtk_widget_grab_focus (gth_file_view_get_widget (window->file_list->view));

	return FALSE;
}


static void
menu_item_select_cb (GtkMenuItem  *proxy,
                     GThumbWindow *window)
{
        GtkAction *action;
        char      *message;
	
        action = g_object_get_data (G_OBJECT (proxy),  "gtk-action");
        g_return_if_fail (action != NULL);
	
        g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
        if (message) {
		gtk_statusbar_push (GTK_STATUSBAR (window->statusbar),
				    window->help_message_cid, message);
		g_free (message);
        }
}


static void
menu_item_deselect_cb (GtkMenuItem  *proxy,
		       GThumbWindow *window)
{
        gtk_statusbar_pop (GTK_STATUSBAR (window->statusbar),
                           window->help_message_cid);
}


static void
disconnect_proxy_cb (GtkUIManager *manager,
		     GtkAction    *action,
		     GtkWidget    *proxy,
		     GThumbWindow *window)
{
        if (GTK_IS_MENU_ITEM (proxy)) {
                g_signal_handlers_disconnect_by_func
                        (proxy, G_CALLBACK (menu_item_select_cb), window);
                g_signal_handlers_disconnect_by_func
                        (proxy, G_CALLBACK (menu_item_deselect_cb), window);
        }
}


static void
connect_proxy_cb (GtkUIManager *manager,
		  GtkAction    *action,
		  GtkWidget    *proxy,
                  GThumbWindow *window)
{
        if (GTK_IS_MENU_ITEM (proxy)) {
		g_signal_connect (proxy, "select",
				  G_CALLBACK (menu_item_select_cb), window);
		g_signal_connect (proxy, "deselect",
				  G_CALLBACK (menu_item_deselect_cb), window);
	}
}


static void
sort_by_radio_action (GtkAction      *action,
		      GtkRadioAction *current,
		      gpointer        data)
{
	GThumbWindow *window = data;
	gth_file_list_set_sort_method (window->file_list, gtk_radio_action_get_current_value (current));
}


static void
zoom_quality_radio_action (GtkAction      *action,
			   GtkRadioAction *current,
			   gpointer        data)
{
	GThumbWindow   *window = data;
	GthZoomQuality  quality;

	quality = gtk_radio_action_get_current_value (current);

	gtk_radio_action_get_current_value (current);
	image_viewer_set_zoom_quality (IMAGE_VIEWER (window->viewer), quality);
	image_viewer_update_view (IMAGE_VIEWER (window->viewer));
	pref_set_zoom_quality (quality);
}


static void
content_radio_action (GtkAction      *action,
		      GtkRadioAction *current,
		      gpointer        data)
{
	GThumbWindow      *window = data;
	GthSidebarContent  content = gtk_radio_action_get_current_value (current);

	if (window->toolbar_merge_id != 0)
		gtk_ui_manager_remove_ui (window->ui, window->toolbar_merge_id);
	gtk_ui_manager_ensure_update (window->ui);

	if (content != GTH_SIDEBAR_NO_LIST) {
		window_set_sidebar_content (window, content);
		window->toolbar_merge_id = gtk_ui_manager_add_ui_from_string (window->ui, browser_ui_info, -1, NULL);
		gtk_ui_manager_ensure_update (window->ui);
	} else {
		window_hide_sidebar (window);
		window->toolbar_merge_id = gtk_ui_manager_add_ui_from_string (window->ui, viewer_ui_info, -1, NULL);
		gtk_ui_manager_ensure_update (window->ui);
	}

	gtk_widget_queue_resize (window->toolbar->parent);
}


static GtkWidget*
create_locationbar_button (const char *stock_id,
                           gboolean    view_text)
{
        GtkWidget    *button;
        GtkWidget    *box;
        GtkWidget    *image;

        button = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);

        box = gtk_hbox_new (FALSE, 1);
        image = gtk_image_new ();
        gtk_image_set_from_stock (GTK_IMAGE (image),
                                  stock_id,
                                  GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_box_pack_start (GTK_BOX (box), image, !view_text, FALSE, 0);

        if (view_text) {
                GtkStockItem  stock_item;
                const char   *text;
                GtkWidget    *label;
                if (gtk_stock_lookup (stock_id, &stock_item))
                        text = stock_item.label;
                else
                        text = "";
                label = gtk_label_new_with_mnemonic (text);
                gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
        }

        gtk_container_add (GTK_CONTAINER (button), box);

        return button;
}


static gboolean 
window_show_cb (GtkWidget    *widget,
		GThumbWindow *window)
{
	if (eel_gconf_get_boolean (PREF_UI_TOOLBAR_VISIBLE, TRUE))
		gtk_widget_show (window->toolbar->parent);
	else
		gtk_widget_hide (window->toolbar->parent);
	set_action_active (window, "View_Toolbar", eel_gconf_get_boolean (PREF_UI_TOOLBAR_VISIBLE, TRUE));

	if (eel_gconf_get_boolean (PREF_UI_STATUSBAR_VISIBLE, TRUE))
		gtk_widget_show (window->statusbar);
	else
		gtk_widget_hide (window->statusbar);
	set_action_active (window, "View_Statusbar", eel_gconf_get_boolean (PREF_UI_STATUSBAR_VISIBLE, TRUE));

	return TRUE;
}





GThumbWindow *
window_new (void)
{
	GThumbWindow      *window;
	GtkWidget         *paned1;      /* Main paned widget. */
	GtkWidget         *paned2;      /* Secondary paned widget. */
	GtkWidget         *table;
	GtkWidget         *frame;
	GtkWidget         *image_vbox;
	GtkWidget         *dir_list_vbox;
	GtkWidget         *info_frame;
	GtkWidget         *info_hbox;
	GtkWidget         *image_pane_paned1;
	GtkWidget         *image_pane_paned2;
	GtkWidget         *scrolled_win; 
	GtkTreeSelection  *selection;
	int                i; 
	GtkActionGroup    *actions;
	GtkUIManager      *ui;
	GError            *error = NULL;
	GtkWidget         *toolbar;

	window = g_new0 (GThumbWindow, 1);

	window->app = gnome_app_new ("main", _("gThumb"));
	gnome_window_icon_set_from_default (GTK_WINDOW (window->app));
	gtk_window_set_default_size (GTK_WINDOW (window->app), 
				     eel_gconf_get_integer (PREF_UI_WINDOW_WIDTH, DEF_WIN_WIDTH),
				     eel_gconf_get_integer (PREF_UI_WINDOW_HEIGHT, DEF_WIN_HEIGHT));

	window->tooltips = gtk_tooltips_new ();

	/* Build the menu and the toolbar. */

	window->actions = actions = gtk_action_group_new ("Actions");
	gtk_action_group_set_translation_domain (actions, NULL);
	gtk_action_group_add_actions (actions, 
				      action_entries, 
				      n_action_entries, 
				      window);
	gtk_action_group_add_toggle_actions (actions, 
					     action_toggle_entries, 
					     n_action_toggle_entries, 
					     window);
	gtk_action_group_add_radio_actions (actions, 
					    sort_by_entries, 
					    n_sort_by_entries,
					    GTH_SORT_METHOD_BY_NAME,
					    G_CALLBACK (sort_by_radio_action), 
					    window);
	gtk_action_group_add_radio_actions (actions, 
					    zoom_quality_entries, 
					    n_zoom_quality_entries,
					    GTH_ZOOM_QUALITY_HIGH,
					    G_CALLBACK (zoom_quality_radio_action), 
					    window);
	gtk_action_group_add_radio_actions (actions, 
					    content_entries, 
					    n_content_entries,
					    GTH_SIDEBAR_DIR_LIST,
					    G_CALLBACK (content_radio_action), 
					    window);
	window->ui = ui = gtk_ui_manager_new ();

	g_signal_connect (ui, "connect_proxy",
			  G_CALLBACK (connect_proxy_cb), window);
	g_signal_connect (ui, "disconnect_proxy",
			  G_CALLBACK (disconnect_proxy_cb), window);

	gtk_ui_manager_insert_action_group (ui, actions, 0);
	gtk_window_add_accel_group (GTK_WINDOW (window->app), 
				    gtk_ui_manager_get_accel_group (ui));
	
	if (!gtk_ui_manager_add_ui_from_string (ui, main_ui_info, -1, &error)) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}

	gnome_app_add_docked (GNOME_APP (window->app),
			      gtk_ui_manager_get_widget (ui, "/MenuBar"),
			      "MenuBar",
			      (BONOBO_DOCK_ITEM_BEH_NEVER_VERTICAL 
			       | BONOBO_DOCK_ITEM_BEH_EXCLUSIVE 
			       | (eel_gconf_get_boolean (PREF_DESKTOP_MENUBAR_DETACHABLE, TRUE) ? BONOBO_DOCK_ITEM_BEH_NORMAL : BONOBO_DOCK_ITEM_BEH_LOCKED)),
			      BONOBO_DOCK_TOP,
			      1, 1, 0);

	window->toolbar = toolbar = gtk_ui_manager_get_widget (ui, "/ToolBar");
	gtk_toolbar_set_show_arrow (GTK_TOOLBAR (toolbar), TRUE);

	set_action_important (window, "/ToolBar/View_ShowFolders", TRUE);
	set_action_important (window, "/ToolBar/View_ShowCatalogs", TRUE);
	set_action_important (window, "/ToolBar/View_ShowImage", TRUE);
	/* FIXME
	set_action_important (window, "/ToolBar/ModeCommands/Tools_Slideshow", TRUE);
	set_action_important (window, "/ToolBar/ModeCommands/View_Fullscreen", TRUE);
	*/

	gnome_app_add_docked (GNOME_APP (window->app),
			      toolbar,
			      "ToolBar",
			      (BONOBO_DOCK_ITEM_BEH_NEVER_VERTICAL 
			       | BONOBO_DOCK_ITEM_BEH_EXCLUSIVE 
			       | (eel_gconf_get_boolean (PREF_DESKTOP_TOOLBAR_DETACHABLE, TRUE) ? BONOBO_DOCK_ITEM_BEH_NORMAL : BONOBO_DOCK_ITEM_BEH_LOCKED)),
			      BONOBO_DOCK_TOP,
			      2, 1, 0);

	window->statusbar = gtk_statusbar_new ();
	window->help_message_cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->statusbar), "help_message");
	window->list_info_cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->statusbar), "list_info");
	gnome_app_set_statusbar (GNOME_APP (window->app), window->statusbar);
	gtk_statusbar_set_has_resize_grip (GTK_STATUSBAR (window->statusbar), TRUE);

	g_signal_connect (G_OBJECT (window->app), 
			  "delete_event",
			  G_CALLBACK (close_window_cb),
			  window);
	g_signal_connect (G_OBJECT (window->app), 
			  "key_press_event",
			  G_CALLBACK (key_press_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->app), 
			  "show",
			  G_CALLBACK (window_show_cb),
			  window);

	/* Popup menus */

	window->file_popup_menu = gtk_ui_manager_get_widget (ui, "/FilePopup");
	window->image_popup_menu = gtk_ui_manager_get_widget (ui, "/ImagePopup");
	window->fullscreen_image_popup_menu = gtk_ui_manager_get_widget (ui, "/FullscreenImagePopup");
	window->catalog_popup_menu = gtk_ui_manager_get_widget (ui, "/CatalogPopup");
	window->library_popup_menu = gtk_ui_manager_get_widget (ui, "/LibraryPopup");
	window->dir_popup_menu = gtk_ui_manager_get_widget (ui, "/DirPopup");
	window->dir_list_popup_menu = gtk_ui_manager_get_widget (ui, "/DirListPopup");
	window->catalog_list_popup_menu = gtk_ui_manager_get_widget (ui, "/CatalogListPopup");
	window->history_list_popup_menu = gtk_ui_manager_get_widget (ui, "/HistoryListPopup");

	/* Create the widgets. */

	window->file_list = create_new_file_list (window);

	/* Dir list. */

	window->dir_list = dir_list_new ();
	gtk_drag_dest_set (window->dir_list->root_widget,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, G_N_ELEMENTS (target_table),
			   GDK_ACTION_MOVE);
	gtk_drag_source_set (window->dir_list->list_view,
			     GDK_BUTTON1_MASK,
			     target_table, G_N_ELEMENTS (target_table),
			     GDK_ACTION_MOVE | GDK_ACTION_COPY);
	g_signal_connect (G_OBJECT (window->dir_list->root_widget),
			  "drag_data_received",
			  G_CALLBACK (dir_list_drag_data_received), 
			  window);
	g_signal_connect (G_OBJECT (window->dir_list->list_view), 
			  "drag_begin",
			  G_CALLBACK (dir_list_drag_begin), 
			  window);
	g_signal_connect (G_OBJECT (window->dir_list->list_view), 
			  "drag_end",
			  G_CALLBACK (dir_list_drag_end), 
			  window);
	g_signal_connect (G_OBJECT (window->dir_list->root_widget), 
			  "drag_motion",
			  G_CALLBACK (dir_list_drag_motion), 
			  window);
	g_signal_connect (G_OBJECT (window->dir_list->root_widget), 
			  "drag_leave",
			  G_CALLBACK (dir_list_drag_leave), 
			  window);
	g_signal_connect (G_OBJECT (window->dir_list->list_view),
			  "drag_data_get",
			  G_CALLBACK (dir_list_drag_data_get), 
			  window);
	g_signal_connect (G_OBJECT (window->dir_list->list_view), 
			  "button_press_event",
			  G_CALLBACK (dir_list_button_press_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->dir_list->list_view), 
			  "button_release_event",
			  G_CALLBACK (dir_list_button_release_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->dir_list->list_view),
                          "row_activated",
                          G_CALLBACK (dir_list_activated_cb),
                          window);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->dir_list->list_view));
	g_signal_connect (G_OBJECT (selection),
                          "changed",
                          G_CALLBACK (dir_or_catalog_sel_changed_cb),
                          window);

	/* Catalog list. */

	window->catalog_list = catalog_list_new (pref_get_real_click_policy () == GTH_CLICK_POLICY_SINGLE);
	gtk_drag_dest_set (window->catalog_list->root_widget,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, G_N_ELEMENTS(target_table),
			   GDK_ACTION_MOVE);
	g_signal_connect (G_OBJECT (window->catalog_list->root_widget),
			  "drag_data_received",
			  G_CALLBACK (catalog_list_drag_data_received), 
			  window);
	g_signal_connect (G_OBJECT (window->catalog_list->list_view), 
			  "drag_begin",
			  G_CALLBACK (catalog_list_drag_begin), 
			  window);
	g_signal_connect (G_OBJECT (window->catalog_list->root_widget), 
			  "drag_motion",
			  G_CALLBACK (catalog_list_drag_motion), 
			  window);
	g_signal_connect (G_OBJECT (window->catalog_list->root_widget), 
			  "drag_leave",
			  G_CALLBACK (catalog_list_drag_leave), 
			  window);
	g_signal_connect (G_OBJECT (window->catalog_list->list_view), 
			  "button_press_event",
			  G_CALLBACK (catalog_list_button_press_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->catalog_list->list_view), 
			  "button_release_event",
			  G_CALLBACK (catalog_list_button_release_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->catalog_list->list_view),
                          "row_activated",
                          G_CALLBACK (catalog_list_activated_cb),
                          window);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->catalog_list->list_view));
	g_signal_connect (G_OBJECT (selection),
                          "changed",
                          G_CALLBACK (dir_or_catalog_sel_changed_cb),
                          window);

	/* Location entry. */

	window->location_entry = gtk_entry_new ();
	g_signal_connect (G_OBJECT (window->location_entry),
			  "key_press_event",
			  G_CALLBACK (location_entry_key_press_cb),
			  window);

	setup_toolbar_go_back_button (window);

	window->go_fwd_toolbar_button = create_locationbar_button (GTK_STOCK_GO_FORWARD, FALSE);
	gtk_tooltips_set_tip (window->tooltips, window->go_fwd_toolbar_button, _("Go to the next visited location"), NULL);
	g_signal_connect (G_OBJECT (window->go_fwd_toolbar_button),
			  "clicked",
			  G_CALLBACK (activate_action_go_forward),
			  window);

	window->go_up_toolbar_button = create_locationbar_button (GTK_STOCK_GO_UP, FALSE);
	gtk_tooltips_set_tip (window->tooltips, window->go_up_toolbar_button, _("Go up one level"), NULL);
	g_signal_connect (G_OBJECT (window->go_up_toolbar_button),
			  "clicked",
			  G_CALLBACK (activate_action_go_up),
			  window);

	window->go_home_toolbar_button = create_locationbar_button (GTK_STOCK_HOME, FALSE);
	gtk_tooltips_set_tip (window->tooltips, window->go_home_toolbar_button, _("Go to the home folder"), NULL);
	g_signal_connect (G_OBJECT (window->go_home_toolbar_button),
			  "clicked",
			  G_CALLBACK (activate_action_go_home),
			  window);

	/* Info bar. */

	window->info_bar = gthumb_info_bar_new ();
	gthumb_info_bar_set_focused (GTHUMB_INFO_BAR (window->info_bar), FALSE);
	g_signal_connect (G_OBJECT (window->info_bar), 
			  "button_press_event",
			  G_CALLBACK (info_bar_clicked_cb), 
			  window);

	/* Image viewer. */
	
	window->viewer = image_viewer_new ();
	gtk_widget_set_size_request (window->viewer, 
				     PANE_MIN_SIZE,
				     PANE_MIN_SIZE);

	/* FIXME 
	gtk_drag_source_set (window->viewer,
			     GDK_BUTTON2_MASK,
			     target_table, G_N_ELEMENTS(target_table), 
			     GDK_ACTION_MOVE);
	*/

	gtk_drag_dest_set (window->viewer,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, G_N_ELEMENTS(target_table),
			   GDK_ACTION_MOVE);

	g_signal_connect (G_OBJECT (window->viewer), 
			  "image_loaded",
			  G_CALLBACK (image_loaded_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->viewer), 
			  "zoom_changed",
			  G_CALLBACK (zoom_changed_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->viewer), 
			  "size_changed",
			  G_CALLBACK (size_changed_cb), 
			  window);
	g_signal_connect_after (G_OBJECT (window->viewer), 
				"button_press_event",
				G_CALLBACK (image_button_press_cb), 
				window);
	g_signal_connect_after (G_OBJECT (window->viewer), 
				"button_release_event",
				G_CALLBACK (image_button_release_cb), 
				window);
	g_signal_connect_after (G_OBJECT (window->viewer), 
				"clicked",
				G_CALLBACK (image_clicked_cb), 
				window);
	g_signal_connect (G_OBJECT (window->viewer), 
			  "focus_in_event",
			  G_CALLBACK (image_focus_changed_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->viewer), 
			  "focus_out_event",
			  G_CALLBACK (image_focus_changed_cb), 
			  window);

	g_signal_connect (G_OBJECT (window->viewer),
			  "drag_data_get",
			  G_CALLBACK (viewer_drag_data_get), 
			  window);

	g_signal_connect (G_OBJECT (window->viewer), 
			  "drag_data_received",
			  G_CALLBACK (viewer_drag_data_received), 
			  window);

	g_signal_connect (G_OBJECT (window->viewer),
			  "key_press_event",
			  G_CALLBACK (viewer_key_press_cb),
			  window);

	g_signal_connect (G_OBJECT (IMAGE_VIEWER (window->viewer)->loader), 
			  "image_progress",
			  G_CALLBACK (image_loader_progress_cb), 
			  window);
	g_signal_connect (G_OBJECT (IMAGE_VIEWER (window->viewer)->loader), 
			  "image_done",
			  G_CALLBACK (image_loader_done_cb), 
			  window);
	g_signal_connect (G_OBJECT (IMAGE_VIEWER (window->viewer)->loader), 
			  "image_error",
			  G_CALLBACK (image_loader_done_cb), 
			  window);

	window->viewer_vscr = gtk_vscrollbar_new (IMAGE_VIEWER (window->viewer)->vadj);
	window->viewer_hscr = gtk_hscrollbar_new (IMAGE_VIEWER (window->viewer)->hadj);
	window->viewer_event_box = gtk_event_box_new ();
	gtk_container_add (GTK_CONTAINER (window->viewer_event_box), _gtk_image_new_from_xpm_data (nav_button_xpm));

	g_signal_connect (G_OBJECT (window->viewer_event_box), 
			  "button_press_event",
			  G_CALLBACK (nav_button_clicked_cb), 
			  window->viewer);

	/* Pack the widgets */

	window->file_list_pane = gtk_vbox_new (0, FALSE);
	gtk_widget_show_all (window->file_list->root_widget);
	gtk_box_pack_start (GTK_BOX (window->file_list_pane), window->file_list->root_widget, TRUE, TRUE, 0);

	window->layout_type = eel_gconf_get_integer (PREF_UI_LAYOUT, 2);

	if (window->layout_type == 1) {
		window->main_pane = paned1 = gtk_vpaned_new (); 
		window->content_pane = paned2 = gtk_hpaned_new ();
	} else {
		window->main_pane = paned1 = gtk_hpaned_new (); 
		window->content_pane = paned2 = gtk_vpaned_new (); 
	}

	gnome_app_set_contents (GNOME_APP (window->app), 
				window->main_pane);

	if (window->layout_type == 3)
		gtk_paned_pack2 (GTK_PANED (paned1), paned2, TRUE, FALSE);
	else
		gtk_paned_pack1 (GTK_PANED (paned1), paned2, FALSE, FALSE);

	window->notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (window->notebook), FALSE);
	gtk_notebook_append_page (GTK_NOTEBOOK (window->notebook), 
				  window->dir_list->root_widget,
				  NULL);
	gtk_notebook_append_page (GTK_NOTEBOOK (window->notebook), 
				  window->catalog_list->root_widget,
				  NULL);

	window->dir_list_pane = dir_list_vbox = gtk_vbox_new (FALSE, 3);

	gtk_box_pack_start (GTK_BOX (dir_list_vbox), window->location_entry, 
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (dir_list_vbox), window->notebook, 
			    TRUE, TRUE, 0);

	if (window->layout_type == 3) 
		gtk_paned_pack1 (GTK_PANED (paned1), dir_list_vbox, FALSE, FALSE);
	else 
		gtk_paned_pack1 (GTK_PANED (paned2), dir_list_vbox, FALSE, FALSE);

	if (window->layout_type <= 1) 
		gtk_paned_pack2 (GTK_PANED (paned2), window->file_list_pane, TRUE, FALSE);
	else if (window->layout_type == 2)
		gtk_paned_pack2 (GTK_PANED (paned1), window->file_list_pane, TRUE, FALSE);
	else if (window->layout_type == 3)
		gtk_paned_pack1 (GTK_PANED (paned2), window->file_list_pane, FALSE, FALSE);

	/**/

	image_vbox = window->image_pane = gtk_vbox_new (FALSE, 0);

	if (window->layout_type <= 1)
		gtk_paned_pack2 (GTK_PANED (paned1), image_vbox, TRUE, FALSE);
	else
		gtk_paned_pack2 (GTK_PANED (paned2), image_vbox, TRUE, FALSE);

	/* image info bar */

	info_frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (info_frame), GTK_SHADOW_NONE);
	gtk_box_pack_start (GTK_BOX (image_vbox), info_frame, FALSE, FALSE, 0);

	info_hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (info_frame), info_hbox);

	gtk_box_pack_start (GTK_BOX (info_hbox), window->info_bar, TRUE, TRUE, 0);

	/* FIXME */
	{
		GtkWidget *button;
		GtkWidget *image;

		button = gtk_button_new ();
		image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
		gtk_container_add (GTK_CONTAINER (button), image);
		gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
		gtk_box_pack_end (GTK_BOX (info_hbox), button, FALSE, FALSE, 0);
		g_signal_connect (G_OBJECT (button), 
				  "clicked",
				  G_CALLBACK (close_preview_image_button_cb), 
				  window);
		gtk_tooltips_set_tip (window->tooltips,
				      button,
				      _("Close"),
				      NULL);

		image = _gtk_image_new_from_inline (preview_comment_16_rgba);
		window->preview_button_comment = button = gtk_toggle_button_new ();
		gtk_container_add (GTK_CONTAINER (button), image);
		gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
		gtk_box_pack_end (GTK_BOX (info_hbox), button, FALSE, FALSE, 0);
		g_signal_connect (G_OBJECT (button), 
				  "toggled",
				  G_CALLBACK (preview_comment_button_cb), 
				  window);
		gtk_tooltips_set_tip (window->tooltips,
				      button,
				      _("Image comment"),
				      NULL);

		image = _gtk_image_new_from_inline (preview_data_16_rgba);
		window->preview_button_data = button = gtk_toggle_button_new ();
		gtk_container_add (GTK_CONTAINER (button), image);
		gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
		gtk_box_pack_end (GTK_BOX (info_hbox), button, FALSE, FALSE, 0);
		g_signal_connect (G_OBJECT (button), 
				  "toggled",
				  G_CALLBACK (preview_data_button_cb), 
				  window);
		gtk_tooltips_set_tip (window->tooltips,
				      button,
				      _("Image data"),
				      NULL);

		image = _gtk_image_new_from_inline (preview_image_16_rgba);
		window->preview_button_image = button = gtk_toggle_button_new ();
		gtk_container_add (GTK_CONTAINER (button), image);
		gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
		gtk_box_pack_end (GTK_BOX (info_hbox), button, FALSE, FALSE, 0);
		g_signal_connect (G_OBJECT (button), 
				  "toggled",
				  G_CALLBACK (preview_image_button_cb), 
				  window);
		gtk_tooltips_set_tip (window->tooltips,
				      button,
				      _("Image preview"),
				      NULL);
	}

	/* image preview, comment, exif data. */

	image_pane_paned1 = gtk_vpaned_new ();
	gtk_paned_set_position (GTK_PANED (image_pane_paned1), eel_gconf_get_integer (PREF_UI_WINDOW_HEIGHT, DEF_WIN_HEIGHT) / 2);
	gtk_box_pack_start (GTK_BOX (image_vbox), image_pane_paned1, TRUE, TRUE, 0);

	/**/

	window->viewer_container = frame = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (frame), window->viewer);

	window->preview_widget_image = table = gtk_table_new (2, 2, FALSE);
	gtk_paned_pack1 (GTK_PANED (image_pane_paned1), table, FALSE, FALSE);

	gtk_table_attach (GTK_TABLE (table), frame, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	gtk_table_attach (GTK_TABLE (table), window->viewer_vscr, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	gtk_table_attach (GTK_TABLE (table), window->viewer_hscr, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_table_attach (GTK_TABLE (table), window->viewer_event_box, 1, 2, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);

	/**/

	window->preview_widget_data_comment = image_pane_paned2 = gtk_hpaned_new ();
	gtk_paned_set_position (GTK_PANED (image_pane_paned2), eel_gconf_get_integer (PREF_UI_WINDOW_WIDTH, DEF_WIN_WIDTH) / 2);
	gtk_paned_pack2 (GTK_PANED (image_pane_paned1), image_pane_paned2, TRUE, FALSE);

	window->preview_widget_comment = scrolled_win = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_win), GTK_SHADOW_ETCHED_IN);

	window->image_comment = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (window->image_comment), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (window->image_comment), GTK_WRAP_WORD);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (window->image_comment), TRUE);
	gtk_container_add (GTK_CONTAINER (scrolled_win), window->image_comment);

	gtk_paned_pack2 (GTK_PANED (image_pane_paned2), scrolled_win, TRUE, FALSE);

	g_signal_connect (G_OBJECT (window->image_comment), 
			  "focus_in_event",
			  G_CALLBACK (image_focus_changed_cb), 
			  window);
	g_signal_connect (G_OBJECT (window->image_comment), 
			  "focus_out_event",
			  G_CALLBACK (image_focus_changed_cb), 
			  window);

	g_signal_connect (G_OBJECT (window->image_comment), 
			  "button_press_event",
			  G_CALLBACK (image_comment_button_press_cb), 
			  window);

	/* exif data */

	window->preview_widget_data = window->exif_data_viewer = gth_exif_data_viewer_new (TRUE);
	gtk_paned_pack1 (GTK_PANED (image_pane_paned2), window->exif_data_viewer, FALSE, FALSE);

	g_signal_connect (G_OBJECT (gth_exif_data_viewer_get_view (GTH_EXIF_DATA_VIEWER (window->exif_data_viewer))), 
			  "focus_in_event",
			  G_CALLBACK (image_focus_changed_cb), 
			  window);
	g_signal_connect (G_OBJECT (gth_exif_data_viewer_get_view (GTH_EXIF_DATA_VIEWER (window->exif_data_viewer))), 
			  "focus_out_event",
			  G_CALLBACK (image_focus_changed_cb), 
			  window);

	/* Progress bar. */

	window->progress = gtk_progress_bar_new ();
	gtk_widget_show (window->progress);
	gtk_box_pack_start (GTK_BOX (window->statusbar), window->progress, FALSE, FALSE, 0);

	/* Zoom info */

	window->zoom_info = gtk_label_new (NULL);
	gtk_widget_show (window->zoom_info);

	window->zoom_info_frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (window->zoom_info_frame), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (window->zoom_info_frame), window->zoom_info);
	gtk_box_pack_start (GTK_BOX (window->statusbar), window->zoom_info_frame, FALSE, FALSE, 0);

	/* Image info */

	window->image_info = gtk_label_new (NULL);
	gtk_widget_show (window->image_info);

	window->image_info_frame = info_frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (info_frame), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (info_frame), window->image_info);
	gtk_box_pack_start (GTK_BOX (window->statusbar), info_frame, FALSE, FALSE, 0);

	/* Progress dialog */

	window->progress_gui = glade_xml_new (GTHUMB_GLADEDIR "/" GLADE_EXPORTER_FILE, NULL, NULL);
	if (! window->progress_gui) {
		window->progress_dialog = NULL;
		window->progress_progressbar = NULL;
		window->progress_info = NULL;
	} else {
		GtkWidget *cancel_button;

		window->progress_dialog = glade_xml_get_widget (window->progress_gui, "progress_dialog");
		window->progress_progressbar = glade_xml_get_widget (window->progress_gui, "progress_progressbar");
		window->progress_info = glade_xml_get_widget (window->progress_gui, "progress_info");
		cancel_button = glade_xml_get_widget (window->progress_gui, "progress_cancel");

		gtk_window_set_transient_for (GTK_WINDOW (window->progress_dialog), GTK_WINDOW (window->app));
		gtk_window_set_modal (GTK_WINDOW (window->progress_dialog), FALSE);

		g_signal_connect (G_OBJECT (cancel_button), 
				  "clicked",
				  G_CALLBACK (progress_cancel_cb), 
				  window);
		g_signal_connect (G_OBJECT (window->progress_dialog), 
				  "delete_event",
				  G_CALLBACK (progress_delete_cb),
				  window);
	}

	/* Update data. */

	window->sidebar_content = GTH_SIDEBAR_NO_LIST;
	window->sidebar_visible = TRUE;
	window->preview_visible = eel_gconf_get_boolean (PREF_SHOW_PREVIEW, TRUE);
	window->image_pane_visible = window->preview_visible;
	window->image_data_visible = eel_gconf_get_boolean (PREF_SHOW_IMAGE_DATA, FALSE);
	window->catalog_path = NULL;
	window->image_path = NULL;
	window->image_mtime = 0;
	window->image_catalog = NULL;
	window->image_modified = FALSE;

	window->fullscreen = FALSE;
	window->slideshow = FALSE;
	window->slideshow_set = NULL;
	window->slideshow_random_set = NULL;
	window->slideshow_first = NULL;
	window->slideshow_current = NULL;
	window->slideshow_timeout = 0;

	window->bookmarks_length = 0;
	window_update_bookmark_list (window);

	window->history = bookmarks_new (NULL);
	window->history_current = NULL;
	window->history_length = 0;
	window->go_op = WINDOW_GO_TO;
	window_update_history_list (window);

	window->activity_timeout = 0;
	window->activity_ref = 0;
	window->setting_file_list = FALSE;
	window->can_set_file_list = TRUE;
	window->changing_directory = FALSE;
	window->can_change_directory = TRUE;

	window->monitor_handle = NULL;
	window->monitor_enabled = FALSE;
	window->update_changes_timeout = 0;
	for (i = 0; i < MONITOR_EVENT_NUM; i++)
		window->monitor_events[i] = NULL;

	window->image_prop_dlg = NULL;

	window->busy_cursor_timeout = 0;

	window->view_image_timeout = 0;
	window->load_dir_timeout = 0;
	window->auto_load_timeout = 0;

	window->sel_change_timeout = 0;

	/* preloader */

	window->preloader = gthumb_preloader_new ();

	g_signal_connect (G_OBJECT (window->preloader), 
			  "requested_done",
			  G_CALLBACK (image_requested_done_cb), 
			  window);

	g_signal_connect (G_OBJECT (window->preloader), 
			  "requested_error",
			  G_CALLBACK (image_requested_error_cb), 
			  window);

	for (i = 0; i < GCONF_NOTIFICATIONS; i++)
		window->cnxn_id[i] = 0;

	window->pixop = NULL;

	/* Sync widgets and visualization options. */

	gtk_widget_realize (window->app);

	/**/

#ifndef HAVE_LIBGPHOTO
	set_action_sensitive (window, "File_CameraImport", FALSE);
#endif /* HAVE_LIBGPHOTO */

	/**/

	window->sidebar_width = eel_gconf_get_integer (PREF_UI_SIDEBAR_SIZE, DEF_SIDEBAR_SIZE);
	gtk_paned_set_position (GTK_PANED (paned1), eel_gconf_get_integer (PREF_UI_SIDEBAR_SIZE, DEF_SIDEBAR_SIZE));
	gtk_paned_set_position (GTK_PANED (paned2), eel_gconf_get_integer (PREF_UI_SIDEBAR_CONTENT_SIZE, DEF_SIDEBAR_CONT_SIZE));

	gtk_widget_show_all (window->main_pane);

	window_sync_menu_with_preferences (window); 

	if (window->preview_visible) 
		window_show_image_pane (window);
	else 
		window_hide_image_pane (window);

	window_set_preview_content (window, pref_get_preview_content ());

	window_notify_update_toolbar_style (window);
	window_update_statusbar_image_info (window);
	window_update_statusbar_zoom_info (window);

	image_viewer_set_zoom_quality (IMAGE_VIEWER (window->viewer),
				       pref_get_zoom_quality ());
	image_viewer_set_zoom_change  (IMAGE_VIEWER (window->viewer),
				       pref_get_zoom_change ());
	image_viewer_set_check_type   (IMAGE_VIEWER (window->viewer),
				       pref_get_check_type ());
	image_viewer_set_check_size   (IMAGE_VIEWER (window->viewer),
				       pref_get_check_size ());
	image_viewer_set_transp_type  (IMAGE_VIEWER (window->viewer),
				       pref_get_transp_type ());
	image_viewer_set_black_background (IMAGE_VIEWER (window->viewer),
					   eel_gconf_get_boolean (PREF_BLACK_BACKGROUND, FALSE));

	/* Add the window to the window's list */

	window_list = g_list_prepend (window_list, window);

	/* Add notification callbacks. */

	i = 0;

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_LAYOUT,
					   pref_ui_layout_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_TOOLBAR_STYLE,
					   pref_ui_toolbar_style_changed,
					   window);
	window->cnxn_id[i++] = eel_gconf_notification_add (
					   "/desktop/gnome/interface/toolbar_style",
					   pref_ui_toolbar_style_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_TOOLBAR_VISIBLE,
					   pref_ui_toolbar_visible_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_UI_STATUSBAR_VISIBLE,
					   pref_ui_statusbar_visible_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_SHOW_THUMBNAILS,
					   pref_show_thumbnails_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_SHOW_FILENAMES,
					   pref_show_filenames_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_SHOW_COMMENTS,
					   pref_show_comments_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_SHOW_HIDDEN_FILES,
					   pref_show_hidden_files_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_THUMBNAIL_SIZE,
					   pref_thumbnail_size_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_CLICK_POLICY,
					   pref_click_policy_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_ZOOM_QUALITY,
					   pref_zoom_quality_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_ZOOM_CHANGE,
					   pref_zoom_change_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_TRANSP_TYPE,
					   pref_transp_type_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_CHECK_TYPE,
					   pref_check_type_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_CHECK_SIZE,
					   pref_check_size_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_BLACK_BACKGROUND,
					   pref_black_background_changed,
					   window);

	window->cnxn_id[i++] = eel_gconf_notification_add (
					   PREF_VIEW_AS,
					   pref_view_as_changed,
					   window);

	window->toolbar_merge_id = gtk_ui_manager_add_ui_from_string (window->ui, browser_ui_info, -1, NULL);
	gtk_ui_manager_ensure_update (window->ui);	

	/* Initial location. */
	g_idle_add (initial_location_cb, window);

	return window;
}


static void
window_free (GThumbWindow *window)
{
	int i;

	g_return_if_fail (window != NULL);

	g_object_unref (window->file_list);
	dir_list_free (window->dir_list);
	catalog_list_free (window->catalog_list);

	if (window->catalog_path) {
		g_free (window->catalog_path);
		window->catalog_path = NULL;
	}

	if (window->image_path) {
		g_free (window->image_path);
		window->image_path = NULL;
	}

#ifdef HAVE_LIBEXIF
	if (window->exif_data != NULL) {
		exif_data_unref (window->exif_data);
		window->exif_data = NULL;
	}
#endif /* HAVE_LIBEXIF */

	if (window->new_image_path) {
		g_free (window->new_image_path);
		window->new_image_path = NULL;
	}

	if (window->image_catalog) {
		g_free (window->image_catalog);	
		window->image_catalog = NULL;
	}

	if (window->history) {
		bookmarks_free (window->history);
		window->history = NULL;
	}

	if (window->preloader) {
		g_object_unref (window->preloader);
		window->preloader = NULL;
	}

	for (i = 0; i < MONITOR_EVENT_NUM; i++)
		path_list_free (window->monitor_events[i]);

	g_free (window);
}


/* -- window_close -- */


static void
_window_remove_notifications (GThumbWindow *window)
{
	int i;

	for (i = 0; i < GCONF_NOTIFICATIONS; i++)
		if (window->cnxn_id[i] != 0)
			eel_gconf_notification_remove (window->cnxn_id[i]);
}


void
close__step6 (char     *filename,
	      gpointer  data)
{
	GThumbWindow   *window = data;
	ImageViewer    *viewer = IMAGE_VIEWER (window->viewer);
	gboolean        last_window;
	GdkWindowState  state;
	gboolean        maximized;

	last_window = window_list->next == NULL;

	/* Save visualization options only if the window is not maximized. */
	state = gdk_window_get_state (GTK_WIDGET (window->app)->window);
	maximized = (state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
	if (! maximized && GTK_WIDGET_VISIBLE (window->app)) {
		int width, height;

		gdk_drawable_get_size (window->app->window, &width, &height);
		eel_gconf_set_integer (PREF_UI_WINDOW_WIDTH, width);
		eel_gconf_set_integer (PREF_UI_WINDOW_HEIGHT, height);
	}

	if (window->sidebar_visible) {
		eel_gconf_set_integer (PREF_UI_SIDEBAR_SIZE, gtk_paned_get_position (GTK_PANED (window->main_pane)));
		eel_gconf_set_integer (PREF_UI_SIDEBAR_CONTENT_SIZE, gtk_paned_get_position (GTK_PANED (window->content_pane)));
	} else
		eel_gconf_set_integer (PREF_UI_SIDEBAR_SIZE, window->sidebar_width);

	if (last_window)
		eel_gconf_set_boolean (PREF_SHOW_THUMBNAILS, window->file_list->enable_thumbs);

	pref_set_arrange_type (window->file_list->sort_method);
	pref_set_sort_order (window->file_list->sort_type);

	if (last_window 
	    && eel_gconf_get_boolean (PREF_GO_TO_LAST_LOCATION, TRUE)) {
		char *location = NULL;

		if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST) 
			location = g_strconcat ("file://",
						window->dir_list->path,
						NULL);
		else if ((window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST) 
			 && (window->catalog_path != NULL))
			location = g_strconcat ("catalog://",
						window->catalog_path,
						NULL);

		if (location != NULL) {
			eel_gconf_set_path (PREF_STARTUP_LOCATION, location);
			g_free (location);
		}
	}

	if (last_window) {
		pref_set_zoom_quality (image_viewer_get_zoom_quality (viewer));
		pref_set_transp_type (image_viewer_get_transp_type (viewer));
	}
	pref_set_check_type (image_viewer_get_check_type (viewer));
	pref_set_check_size (image_viewer_get_check_size (viewer));

	eel_gconf_set_boolean (PREF_UI_IMAGE_PANE_VISIBLE, window->image_pane_visible);
	eel_gconf_set_boolean (PREF_SHOW_PREVIEW, window->preview_visible);
	eel_gconf_set_boolean (PREF_SHOW_IMAGE_DATA, window->image_data_visible);
	pref_set_preview_content (window->preview_content);

	/* Destroy the main window. */

	if (window->progress_timeout != 0) {
		g_source_remove (window->progress_timeout);
		window->progress_timeout = 0;
	}

	if (window->activity_timeout != 0) {
		g_source_remove (window->activity_timeout);
		window->activity_timeout = 0;
	}

	if (window->load_dir_timeout != 0) {
		g_source_remove (window->load_dir_timeout);
		window->load_dir_timeout = 0;
	}

	if (window->sel_change_timeout != 0) {
		g_source_remove (window->sel_change_timeout);
		window->sel_change_timeout = 0;
	}

	if (window->busy_cursor_timeout != 0) {
		g_source_remove (window->busy_cursor_timeout);
		window->busy_cursor_timeout = 0;
	}

	if (window->view_image_timeout != 0) {
 		g_source_remove (window->view_image_timeout);
		window->view_image_timeout = 0;
	}

	if (window->update_changes_timeout != 0) {
		g_source_remove (window->update_changes_timeout);
		window->update_changes_timeout = 0;
	}

	if (window->auto_load_timeout != 0) {
		g_source_remove (window->auto_load_timeout);
		window->auto_load_timeout = 0;
	}

	if (window->pixop != NULL) 
		g_object_unref (window->pixop);

	if (window->progress_gui != NULL)
		g_object_unref (window->progress_gui);

	gtk_widget_destroy (window->file_popup_menu);
	gtk_widget_destroy (window->image_popup_menu);
	gtk_widget_destroy (window->fullscreen_image_popup_menu);
	gtk_widget_destroy (window->catalog_popup_menu);
	gtk_widget_destroy (window->library_popup_menu);
	gtk_widget_destroy (window->dir_popup_menu);
	gtk_widget_destroy (window->dir_list_popup_menu);
	gtk_widget_destroy (window->catalog_list_popup_menu);
	gtk_widget_destroy (window->history_list_popup_menu);

	if (gth_file_list_drag_data != NULL) {
		path_list_free (gth_file_list_drag_data);
		gth_file_list_drag_data = NULL;
	}

	if (dir_list_drag_data != NULL) {
		g_free (dir_list_drag_data);
		dir_list_drag_data = NULL;
	}

	if (window->slideshow_set != NULL)
		g_list_free (window->slideshow_set);
	if (window->slideshow_random_set != NULL)
		g_list_free (window->slideshow_random_set);

	gtk_object_destroy (GTK_OBJECT (window->tooltips));

	gtk_widget_destroy (window->app);
	window_list = g_list_remove (window_list, window);
	window_free (window);

	if (window_list == NULL) {
		if (dir_list_tree_path != NULL) {
			gtk_tree_path_free (dir_list_tree_path);
			dir_list_tree_path = NULL;
		}

		if (catalog_list_tree_path != NULL) {
			gtk_tree_path_free (catalog_list_tree_path);
			catalog_list_tree_path = NULL;
		}

		gtk_main_quit(); 

	} else if (ExitAll) {
		GThumbWindow *first_window = window_list->data;
		window_close (first_window);
	}
}


void
close__step5 (GThumbWindow *window)
{
	if (window->image_modified) 
		if (ask_whether_to_save (window, close__step6))
			return;
	close__step6 (NULL, window);
}


void
close__step4 (GThumbWindow *window)
{
	if (window->slideshow)
		window_stop_slideshow (window);

	if (window->preloader != NULL)
		gthumb_preloader_stop (window->preloader, 
				       (DoneFunc) close__step5, 
				       window);
	else
		close__step5 (window);
}


void
close__step3 (GThumbWindow *window)
{
	window->setting_file_list = FALSE;
	if (window->file_list->doing_thumbs)
		gth_file_list_interrupt_thumbs (window->file_list, 
						(DoneFunc) close__step4, 
						window);
	else
		close__step4 (window);
}


void
close__step2 (GThumbWindow *window)
{
	window->changing_directory = FALSE;
	if (window->setting_file_list) 
		gth_file_list_interrupt_set_list (window->file_list,
						  (DoneFunc) close__step3,
						  window);
	else
		close__step3 (window);
}


void
window_close (GThumbWindow *window)
{
	g_return_if_fail (window != NULL);

	/* Interrupt any activity. */

	if (window->update_layout_timeout != 0) {
		g_source_remove (window->update_layout_timeout);
		window->update_layout_timeout = 0;
	}

	_window_remove_notifications (window);
	window_stop_activity_mode (window);
	window_remove_monitor (window);

	if (window->image_prop_dlg != NULL) {
		dlg_image_prop_close (window->image_prop_dlg);
		window->image_prop_dlg = NULL;
	}

	if (window->comment_dlg != NULL) {
		dlg_comment_close (window->comment_dlg);
		window->comment_dlg = NULL;
	}

	if (window->categories_dlg != NULL) {
		dlg_categories_close (window->categories_dlg);
		window->categories_dlg = NULL;
	}

	if (window->changing_directory) 
		dir_list_interrupt_change_to (window->dir_list, 
					      (DoneFunc) close__step2,
					      window);
	else
		close__step2 (window);
}


void
window_set_sidebar_content (GThumbWindow *window,
			    int           sidebar_content)
{
	char  old_content = window->sidebar_content;
	char *path;

	_window_set_sidebar (window, sidebar_content);
	window_show_sidebar (window);

	if (old_content == sidebar_content) 
		return;

	switch (sidebar_content) {
	case GTH_SIDEBAR_DIR_LIST: 
		if (window->dir_list->path == NULL) 
			window_go_to_directory (window, g_get_home_dir ());
		else 
			window_go_to_directory (window, window->dir_list->path);
		break;

	case GTH_SIDEBAR_CATALOG_LIST:
		if (window->catalog_path == NULL)
			path = get_catalog_full_path (NULL);
		else 
			path = remove_level_from_path (window->catalog_path);
		window_go_to_catalog_directory (window, path);
		
		g_free (path);

		debug (DEBUG_INFO, "catalog path: %s", window->catalog_path);

		if (window->catalog_path != NULL) {
			GtkTreeIter iter;
			
			if (window->slideshow)
				window_stop_slideshow (window);
			
			if (! catalog_list_get_iter_from_path (window->catalog_list, window->catalog_path, &iter)) {
				window_image_viewer_set_void (window);
				return;
			}
			catalog_list_select_iter (window->catalog_list, &iter);
			catalog_activate (window, window->catalog_path);
		} else {
			window_set_file_list (window, NULL, NULL, NULL);
			window_image_viewer_set_void (window);
		}

		window_update_title (window);
		break;

	default:
		break;
	}
}


void
window_hide_sidebar (GThumbWindow *window)
{
	if (window->image_path == NULL)
		return;

	window->sidebar_visible = FALSE;
	window->sidebar_width = gtk_paned_get_position (GTK_PANED (window->main_pane));

	if (window->layout_type <= 1)
		gtk_widget_hide (GTK_PANED (window->main_pane)->child1);

	else if (window->layout_type == 2) {
		gtk_widget_hide (GTK_PANED (window->main_pane)->child2);
		gtk_widget_hide (GTK_PANED (window->content_pane)->child1);

	} else if (window->layout_type == 3) {
		gtk_widget_hide (GTK_PANED (window->main_pane)->child1);
		gtk_widget_hide (GTK_PANED (window->content_pane)->child1);
	}

	if (window->image_data_visible)
		window_show_image_data (window);
	else
		window_hide_image_data (window);

	gtk_widget_show (window->preview_widget_image);
	gtk_widget_grab_focus (window->viewer);

	/* Sync menu and toolbar. */

	set_action_active_if_different (window, "View_ShowImage", TRUE);
	set_button_active_no_notify (window,
				     get_button_from_sidebar_content (window),
				     FALSE);

	/**/

	if (eel_gconf_get_boolean (PREF_UI_TOOLBAR_VISIBLE, TRUE)) {
		/*
		bonobo_ui_component_set_prop (window->ui_component, 
					      "/Toolbar",
					      "hidden", "1",
					      NULL);
		bonobo_ui_component_set_prop (window->ui_component, 
					      "/ImageToolbar",
					      "hidden", "0",
					      NULL);
		*/
	}

	/**/

	gtk_widget_hide (window->preview_button_image);
	gtk_widget_hide (window->preview_button_comment);

	/**/

	if (! window->image_pane_visible) 
		window_show_image_pane (window);

	window_update_sensitivity (window);
	window_update_statusbar_zoom_info (window);
}


void
window_show_sidebar (GThumbWindow *window)
{
	char *cname;

	if (! window->sidebar_visible) 
		gtk_paned_set_position (GTK_PANED (window->main_pane), 
					window->sidebar_width); 

	window->sidebar_visible = TRUE;

	/**/

	if (window->layout_type < 2)
		gtk_widget_show (GTK_PANED (window->main_pane)->child1);

	else if (window->layout_type == 2) {
		gtk_widget_show (GTK_PANED (window->main_pane)->child2);
		gtk_widget_show (GTK_PANED (window->content_pane)->child1);

	} else if (window->layout_type == 3) {
		gtk_widget_show (GTK_PANED (window->main_pane)->child1);
		gtk_widget_show (GTK_PANED (window->content_pane)->child1);
	}

	/**/

	window_set_preview_content (window, window->preview_content);
	
	/* Sync menu and toolbar. */

	cname = get_command_name_from_sidebar_content (window);
	if (cname != NULL)
		set_action_active_if_different (window, cname, TRUE);
	set_button_active_no_notify (window,
				     get_button_from_sidebar_content (window),
				     TRUE);

	/**/

	if (eel_gconf_get_boolean (PREF_UI_TOOLBAR_VISIBLE, TRUE)) {
		/*
		bonobo_ui_component_set_prop (window->ui_component, 
					      "/Toolbar",
					      "hidden", "0",
					      NULL);
		bonobo_ui_component_set_prop (window->ui_component, 
					      "/ImageToolbar",
					      "hidden", "1",
					      NULL);
		*/
	}

	/**/

	gtk_widget_show (window->preview_button_image);
	gtk_widget_show (window->preview_button_comment);

	if (window->preview_visible)
		window_show_image_pane (window);
	else
		window_hide_image_pane (window);

	/**/

	gtk_widget_grab_focus (gth_file_view_get_widget (window->file_list->view));
	window_update_sensitivity (window);
}


void
window_hide_image_pane (GThumbWindow *window)
{
	window->image_pane_visible = FALSE;
	gtk_widget_hide (window->image_pane);

	gtk_widget_grab_focus (gth_file_view_get_widget (window->file_list->view));

	/**/

	if (! window->sidebar_visible)
		window_show_sidebar (window);
	else {
		window->preview_visible = FALSE;
		/* Sync menu and toolbar. */
		set_action_active_if_different (window, 
						"View_ShowPreview", 
						FALSE);
	}

	window_update_statusbar_zoom_info (window);
	window_update_sensitivity (window);
}


void
window_show_image_pane (GThumbWindow *window)
{
	window->image_pane_visible = TRUE;

	if (window->sidebar_visible) {
		window->preview_visible = TRUE;
		/* Sync menu and toolbar. */
		set_action_active_if_different (window, 
						"View_ShowPreview", 
						TRUE);
	}

	gtk_widget_show (window->image_pane);
	gtk_widget_grab_focus (window->viewer);

	window_update_statusbar_zoom_info (window);
	window_update_sensitivity (window);
}


/* -- window_stop_loading -- */


void
stop__step5 (GThumbWindow *window)
{
	set_action_sensitive (window, "Go_Stop", 
			       (window->activity_ref > 0) 
			       || window->setting_file_list
			       || window->changing_directory
			       || window->file_list->doing_thumbs);
}


void
stop__step4 (GThumbWindow *window)
{
	if (window->slideshow)
		window_stop_slideshow (window);

	gthumb_preloader_stop (window->preloader, 
			       (DoneFunc) stop__step5, 
			       window);

	set_action_sensitive (window, "Go_Stop", 
			       (window->activity_ref > 0) 
			       || window->setting_file_list
			       || window->changing_directory
			       || window->file_list->doing_thumbs);
}


void
stop__step3 (GThumbWindow *window)
{
	window->setting_file_list = FALSE;
	if (window->file_list->doing_thumbs)
		gth_file_list_interrupt_thumbs (window->file_list, 
						(DoneFunc) stop__step4, 
						window);
	else
		stop__step4 (window);
}


void
stop__step2 (GThumbWindow *window)
{
	window->changing_directory = FALSE;
	if (window->setting_file_list) 
		gth_file_list_interrupt_set_list (window->file_list,
						  (DoneFunc) stop__step3,
						  window);
	else
		stop__step3 (window);
}


void
window_stop_loading (GThumbWindow *window)
{
	window_stop_activity_mode (window);

	if (window->changing_directory) 
		dir_list_interrupt_change_to (window->dir_list,
					      (DoneFunc) stop__step2,
					      window);
	else
		stop__step2 (window);
}


/* -- window_refresh -- */


void
window_refresh (GThumbWindow *window)
{
	window->refreshing = TRUE;
	window_update_file_list (window);
}


/* -- file system monitor -- */


static void notify_files_added (GThumbWindow *window, GList *list);
static void notify_files_deleted (GThumbWindow *window, GList *list);


static gboolean
_proc_monitor_events (gpointer data)
{
	GThumbWindow *window = data;
	GList        *dir_created_list, *dir_deleted_list;
	GList        *file_created_list, *file_deleted_list, *file_changed_list;
	GList        *scan;

	dir_created_list = window->monitor_events[MONITOR_EVENT_DIR_CREATED];
	window->monitor_events[MONITOR_EVENT_DIR_CREATED] = NULL;

	dir_deleted_list = window->monitor_events[MONITOR_EVENT_DIR_DELETED];
	window->monitor_events[MONITOR_EVENT_DIR_DELETED] = NULL;

	file_created_list = window->monitor_events[MONITOR_EVENT_FILE_CREATED];
	window->monitor_events[MONITOR_EVENT_FILE_CREATED] = NULL;

	file_deleted_list = window->monitor_events[MONITOR_EVENT_FILE_DELETED];
	window->monitor_events[MONITOR_EVENT_FILE_DELETED] = NULL;

	file_changed_list = window->monitor_events[MONITOR_EVENT_FILE_CHANGED];
	window->monitor_events[MONITOR_EVENT_FILE_CHANGED] = NULL;

	if (window->update_changes_timeout != 0) 
		g_source_remove (window->update_changes_timeout);

	/**/

	for (scan = dir_created_list; scan; scan = scan->next) {
		char *path = scan->data;
		const char *name = file_name_from_path (path);

		/* ignore hidden directories. */
		if (name[0] == '.')
			continue;

		dir_list_add_directory (window->dir_list, path);
	}
	path_list_free (dir_created_list);

	/**/

	for (scan = dir_deleted_list; scan; scan = scan->next) {
		char *path = scan->data;
		dir_list_remove_directory (window->dir_list, path);
	}
	path_list_free (dir_deleted_list);

	/**/

	if (file_created_list != NULL) {
		notify_files_added (window, file_created_list);
		path_list_free (file_created_list);
	}

	if (file_deleted_list != NULL) {
		notify_files_deleted (window, file_deleted_list);
		path_list_free (file_deleted_list);
	}

	if (file_changed_list != NULL) {
		window_notify_files_changed (window, file_changed_list);
		path_list_free (file_changed_list);
	}

	/**/

	window->update_changes_timeout = 0;

	return FALSE;
}


static gboolean
remove_if_present (GList            **monitor_events,
		   MonitorEventType   type,
		   const char        *path)
{
	GList *list, *link;

	list = monitor_events[type];
	link = path_list_find_path (list, path);
	if (link != NULL) {
		monitor_events[type] = g_list_remove_link (list, link);
		path_list_free (link);
		return TRUE;
	}

	return FALSE;
}


static gboolean
add_if_not_present (GList            **monitor_events,
		    MonitorEventType   type,
		    MonitorEventType   add_type,
		    const char        *path)
{
	GList *list, *link;

	list = monitor_events[type];
	link = path_list_find_path (list, path);
	if (link == NULL) {
		monitor_events[add_type] = g_list_append (list, g_strdup (path));
		return TRUE;
	}

	return FALSE;
}


static void
_window_add_monitor_event (GThumbWindow              *window,
			   GnomeVFSMonitorEventType   event_type,
			   char                      *path,
			   GList                    **monitor_events)
{
	MonitorEventType type;

#ifdef DEBUG /* FIXME */
	{
		char *op;

		if (event_type == GNOME_VFS_MONITOR_EVENT_CREATED)
			op = "CREATED";
		else if (event_type == GNOME_VFS_MONITOR_EVENT_DELETED)
			op = "DELETED";
		else 
			op = "CHANGED";

		debug (DEBUG_INFO, "[%s] %s", op, path);
	}
#endif

	if (event_type == GNOME_VFS_MONITOR_EVENT_CREATED) {
		if (path_is_dir (path))
			type = MONITOR_EVENT_DIR_CREATED;
		else
			type = MONITOR_EVENT_FILE_CREATED;

	} else if (event_type == GNOME_VFS_MONITOR_EVENT_DELETED) {
		if (dir_list_get_row_from_path (window->dir_list, path) != -1)
			type = MONITOR_EVENT_DIR_DELETED;
		else
			type = MONITOR_EVENT_FILE_DELETED;

	} else {
		if (! path_is_dir (path))
			type = MONITOR_EVENT_FILE_CHANGED;
		else 
			return;
	}

	if (type == MONITOR_EVENT_FILE_CREATED) {
		if (remove_if_present (monitor_events, 
				       MONITOR_EVENT_FILE_DELETED, 
				       path))
			type = MONITOR_EVENT_FILE_CHANGED;
		
	} else if (type == MONITOR_EVENT_FILE_DELETED) {
		remove_if_present (monitor_events, 
				   MONITOR_EVENT_FILE_CREATED, 
				   path);
		remove_if_present (monitor_events, 
				   MONITOR_EVENT_FILE_CHANGED, 
				   path);

	} else if (type == MONITOR_EVENT_FILE_CHANGED) {
		remove_if_present (monitor_events, 
				   MONITOR_EVENT_FILE_CHANGED, 
				   path);
		
		if (gth_file_list_pos_from_path (window->file_list, path) == -1)
			add_if_not_present (monitor_events, 
					    MONITOR_EVENT_FILE_CHANGED, 
					    MONITOR_EVENT_FILE_CREATED, 
					    path);

	} else if (type == MONITOR_EVENT_DIR_CREATED) {
		remove_if_present (monitor_events, 
				   MONITOR_EVENT_DIR_DELETED,
				   path);

	} else if (type == MONITOR_EVENT_DIR_DELETED) 
		remove_if_present (monitor_events, 
				   MONITOR_EVENT_DIR_CREATED, 
				   path);

	monitor_events[type] = g_list_append (monitor_events[type], g_strdup (path));
}


static void
directory_changed (GnomeVFSMonitorHandle    *handle,
		   const char               *monitor_uri,
		   const char               *info_uri,
		   GnomeVFSMonitorEventType  event_type,
		   gpointer                  user_data)
{
	GThumbWindow *window = user_data; 
	char         *path;

	if (window->sidebar_content != GTH_SIDEBAR_DIR_LIST)
		return;

	if (window->update_changes_timeout != 0) 
		return;

	path = gnome_vfs_unescape_string (info_uri + strlen ("file://"), NULL);
	_window_add_monitor_event (window, event_type, path, window->monitor_events);
	g_free (path);

	window->update_changes_timeout = g_timeout_add (UPDATE_DIR_DELAY,
							_proc_monitor_events,
							window);
}


/* -- go to directory -- */


void
window_add_monitor (GThumbWindow *window)
{
	GnomeVFSResult  result;
	char           *uri;

	if (window->sidebar_content != GTH_SIDEBAR_DIR_LIST)
		return;

	if (window->dir_list->path == NULL)
		return;

	if (window->monitor_handle != NULL)
		gnome_vfs_monitor_cancel (window->monitor_handle);

	uri = g_strconcat ("file://", window->dir_list->path, NULL);
	result = gnome_vfs_monitor_add (&window->monitor_handle,
					uri,
					GNOME_VFS_MONITOR_DIRECTORY,
					directory_changed,
					window);
	g_free (uri);
	window->monitor_enabled = (result == GNOME_VFS_OK);
}


void
window_remove_monitor (GThumbWindow *window)
{
	if (window->monitor_handle != NULL) {
		gnome_vfs_monitor_cancel (window->monitor_handle);
		window->monitor_handle = NULL;
	}
	window->monitor_enabled = FALSE;
}


static void
set_dir_list_continue (gpointer data)
{
	GThumbWindow *window = data;

	window_update_title (window);
	window_update_sensitivity (window);
	window_make_current_image_visible (window);

	if (window->focus_location_entry) {
		gtk_widget_grab_focus (window->location_entry);
		gtk_editable_set_position (GTK_EDITABLE (window->location_entry), -1);
		window->focus_location_entry = FALSE;
	}

	window_add_monitor (window);
}


static void
go_to_directory_continue (DirList  *dir_list,
			  gpointer  data)
{
	GThumbWindow *window = data;
	char         *path;
	GList        *file_list;

	window_stop_activity_mode (window);

	if (dir_list->result != GNOME_VFS_ERROR_EOF) {
		char *utf8_path;

		utf8_path = g_filename_to_utf8 (dir_list->try_path, -1,
						NULL, NULL, NULL);
		_gtk_error_dialog_run (GTK_WINDOW (window->app),
				       _("Cannot load folder \"%s\": %s\n"), 
				       utf8_path, 
				       gnome_vfs_result_to_string (dir_list->result));
		g_free (utf8_path);

		window->changing_directory = FALSE;
		return;
	}

	path = dir_list->path;

	set_action_sensitive (window, "Go_Up", strcmp (path, "/") != 0);
	
	/* Add to history list if not present as last entry. */

	if ((window->go_op == WINDOW_GO_TO)
	    && ((window->history_current == NULL) 
		|| (strcmp (path, pref_util_remove_prefix (window->history_current->data)) != 0))) 
		add_history_item (window, path, FILE_PREFIX);
	else
		window->go_op = WINDOW_GO_TO;
	window_update_history_list (window);

	set_location (window, path);

	window->changing_directory = FALSE;

	/**/

	file_list = dir_list_get_file_list (window->dir_list);
	window_set_file_list (window, file_list, 
			      set_dir_list_continue, window);
	g_list_free (file_list);
}


/* -- real_go_to_directory -- */


typedef struct {
	GThumbWindow *window;
	char         *path;
} GoToData;


void
go_to_directory__step4 (GoToData *gt_data)
{
	GThumbWindow *window = gt_data->window;
	char         *dir_path = gt_data->path;
	char         *path;

	window->setting_file_list = FALSE;
	window->changing_directory = TRUE;

	/* Select the directory view. */

	_window_set_sidebar (window, GTH_SIDEBAR_DIR_LIST); 
	if (! window->refreshing)
		window_show_sidebar (window);
	else
		window->refreshing = FALSE;

	/**/

	path = remove_ending_separator (dir_path);

	window_start_activity_mode (window);
	dir_list_change_to (window->dir_list, 
			    path, 
			    go_to_directory_continue,
			    window);
	g_free (path);

	g_free (gt_data->path);
	g_free (gt_data);

	window->can_change_directory = TRUE;
}


static gboolean
go_to_directory_cb (gpointer data)
{
	GoToData     *gt_data = data;
	GThumbWindow *window = gt_data->window;

	window->can_change_directory = FALSE;

	g_source_remove (window->load_dir_timeout);
	window->load_dir_timeout = 0;

	if (window->slideshow)
		window_stop_slideshow (window);

	if (window->changing_directory) {
		window_stop_activity_mode (window);
		dir_list_interrupt_change_to (window->dir_list,
					      (DoneFunc)go_to_directory__step4,
					      gt_data);

	} else if (window->setting_file_list) 
		gth_file_list_interrupt_set_list (window->file_list,
						  (DoneFunc) go_to_directory__step4,
						  gt_data);

	else if (window->file_list->doing_thumbs) 
		gth_file_list_interrupt_thumbs (window->file_list, 
						(DoneFunc) go_to_directory__step4,
						gt_data);

	else 
		go_to_directory__step4 (gt_data);

	return FALSE;
}


/* used in : goto_dir_set_list_interrupted, window_go_to_directory. */
static void
real_go_to_directory (GThumbWindow *window,
		      const char   *dir_path)
{
	GoToData *gt_data;

	if (! window->can_change_directory)
		return;

	gt_data = g_new (GoToData, 1);
	gt_data->window = window;
	gt_data->path = g_strdup (dir_path);

	if (window->load_dir_timeout != 0) {
		g_source_remove (window->load_dir_timeout);
		window->load_dir_timeout = 0;
	}

	window->load_dir_timeout = g_timeout_add (LOAD_DIR_DELAY,
						  go_to_directory_cb, 
						  gt_data);
}


typedef struct {
	GThumbWindow *window;
	char         *dir_path;
} GoToDir_SetListInterruptedData;


static void
go_to_dir_set_list_interrupted (gpointer callback_data)
{
	GoToDir_SetListInterruptedData *data = callback_data;

	data->window->setting_file_list = FALSE;
	window_stop_activity_mode (data->window);

	real_go_to_directory (data->window, data->dir_path);
	g_free (data->dir_path);
	g_free (data);
}

/**/

void
window_go_to_directory (GThumbWindow *window,
			const char   *dir_path)
{
	g_return_if_fail (window != NULL);

	if (! window->can_change_directory)
		return;

	if (window->setting_file_list && FirstStart)
		return;

	if (window->setting_file_list && ! window->can_set_file_list) 
		return;

	/**/

	if (window->slideshow)
		window_stop_slideshow (window);

	if (window->monitor_handle != NULL) {
		gnome_vfs_monitor_cancel (window->monitor_handle);
		window->monitor_handle = NULL;
	}

	if (window->setting_file_list) 
		window_stop_activity_mode (window);

	if (window->setting_file_list) {
		GoToDir_SetListInterruptedData *sli_data;

		sli_data = g_new (GoToDir_SetListInterruptedData, 1);
		sli_data->window = window;
		sli_data->dir_path = g_strdup (dir_path);
		gth_file_list_interrupt_set_list (window->file_list,
						  go_to_dir_set_list_interrupted,
						  sli_data);
		return;
	}

	real_go_to_directory (window, dir_path);
}


/* -- */


void
window_go_to_catalog_directory (GThumbWindow *window,
				const char   *catalog_dir)
{
	char *base_dir;
	char *catalog_dir2;
	char *catalog_dir3;
	char *current_path;

	catalog_dir2 = remove_special_dirs_from_path (catalog_dir);
	catalog_dir3 = remove_ending_separator (catalog_dir2);
	g_free (catalog_dir2);

	if (catalog_dir3 == NULL)
		return; /* FIXME: error dialog?  */

	catalog_list_change_to (window->catalog_list, catalog_dir3);
	set_location (window, catalog_dir3);
	g_free (catalog_dir3);

	/* Update Go_Up command sensibility */

	current_path = window->catalog_list->path;
	base_dir = get_catalog_full_path (NULL);
	set_action_sensitive (window, "Go_Up", 
			       ((current_path != NULL)
				&& strcmp (current_path, base_dir)) != 0);
	g_free (base_dir);
}


/* -- window_go_to_catalog -- */


void
go_to_catalog__step2 (GoToData *gt_data)
{
	GThumbWindow *window = gt_data->window;
	char         *catalog_path = gt_data->path;
	GtkTreeIter   iter;
	char         *catalog_dir;

	if (window->slideshow)
		window_stop_slideshow (window);

	if (catalog_path == NULL) {
		window_set_file_list (window, NULL, NULL, NULL);
		if (window->catalog_path)
			g_free (window->catalog_path);
		window->catalog_path = NULL;
		g_free (gt_data->path);
		g_free (gt_data);
		window_update_title (window);
		window_image_viewer_set_void (window);
		return;
	}

	if (! path_is_file (catalog_path)) {
		_gtk_error_dialog_run (GTK_WINDOW (window->app),
				       _("The specified catalog does not exist."));

		/* window_go_to_directory (window, g_get_home_dir ()); FIXME */
		g_free (gt_data->path);
		g_free (gt_data);
		window_update_title (window);
		window_image_viewer_set_void (window);
		return;
	}

	if (window->catalog_path != catalog_path) {
		if (window->catalog_path)
			g_free (window->catalog_path);
		window->catalog_path = g_strdup (catalog_path);
	}

	_window_set_sidebar (window, GTH_SIDEBAR_CATALOG_LIST); 
	if (! window->refreshing && ! ViewFirstImage)
		window_show_sidebar (window);
	else
		window->refreshing = FALSE;

	catalog_dir = remove_level_from_path (catalog_path);
	window_go_to_catalog_directory (window, catalog_dir);
	g_free (catalog_dir);

	if (! catalog_list_get_iter_from_path (window->catalog_list,
					       catalog_path,
					       &iter)) {
		g_free (gt_data->path);
		g_free (gt_data);
		window_image_viewer_set_void (window);
		return;
	}

	catalog_list_select_iter (window->catalog_list, &iter);
	catalog_activate (window, catalog_path);

	g_free (gt_data->path);
	g_free (gt_data);
}


void
window_go_to_catalog (GThumbWindow *window,
		      const char  *catalog_path)
{
	GoToData *gt_data;

	g_return_if_fail (window != NULL);

	if (window->setting_file_list && FirstStart)
		return;

	gt_data = g_new (GoToData, 1);
	gt_data->window = window;
	gt_data->path = g_strdup (catalog_path);
	
	if (window->file_list->doing_thumbs)
		gth_file_list_interrupt_thumbs (window->file_list, 
						(DoneFunc) go_to_catalog__step2,
						gt_data);
	else
		go_to_catalog__step2 (gt_data);
}


gboolean
window_go_up__is_base_dir (GThumbWindow *window,
			   const char   *dir)
{
	if (dir == NULL)
		return FALSE;

	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		return (strcmp (dir, "/") == 0);
	else {
		char *catalog_base = get_catalog_full_path (NULL);
		gboolean is_base_dir;

		is_base_dir = strcmp (dir, catalog_base) == 0;
		g_free (catalog_base);
		return is_base_dir;
	}
	
	return FALSE;
}


void
window_go_up (GThumbWindow *window)
{
	char *current_path;
	char *up_dir;

	g_return_if_fail (window != NULL);

	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST) 
		current_path = window->dir_list->path;
	else 
		current_path = window->catalog_list->path;

	if (current_path == NULL)
		return;

	if (window_go_up__is_base_dir (window, current_path))
		return;

	up_dir = g_strdup (current_path);
	do {
		char *tmp = up_dir;
		up_dir = remove_level_from_path (tmp);
		g_free (tmp);
	} while (! window_go_up__is_base_dir (window, up_dir) 
		 && ! path_is_dir (up_dir));

	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
		window_go_to_directory (window, up_dir);
	else {
		window_go_to_catalog (window, NULL);
		window_go_to_catalog_directory (window, up_dir);
	}

	g_free (up_dir);
}


static void 
go_to_current_location (GThumbWindow *window,
			WindowGoOp go_op)
{
	char *location;

	if (window->history_current == NULL)
		return;

	window->go_op = go_op;

	location = window->history_current->data;
	if (pref_util_location_is_catalog (location))
		window_go_to_catalog (window, pref_util_get_catalog_location (location));
	else if (pref_util_location_is_search (location))
		window_go_to_catalog (window, pref_util_get_search_location (location));
	else if (pref_util_location_is_file (location)) 
		window_go_to_directory (window, pref_util_get_file_location (location));
}


void
window_go_back (GThumbWindow *window)
{
	g_return_if_fail (window != NULL);
	
	if (window->history_current == NULL)
		return;

	if (window->history_current->next == NULL)
		return;

	window->history_current = window->history_current->next;
	go_to_current_location (window, WINDOW_GO_BACK);
}


void
window_go_forward (GThumbWindow *window)
{
	if (window->history_current == NULL)
		return;

	if (window->history_current->prev == NULL)
		return;

	window->history_current = window->history_current->prev;
	go_to_current_location (window, WINDOW_GO_FORWARD);
}


void
window_delete_history (GThumbWindow *window)
{
	bookmarks_remove_all (window->history);
	window->history_current = NULL;
	window_update_history_list (window);
}


/* -- slideshow -- */


static GList *
get_slideshow_random_image (GThumbWindow *window)
{
	int    n, i;
	GList *item;

	if (window->slideshow_current != NULL) {
		g_list_free (window->slideshow_current);
		window->slideshow_current = NULL;
	}

	n = g_list_length (window->slideshow_random_set);
	if (n == 0)
		return NULL;
	i = g_random_int_range (0, n - 1);

	item = g_list_nth (window->slideshow_random_set, i);
	if (item != NULL)
		window->slideshow_random_set = g_list_remove_link (window->slideshow_random_set, item);

	window->slideshow_current = item;

	return item;
}


static GList *
get_next_slideshow_image (GThumbWindow *window)
{
	GList    *current;
	FileData *fd;
	gboolean  wrap_around;

	current = window->slideshow_current;
	if (current == NULL) {
		if (pref_get_slideshow_direction () == GTH_DIRECTION_RANDOM)
			current = get_slideshow_random_image (window);
		else
			current = window->slideshow_first;
		fd = current->data;
		if (! fd->error) 
			return current;
	}

	wrap_around = eel_gconf_get_boolean (PREF_SLIDESHOW_WRAP_AROUND, FALSE);
	do {
		if (pref_get_slideshow_direction () == GTH_DIRECTION_FORWARD) {
			current = current->next;
			if (current == NULL)
				current = window->slideshow_set;
			if ((current == window->slideshow_first) && !wrap_around) 
				return NULL;

		} else if (pref_get_slideshow_direction () == GTH_DIRECTION_REVERSE) {
			current = current->prev;
			if (current == NULL)
				current = g_list_last (window->slideshow_set);
			if ((current == window->slideshow_first) && ! wrap_around)
				return NULL;

		} else if (pref_get_slideshow_direction () == GTH_DIRECTION_RANDOM) {
			current = get_slideshow_random_image (window);
			if (current == NULL) {
				if (! wrap_around)
					return NULL;
				else {
					window->slideshow_random_set = g_list_copy (window->slideshow_set);
					current = get_slideshow_random_image (window);
				}
			}
		}

		fd = current->data;
	} while (fd->error);

	return current;
}


static gboolean
slideshow_timeout_cb (gpointer data)
{
	GThumbWindow *window = data;
	gboolean      go_on = FALSE;

	if (window->slideshow_timeout != 0) {
		g_source_remove (window->slideshow_timeout);
		window->slideshow_timeout = 0;
	}

	window->slideshow_current = get_next_slideshow_image (window);

	if (window->slideshow_current != NULL) {
		FileData *fd = window->slideshow_current->data;
		int       pos = gth_file_list_pos_from_path (window->file_list, fd->path);
		if (pos != -1) {
			gth_file_view_set_cursor (window->file_list->view, pos);
			go_on = TRUE;
		}
	}

	if (! go_on) 
		window_stop_slideshow (window);
	else
		window->slideshow_timeout = g_timeout_add (eel_gconf_get_integer (PREF_SLIDESHOW_DELAY, DEF_SLIDESHOW_DELAY) * 1000, slideshow_timeout_cb, window);

	return FALSE;
}


void
window_start_slideshow (GThumbWindow *window)
{
	gboolean  only_selected;
	gboolean  go_on = FALSE;

	g_return_if_fail (window != NULL);

	if (window->file_list->list == NULL)
		return;

	if (window->slideshow)
		return;

	window->slideshow = TRUE;

	if (window->slideshow_set != NULL) {
		g_list_free (window->slideshow_set);
		window->slideshow_set = NULL;
	}
	if (window->slideshow_random_set != NULL) {
		g_list_free (window->slideshow_random_set);
		window->slideshow_random_set = NULL;
	}
	window->slideshow_first = NULL;
	window->slideshow_current = NULL;

	only_selected = gth_file_view_selection_not_null (window->file_list->view) && ! gth_file_view_only_one_is_selected (window->file_list->view);

	if (only_selected)
		window->slideshow_set = gth_file_view_get_selection (window->file_list->view);
	else
		window->slideshow_set = gth_file_view_get_list (window->file_list->view);
	window->slideshow_random_set = g_list_copy (window->slideshow_set);

	if (pref_get_slideshow_direction () == GTH_DIRECTION_FORWARD)
		window->slideshow_first = g_list_first (window->slideshow_set);
	else if (pref_get_slideshow_direction () == GTH_DIRECTION_REVERSE)
		window->slideshow_first = g_list_last (window->slideshow_set);
	else if (pref_get_slideshow_direction () == GTH_DIRECTION_RANDOM) 
		window->slideshow_first = NULL;

	window->slideshow_current = get_next_slideshow_image (window);

	if (window->slideshow_current != NULL) {
		FileData *fd = window->slideshow_current->data;
		int       pos = gth_file_list_pos_from_path (window->file_list, fd->path);
		if (pos != -1) {
			if (eel_gconf_get_boolean (PREF_SLIDESHOW_FULLSCREEN, TRUE))
				fullscreen_start (fullscreen, window);
			gth_file_view_set_cursor (window->file_list->view, pos);
			go_on = TRUE;
		}
	}

	if (! go_on) 
		window_stop_slideshow (window);
	else
		window->slideshow_timeout = g_timeout_add (eel_gconf_get_integer (PREF_SLIDESHOW_DELAY, DEF_SLIDESHOW_DELAY) * 1000, slideshow_timeout_cb, window);
}


void
window_stop_slideshow (GThumbWindow *window)
{
	if (! window->slideshow)
		return;

	window->slideshow = FALSE;
	if (window->slideshow_timeout != 0) {
		g_source_remove (window->slideshow_timeout);
		window->slideshow_timeout = 0;
	}

	if ((pref_get_slideshow_direction () == GTH_DIRECTION_RANDOM)
	    && (window->slideshow_current != NULL)) {
		g_list_free (window->slideshow_current);
		window->slideshow_current = NULL;
	}

	if (window->slideshow_set != NULL) {
		g_list_free (window->slideshow_set);
		window->slideshow_set = NULL;
	}

	if (window->slideshow_random_set != NULL) {
		g_list_free (window->slideshow_random_set);
		window->slideshow_random_set = NULL;
	}

	if (eel_gconf_get_boolean (PREF_SLIDESHOW_FULLSCREEN, TRUE) && window->fullscreen)
		fullscreen_stop (fullscreen);
}


/**/


static gboolean
view_focused_image (GThumbWindow *window)
{
	int       pos;
	char     *focused;
	gboolean  not_focused;

	pos = gth_file_view_get_cursor (window->file_list->view);
	if (pos == -1)
		return FALSE;

	focused = gth_file_list_path_from_pos (window->file_list, pos);
	if (focused == NULL)
		return FALSE;

	not_focused = strcmp (window->image_path, focused) != 0;
	g_free (focused);

	return not_focused;
}


gboolean
window_show_next_image (GThumbWindow *window,
			gboolean      only_selected)
{
	gboolean skip_broken;
	int      pos;

	g_return_val_if_fail (window != NULL, FALSE);
	
	if (window->slideshow 
	    || window->setting_file_list 
	    || window->changing_directory) 
		return FALSE;
	
	skip_broken = window->fullscreen;

	if (window->image_path == NULL) {
		pos = gth_file_list_next_image (window->file_list, -1, skip_broken, only_selected);

	} else if (view_focused_image (window)) {
		pos = gth_file_view_get_cursor (window->file_list->view);
		if (pos == -1)
			pos = gth_file_list_next_image (window->file_list, pos, skip_broken, only_selected);

	} else {
		pos = gth_file_list_pos_from_path (window->file_list, window->image_path);
		pos = gth_file_list_next_image (window->file_list, pos, skip_broken, only_selected);
	}

	if (pos != -1) {
		if (! only_selected)
			gth_file_list_select_image_by_pos (window->file_list, pos); 
		gth_file_view_set_cursor (window->file_list->view, pos);
	}

	return (pos != -1);
}


gboolean
window_show_prev_image (GThumbWindow *window,
			gboolean      only_selected)
{
	gboolean skip_broken;
	int pos;

	g_return_val_if_fail (window != NULL, FALSE);
	
	if (window->slideshow 
	    || window->setting_file_list 
	    || window->changing_directory)
		return FALSE;

	skip_broken = window->fullscreen;
	
	if (window->image_path == NULL) {
		pos = gth_file_view_get_images (window->file_list->view);
		pos = gth_file_list_prev_image (window->file_list, pos, skip_broken, only_selected);
			
	} else if (view_focused_image (window)) {
		pos = gth_file_view_get_cursor (window->file_list->view);
		if (pos == -1) {
			pos = gth_file_view_get_images (window->file_list->view);
			pos = gth_file_list_prev_image (window->file_list, pos, skip_broken, only_selected);
		}

	} else {
		pos = gth_file_list_pos_from_path (window->file_list, window->image_path);
		pos = gth_file_list_prev_image (window->file_list, pos, skip_broken, only_selected);
	}

	if (pos != -1) {
		if (! only_selected)
			gth_file_list_select_image_by_pos (window->file_list, pos); 
		gth_file_view_set_cursor (window->file_list->view, pos);
	}

	return (pos != -1);
}


gboolean
window_show_first_image (GThumbWindow *window, 
			 gboolean      only_selected)
{
	if (gth_file_view_get_images (window->file_list->view) == 0)
		return FALSE;

	if (window->image_path) {
		g_free (window->image_path);
		window->image_path = NULL;
	}

	return window_show_next_image (window, only_selected);
}


gboolean
window_show_last_image (GThumbWindow *window, 
			gboolean      only_selected)
{
	if (gth_file_view_get_images (window->file_list->view) == 0)
		return FALSE;

	if (window->image_path) {
		g_free (window->image_path);
		window->image_path = NULL;
	}

	return window_show_prev_image (window, only_selected);
}


void
window_show_image_prop (GThumbWindow *window)
{
	if (window->image_prop_dlg == NULL) 
		window->image_prop_dlg = dlg_image_prop_new (window);
	else
		gtk_window_present (GTK_WINDOW (window->image_prop_dlg));
}


void
window_show_comment_dlg (GThumbWindow *window)
{
	if (window->comment_dlg == NULL) 
		window->comment_dlg = dlg_comment_new (window);
	else
		gtk_window_present (GTK_WINDOW (window->comment_dlg));
}


void
window_show_categories_dlg (GThumbWindow *window)
{
	if (window->categories_dlg == NULL) 
		window->categories_dlg = dlg_categories_new (window);
	else
		gtk_window_present (GTK_WINDOW (window->categories_dlg));
}


void
window_image_set_modified (GThumbWindow *window,
			   gboolean      modified)
{
	window->image_modified = modified;
	window_update_infobar (window);
	window_update_statusbar_image_info (window);
	window_update_title (window);

	set_action_sensitive (window, "File_Revert", ! image_viewer_is_void (IMAGE_VIEWER (window->viewer)) && window->image_modified);

	if (modified && window->image_prop_dlg != NULL)
		dlg_image_prop_update (window->image_prop_dlg);
}


gboolean
window_image_get_modified (GThumbWindow *window)
{
	return window->image_modified;
}


/* -- load image -- */


static char *
get_image_to_preload (GThumbWindow *window,
		      int           pos,
		      int           priority)
{
	FileData *fdata;
	int       max_size;

	if (pos < 0)
		return NULL;
	if (pos >= gth_file_view_get_images (window->file_list->view))
		return NULL;

	fdata = gth_file_view_get_image_data (window->file_list->view, pos);
	if (fdata == NULL)
		return NULL;

	debug (DEBUG_INFO, "%ld <-> %ld\n", (long int) fdata->size, (long int)PRELOADED_IMAGE_MAX_SIZE);

	if (priority == 1)
		max_size = PRELOADED_IMAGE_MAX_DIM1;
	else
		max_size = PRELOADED_IMAGE_MAX_DIM2;

	if (fdata->size > max_size) {
		debug (DEBUG_INFO, "image %s too large for preloading", gth_file_list_path_from_pos (window->file_list, pos));
		file_data_unref (fdata);
		return NULL;
	}

#ifdef HAVE_LIBJPEG
	if (image_is_jpeg (fdata->path)) {
		int width = 0, height = 0;

		f_get_jpeg_size (fdata->path, &width, &height);

		debug (DEBUG_INFO, "[%dx%d] <-> %d\n", width, height, max_size);

		if (width * height > max_size) {
			debug (DEBUG_INFO, "image %s too large for preloading", gth_file_list_path_from_pos (window->file_list, pos));
			file_data_unref (fdata);
			return NULL;
		}
	}
#endif /* HAVE_LIBJPEG */

	file_data_unref (fdata);

	return gth_file_list_path_from_pos (window->file_list, pos);
}


static gboolean
load_timeout_cb (gpointer data)
{
	GThumbWindow *window = data;
	char         *prev1;
	char         *next1;
	char         *next2;
	int           pos;

	if (window->view_image_timeout != 0) {
		g_source_remove (window->view_image_timeout);
		window->view_image_timeout = 0;
	}

	if (window->image_path == NULL)
		return FALSE;

	pos = gth_file_list_pos_from_path (window->file_list, window->image_path);
	g_return_val_if_fail (pos != -1, FALSE);

	prev1 = get_image_to_preload (window, pos - 1, 1);
	next1 = get_image_to_preload (window, pos + 1, 1);
	next2 = get_image_to_preload (window, pos + 2, 2);
	
	gthumb_preloader_start (window->preloader, 
				window->image_path, 
				next1, 
				prev1, 
				next2);

	g_free (prev1);
	g_free (next1);
	g_free (next2);

	return FALSE;
}


void
window_reload_image (GThumbWindow *window)
{
	g_return_if_fail (window != NULL);
	
	if (window->image_path != NULL)
		load_timeout_cb (window);
}


static void
load_image__image_saved_cb (char     *filename,
			    gpointer  data)
{
	GThumbWindow *window = data;
	window->image_modified = FALSE;
	window->saving_modified_image = FALSE;
	window_load_image (window, window->new_image_path);
}


void
window_load_image (GThumbWindow *window, 
		   const char   *filename)
{
	g_return_if_fail (window != NULL);

	if (window->image_modified) {
		if (window->saving_modified_image)
			return;
		g_free (window->new_image_path);
		window->new_image_path = g_strdup (filename);
		if (ask_whether_to_save (window, load_image__image_saved_cb))
			return;
	}

	if (filename == window->image_path) {
		window_reload_image (window);
		return;
	}

	if (! window->image_modified
	    && (window->image_path != NULL) 
	    && (filename != NULL)
	    && (strcmp (filename, window->image_path) == 0)
	    && (window->image_mtime == get_file_mtime (window->image_path))) 
		return;

	if (window->view_image_timeout != 0) {
		g_source_remove (window->view_image_timeout);
		window->view_image_timeout = 0;
	}
	
	/* If the image is from a catalog remember the catalog name. */

	if (window->image_catalog != NULL) {
		g_free (window->image_catalog);
		window->image_catalog = NULL;
	}
	if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST)
		window->image_catalog = g_strdup (window->catalog_path);

	/**/

	if (filename == NULL) {
		window_image_viewer_set_void (window);
		return;
	}

	g_free (window->image_path);
	window->image_path = g_strdup (filename);

	window->view_image_timeout = g_timeout_add (VIEW_IMAGE_DELAY,
						    load_timeout_cb, 
						    window);
}


/* -- changes notification functions -- */


void
notify_files_added__step2 (gpointer data)
{
	GThumbWindow *window = data;

	window_update_statusbar_list_info (window);
	window_update_infobar (window);
	window_update_sensitivity (window);
}


static void
notify_files_added (GThumbWindow *window,
		    GList        *list)
{
	gth_file_list_add_list (window->file_list, 
				list, 
				notify_files_added__step2,
				window);
}


void
window_notify_files_created (GThumbWindow *window,
			     GList        *list)
{
	GList *scan;
	GList *created_in_current_dir = NULL;
	char  *current_dir;

	if (window->sidebar_content != GTH_SIDEBAR_DIR_LIST)
		return;

	current_dir = window->dir_list->path;
	if (current_dir == NULL)
		return;

	for (scan = list; scan; scan = scan->next) {
		char *path = scan->data;
		char *parent_dir;
		
		parent_dir = remove_level_from_path (path);

		if (strcmp (parent_dir, current_dir) == 0)
			created_in_current_dir = g_list_prepend (created_in_current_dir, path);

		g_free (parent_dir);
	}

	if (created_in_current_dir != NULL) {
		notify_files_added (window, created_in_current_dir);
		g_list_free (created_in_current_dir);
	}
}


typedef struct {
	GThumbWindow *window;
	GList        *list;
	gboolean      restart_thumbs;
} FilesDeletedData;


static void
notify_files_deleted__step2 (FilesDeletedData *data)
{
	GThumbWindow *window = data->window;
	GList        *list = data->list;
	GList        *scan;
	char         *filename;
	int           pos, smallest_pos, image_pos;
	gboolean      current_image_deleted = FALSE;
	gboolean      no_image_viewed;

	gth_file_view_freeze (window->file_list->view);

	pos = -1;
	smallest_pos = -1;
	image_pos = -1;
	if (window->image_path)
		image_pos = gth_file_list_pos_from_path (window->file_list, 
							 window->image_path);
	no_image_viewed = (image_pos == -1);

	for (scan = list; scan; scan = scan->next) {
		filename = scan->data;

		pos = gth_file_list_pos_from_path (window->file_list, filename);
		if (pos == -1) 
			continue;

		if (image_pos == pos) {
			/* the current image will be deleted. */
			image_pos = -1;
			current_image_deleted = TRUE;

		} else if (image_pos > pos)
			/* a previous image will be deleted, so image_pos 
			 * decrements its value. */
			image_pos--;

		if (scan == list)
			smallest_pos = pos;
		else
			smallest_pos = MIN (smallest_pos, pos);

		gth_file_list_delete_pos (window->file_list, pos);
	}

	gth_file_view_thaw (window->file_list->view);

	/* Try to visualize the smallest pos. */
	if (smallest_pos != -1) {
		int images = gth_file_view_get_images (window->file_list->view);

		pos = smallest_pos;

		if (pos > images - 1)
			pos = images - 1;
		if (pos < 0)
			pos = 0;

		gth_file_view_moveto (window->file_list->view, pos, 0.5);
	}

	if (! no_image_viewed) {
		if (current_image_deleted) {
			int images = gth_file_view_get_images (window->file_list->view);

			/* delete the image from the viewer. */
			window_load_image (window, NULL);

			if ((images > 0) && (smallest_pos != -1)) {
				pos = smallest_pos;
				
				if (pos > images - 1)
					pos = images - 1;
				if (pos < 0)
					pos = 0;
				
				view_image_at_pos (window, pos);
				gth_file_list_select_image_by_pos (window->file_list, pos);
			}
		}
	}

	window_update_statusbar_list_info (window);
	window_update_sensitivity (window);

	if (data->restart_thumbs)
		gth_file_list_restart_thumbs (data->window->file_list, TRUE);

	path_list_free (data->list);
	g_free (data);
}


static void
notify_files_deleted (GThumbWindow *window,
		      GList        *list)
{
	FilesDeletedData *data;

	if (list == NULL)
		return;

	data = g_new (FilesDeletedData, 1);

	data->window = window;
	data->list = path_list_dup (list);
	data->restart_thumbs = window->file_list->doing_thumbs;
	
	gth_file_list_interrupt_thumbs (window->file_list,
					(DoneFunc) notify_files_deleted__step2,
					data);
}


void
window_notify_files_deleted (GThumbWindow *window,
			     GList        *list)
{
	g_return_if_fail (window != NULL);

	if ((window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST)
	    && (window->catalog_path != NULL)) { /* update the catalog. */
		Catalog *catalog;
		GList   *scan;

		catalog = catalog_new ();
		if (catalog_load_from_disk (catalog, window->catalog_path, NULL)) {
			for (scan = list; scan; scan = scan->next)
				catalog_remove_item (catalog, (char*) scan->data);
			catalog_write_to_disk (catalog, NULL);
		}
		catalog_free (catalog);
	} 

	notify_files_deleted (window, list);
}


void
window_notify_files_changed (GThumbWindow *window,
			     GList        *list)
{
	if (! window->file_list->doing_thumbs)
		gth_file_list_update_thumb_list (window->file_list, list);

	if (window->image_path != NULL) {
		int pos = gth_file_list_pos_from_path (window->file_list,
						       window->image_path);
		if (pos != -1)
			view_image_at_pos (window, pos);
	}
}


void
window_notify_cat_files_added (GThumbWindow *window,
			       const char   *catalog_name,
			       GList        *list)
{
	g_return_if_fail (window != NULL);

	if (window->sidebar_content != GTH_SIDEBAR_CATALOG_LIST)
		return;
	if (window->catalog_path == NULL)
		return;
	if (strcmp (window->catalog_path, catalog_name) != 0)
		return;

	notify_files_added (window, list);
}


void
window_notify_cat_files_deleted (GThumbWindow *window,
				 const char   *catalog_name,
				 GList        *list)
{
	g_return_if_fail (window != NULL);

	if (window->sidebar_content != GTH_SIDEBAR_CATALOG_LIST)
		return;
	if (window->catalog_path == NULL)
		return;
	if (strcmp (window->catalog_path, catalog_name) != 0)
		return;

	notify_files_deleted (window, list);
}

	
void
window_notify_file_rename (GThumbWindow *window,
			   const char   *old_name,
			   const char   *new_name)
{
	int pos;

	g_return_if_fail (window != NULL);
	if ((old_name == NULL) || (new_name == NULL))
		return;

	if ((window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST)
	    && (window->catalog_path != NULL)) { /* update the catalog. */
		Catalog  *catalog;
		GList    *scan;
		gboolean  changed = FALSE;

		catalog = catalog_new ();
		if (catalog_load_from_disk (catalog, window->catalog_path, NULL)) {
			for (scan = catalog->list; scan; scan = scan->next) {
				char *entry = scan->data;
				if (strcmp (entry, old_name) == 0) {
					catalog_remove_item (catalog, old_name);
					catalog_add_item (catalog, new_name);
					changed = TRUE;
				}
			}
			if (changed)
				catalog_write_to_disk (catalog, NULL);
		}
		catalog_free (catalog);
	}

	pos = gth_file_list_pos_from_path (window->file_list, new_name);
	if (pos != -1)
		gth_file_list_delete_pos (window->file_list, pos);

	pos = gth_file_list_pos_from_path (window->file_list, old_name);
	if (pos != -1)
		gth_file_list_rename_pos (window->file_list, pos, new_name);

	if ((window->image_path != NULL) 
	    && strcmp (old_name, window->image_path) == 0) 
		window_load_image (window, new_name);
}


static gboolean
first_level_sub_directory (GThumbWindow *window,
			   const char   *current,
			   const char   *old_path)
{
	const char *old_name;
	int         current_l;
	int         old_path_l;

	current_l = strlen (current);
	old_path_l = strlen (old_path);

	if (old_path_l <= current_l + 1)
		return FALSE;

	if (strncmp (current, old_path, current_l) != 0)
		return FALSE;

	old_name = old_path + current_l + 1;

	return (strchr (old_name, '/') == NULL);
}


void
window_notify_directory_rename (GThumbWindow *window,
				const char  *old_name,
				const char  *new_name)
{
	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST) {
		if (strcmp (window->dir_list->path, old_name) == 0) 
			window_go_to_directory (window, new_name);
		else {
			const char *current = window->dir_list->path;

			/* a sub directory was renamed, refresh. */
			if (first_level_sub_directory (window, current, old_name)) 
				dir_list_remove_directory (window->dir_list, 
							   file_name_from_path (old_name));

			if (first_level_sub_directory (window, current, new_name)) 
				dir_list_add_directory (window->dir_list, 
							file_name_from_path (new_name));
		}
		
	} else if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST) {
		if (strcmp (window->catalog_list->path, old_name) == 0) 
			window_go_to_catalog_directory (window, new_name);
		else {
			const char *current = window->catalog_list->path;
			if (first_level_sub_directory (window, current, old_name))  
				window_update_catalog_list (window);
		}
	}

	if ((window->image_path != NULL) 
	    && (window->sidebar_content == GTH_SIDEBAR_DIR_LIST)
	    && (strncmp (window->image_path, 
			 old_name,
			 strlen (old_name)) == 0)) {
		char *new_image_name;

		new_image_name = g_strconcat (new_name,
					      window->image_path + strlen (old_name),
					      NULL);
		window_notify_file_rename (window, 
					   window->image_path,
					   new_image_name);
		g_free (new_image_name);
	}
}


void
window_notify_directory_delete (GThumbWindow *window,
				const char   *path)
{
	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST) {
		if (strcmp (window->dir_list->path, path) == 0) 
			window_go_up (window);
		else {
			const char *current = window->dir_list->path;
			if (first_level_sub_directory (window, current, path))
				dir_list_remove_directory (window->dir_list, 
							   file_name_from_path (path));
		}

	} else if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST) {
		if (strcmp (window->catalog_list->path, path) == 0) 
			window_go_up (window);
		else {
			const char *current = window->catalog_list->path;
			if (path_in_path (current, path))
				/* a sub directory got deleted, refresh. */
				window_update_catalog_list (window);
		}
	}

	if ((window->image_path != NULL) 
	    && (path_in_path (window->image_path, path))) {
		GList *list;
		
		list = g_list_append (NULL, window->image_path);
		window_notify_files_deleted (window, list);
		g_list_free (list);
	}
}


void
window_notify_directory_new (GThumbWindow *window,
			     const char  *path)
{
	if (window->sidebar_content == GTH_SIDEBAR_DIR_LIST) {
		const char *current = window->dir_list->path;
		if (first_level_sub_directory (window, current, path))
			dir_list_add_directory (window->dir_list, 
						file_name_from_path (path));

	} else if (window->sidebar_content == GTH_SIDEBAR_CATALOG_LIST) {
		const char *current = window->catalog_list->path;
		if (path_in_path (current, path))
			/* a sub directory was created, refresh. */
			window_update_catalog_list (window);
	}
}


void
window_notify_catalog_rename (GThumbWindow *window,
			      const char  *old_path,
			      const char  *new_path)
{
	char     *catalog_dir;
	gboolean  viewing_a_catalog;
	gboolean  current_cat_renamed;
	gboolean  renamed_cat_is_in_current_dir;

	if (window->sidebar_content != GTH_SIDEBAR_CATALOG_LIST) 
		return;

	if (window->catalog_list->path == NULL)
		return;

	catalog_dir = remove_level_from_path (window->catalog_list->path);
	viewing_a_catalog = (window->catalog_path != NULL);
	current_cat_renamed = ((window->catalog_path != NULL) && (strcmp (window->catalog_path, old_path) == 0));
	renamed_cat_is_in_current_dir = path_in_path (catalog_dir, new_path);

	if (! renamed_cat_is_in_current_dir) {
		g_free (catalog_dir);
		return;
	}

	if (! viewing_a_catalog) 
		window_go_to_catalog_directory (window, window->catalog_list->path);
	else {
		if (current_cat_renamed)
			window_go_to_catalog (window, new_path);
		else {
			GtkTreeIter iter;
			window_go_to_catalog_directory (window, window->catalog_list->path);

			/* reselect the current catalog. */
			if (catalog_list_get_iter_from_path (window->catalog_list, 
							     window->catalog_path, &iter))
				catalog_list_select_iter (window->catalog_list, &iter);
		}
	}

	g_free (catalog_dir);
}


void
window_notify_catalog_new (GThumbWindow *window,
			   const char   *path)
{
	char     *catalog_dir;
	gboolean  viewing_a_catalog;
	gboolean  created_cat_is_in_current_dir;

	if (window->sidebar_content != GTH_SIDEBAR_CATALOG_LIST) 
		return;

	if (window->catalog_list->path == NULL)
		return;

	viewing_a_catalog = (window->catalog_path != NULL);
	catalog_dir = remove_level_from_path (window->catalog_list->path);
	created_cat_is_in_current_dir = path_in_path (catalog_dir, path);

	if (! created_cat_is_in_current_dir) {
		g_free (catalog_dir);
		return;
	}

	window_go_to_catalog_directory (window, window->catalog_list->path);

	if (viewing_a_catalog) {
		GtkTreeIter iter;
			
		/* reselect the current catalog. */
		if (catalog_list_get_iter_from_path (window->catalog_list, 
						     window->catalog_path, 
						     &iter))
			catalog_list_select_iter (window->catalog_list, &iter);
	}

	g_free (catalog_dir);
}


void
window_notify_catalog_delete (GThumbWindow *window,
			      const char   *path)
{
	char     *catalog_dir;
	gboolean  viewing_a_catalog;
	gboolean  current_cat_deleted;
	gboolean  deleted_cat_is_in_current_dir;

	if (window->sidebar_content != GTH_SIDEBAR_CATALOG_LIST) 
		return;

	if (window->catalog_list->path == NULL)
		return;

	catalog_dir = remove_level_from_path (window->catalog_list->path);
	viewing_a_catalog = (window->catalog_path != NULL);
	current_cat_deleted = ((window->catalog_path != NULL) && (strcmp (window->catalog_path, path) == 0));
	deleted_cat_is_in_current_dir = path_in_path (catalog_dir, path);

	if (! deleted_cat_is_in_current_dir) {
		g_free (catalog_dir);
		return;
	}

	if (! viewing_a_catalog) 
		window_go_to_catalog_directory (window, window->catalog_list->path);
	else {
		if (current_cat_deleted) {
			window_go_to_catalog (window, NULL);
			window_go_to_catalog_directory (window, window->catalog_list->path);
		} else {
			GtkTreeIter iter;
			window_go_to_catalog_directory (window, window->catalog_list->path);
			
			/* reselect the current catalog. */
			if (catalog_list_get_iter_from_path (window->catalog_list, 
							     window->catalog_path, &iter))
				catalog_list_select_iter (window->catalog_list, &iter);
		}
	}

	g_free (catalog_dir);
}


void
window_notify_update_comment (GThumbWindow *window,
			      const char   *filename)
{
	int pos;

	g_return_if_fail (window != NULL);

	update_image_comment (window);

	pos = gth_file_list_pos_from_path (window->file_list, filename);
	if (pos != -1)
		gth_file_list_update_comment (window->file_list, pos);
}


void
window_notify_update_directory (GThumbWindow *window,
				const char   *dir_path)
{
	g_return_if_fail (window != NULL);

	if (window->monitor_enabled)
		return;
	
	if ((window->dir_list->path == NULL) 
	    || (strcmp (window->dir_list->path, dir_path) != 0)) 
		return;

	window_update_file_list (window);
}


gboolean
window_notify_update_layout_cb (gpointer data)
{
	GThumbWindow *window = data;
	GtkWidget    *paned1;      /* Main paned widget. */
	GtkWidget    *paned2;      /* Secondary paned widget. */
	int           paned1_pos;
	int           paned2_pos;
	gboolean      sidebar_visible;

	if (! GTK_IS_WIDGET (window->main_pane))
		return TRUE;

	if (window->update_layout_timeout != 0) {
		g_source_remove (window->update_layout_timeout);
		window->update_layout_timeout = 0;
	}

	sidebar_visible = window->sidebar_visible;
	if (! sidebar_visible) 
		window_show_sidebar (window);

	window->layout_type = eel_gconf_get_integer (PREF_UI_LAYOUT, 2);

	paned1_pos = gtk_paned_get_position (GTK_PANED (window->main_pane));
	paned2_pos = gtk_paned_get_position (GTK_PANED (window->content_pane));

	if (window->layout_type == 1) {
		paned1 = gtk_vpaned_new (); 
		paned2 = gtk_hpaned_new ();
	} else {
		paned1 = gtk_hpaned_new (); 
		paned2 = gtk_vpaned_new (); 
	}

	if (window->layout_type == 3)
		gtk_paned_pack2 (GTK_PANED (paned1), paned2, TRUE, FALSE);
	else
		gtk_paned_pack1 (GTK_PANED (paned1), paned2, FALSE, FALSE);

	if (window->layout_type == 3)
		gtk_widget_reparent (window->dir_list_pane, paned1);
	else
		gtk_widget_reparent (window->dir_list_pane, paned2);

	if (window->layout_type == 2) 
		gtk_widget_reparent (window->file_list_pane, paned1);
	else 
		gtk_widget_reparent (window->file_list_pane, paned2);

	if (window->layout_type <= 1) 
		gtk_widget_reparent (window->image_pane, paned1);
	else 
		gtk_widget_reparent (window->image_pane, paned2);

	gtk_paned_set_position (GTK_PANED (paned1), paned1_pos);
	gtk_paned_set_position (GTK_PANED (paned2), paned2_pos);

	gnome_app_set_contents (GNOME_APP (window->app), paned1);

	gtk_widget_show (paned1);
	gtk_widget_show (paned2);

	window->main_pane = paned1;
	window->content_pane = paned2;

	if (! sidebar_visible) 
		window_hide_sidebar (window);

	return FALSE;
}



void
window_notify_update_layout (GThumbWindow *window)
{
	if (window->update_layout_timeout != 0) {
		g_source_remove (window->update_layout_timeout);
		window->update_layout_timeout = 0;
	}

	window->update_layout_timeout = g_timeout_add (UPDATE_LAYOUT_DELAY, 
						       window_notify_update_layout_cb, 
						       window);
}


void
window_notify_update_toolbar_style (GThumbWindow *window)
{
	GthToolbarStyle toolbar_style;
	GtkToolbarStyle prop = GTK_TOOLBAR_BOTH;

	toolbar_style = pref_get_real_toolbar_style ();

	switch (toolbar_style) {
	case GTH_TOOLBAR_STYLE_TEXT_BELOW:
		prop = GTK_TOOLBAR_BOTH; break;
	case GTH_TOOLBAR_STYLE_TEXT_BESIDE:
		prop = GTK_TOOLBAR_BOTH_HORIZ; break;
	case GTH_TOOLBAR_STYLE_ICONS:
		prop = GTK_TOOLBAR_ICONS; break;
	case GTH_TOOLBAR_STYLE_TEXT:
		prop = GTK_TOOLBAR_TEXT; break;
	default:
		break;
	}

	gtk_toolbar_set_style (GTK_TOOLBAR (window->toolbar), prop);
}


void
window_notify_update_icon_theme (GThumbWindow *window)
{
	GdkPixbuf *icon;

	gth_file_view_update_icon_theme (window->file_list->view);
	dir_list_update_icon_theme (window->dir_list);

	window_update_bookmark_list (window);
	window_update_history_list (window);
	window_update_file_list (window);

	if (window->bookmarks_dlg != NULL)
		dlg_edit_bookmarks_update (window->bookmarks_dlg);

	/**/

	icon = gtk_widget_render_icon (window->app,
				       GTK_STOCK_GO_BACK,
				       GTK_ICON_SIZE_MENU,
				       "");
	e_combo_button_set_icon (E_COMBO_BUTTON (window->go_back_toolbar_button), icon);
	g_object_unref (icon);
}


/* -- image operations -- */


static void
pixbuf_op_done_cb (GthPixbufOp   *pixop,
		   gboolean       completed,
		   GThumbWindow  *window)
{
	ImageViewer *viewer = IMAGE_VIEWER (window->viewer);

	if (completed) {
		image_viewer_set_pixbuf (viewer, window->pixop->dest);
		window_image_set_modified (window, TRUE);
	}

	g_object_unref (window->pixop);
	window->pixop = NULL;

	if (window->progress_dialog != NULL) 
		gtk_widget_hide (window->progress_dialog);
}


static void
pixbuf_op_progress_cb (GthPixbufOp  *pixop,
		       float         p, 
		       GThumbWindow *window)
{
	if (window->progress_dialog != NULL) 
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->progress_progressbar), p);
}


static int
window__display_progress_dialog (gpointer data)
{
	GThumbWindow *window = data;

	if (window->progress_timeout != 0) {
		g_source_remove (window->progress_timeout);
		window->progress_timeout = 0;
	}

	if (window->pixop != NULL)
		gtk_widget_show (window->progress_dialog);

	return FALSE;
}


void
window_exec_pixbuf_op (GThumbWindow *window,
		       GthPixbufOp  *pixop)
{
	window->pixop = pixop;
	g_object_ref (window->pixop);

	gtk_label_set_text (GTK_LABEL (window->progress_info),
			    _("Wait please..."));

	g_signal_connect (G_OBJECT (pixop),
			  "pixbuf_op_done",
			  G_CALLBACK (pixbuf_op_done_cb),
			  window);
	g_signal_connect (G_OBJECT (pixop),
			  "pixbuf_op_progress",
			  G_CALLBACK (pixbuf_op_progress_cb),
			  window);
	
	if (window->progress_dialog != NULL)
		window->progress_timeout = g_timeout_add (DISPLAY_PROGRESS_DELAY, window__display_progress_dialog, window);

	gth_pixbuf_op_start (window->pixop);
}
