/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2019 Free Software Foundation, Inc.
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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GTH_SHORTCUT_H
#define GTH_SHORTCUT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTH_SHORTCUT_CONTEXT_INTERNAL -1

#define GTH_SHORTCUT_CATEGORY_HIDDEN "hidden"
#define GTH_SHORTCUT_CATEGORY_UI "ui"
#define GTH_SHORTCUT_CATEGORY_FILE_NAVIGATION "file-navigation"
#define GTH_SHORTCUT_CATEGORY_FILE_EDIT "file-edit"
#define GTH_SHORTCUT_CATEGORY_IMAGE_VIEW "image-view"
#define GTH_SHORTCUT_CATEGORY_IMAGE_EDIT "image-edit"
#define GTH_SHORTCUT_CATEGORY_SLIDESHOW "slideshow"


typedef struct {
	char *id;
	char *display_name;
} GthShortcutCategory;


typedef struct {
	char            *action_name;
	char            *description;
	int              context;
	char            *category;
	char            *default_accelerator;
	char            *accelerator;
	char            *label;
	guint            keyval;
	GdkModifierType  modifiers;
} GthShortcut;


GthShortcut * gth_shortcut_new			(void);
GthShortcut * gth_shortcut_dup			(const GthShortcut *shortcut);
void          gth_shortcut_free			(GthShortcut       *shortcut);
void          gth_shortcut_set_key		(GthShortcut       *shortcut,
						 guint              keyval,
						 GdkModifierType    modifiers);
void          gth_shortcut_set_accelerator	(GthShortcut       *shortcut,
						 const char        *name);
GthShortcut * gth_shortcut_array_find           (GPtrArray         *shortcuts_v,
						 int                context,
						 guint              keycode,
						 GdkModifierType    modifiers);
gboolean      gth_shortcut_valid                (guint              keycode,
						 GdkModifierType    modifiers);
gboolean      gth_shortcuts_write_to_file       (GPtrArray         *shortcuts_v,
						 GError           **error);
gboolean      gth_shortcuts_load_from_file      (GPtrArray         *shortcuts_v,
						 GHashTable        *shortcuts,
						 GError           **error);

G_END_DECLS

#endif /* GTH_SHORTCUT_H */