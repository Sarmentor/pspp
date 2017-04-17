/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010, 2011, 2012 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef __PSPPIRE_DATA_EDITOR_H__
#define __PSPPIRE_DATA_EDITOR_H__

/* PsppireDataEditor is a GtkNotebook for editing a single PSPP dataset.

   PsppireDataEditor has two tabs that normally contain a PsppireDataSheet and
   a PsppireVarSheet.  The user can choose to "split" the PsppireDataSheet view
   into four views within the single tab.  PsppireDataEditor also adds some
   decorations above the PsppireDataSheet to note the current cell and allow
   the current cell to be edited.

   PsppireDataEditor's normal parent in the widget hierarchy is
   PsppireDataWindow. */

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "psppire-data-store.h"

G_BEGIN_DECLS

#define PSPPIRE_DATA_EDITOR_TYPE            (psppire_data_editor_get_type ())
#define PSPPIRE_DATA_EDITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_DATA_EDITOR_TYPE, PsppireDataEditor))
#define PSPPIRE_DATA_EDITOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_DATA_EDITOR_TYPE, PsppireDataEditorClass))
#define PSPPIRE_IS_DATA_EDITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_DATA_EDITOR_TYPE))
#define PSPPIRE_IS_DATA_EDITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_DATA_EDITOR_TYPE))


typedef struct _PsppireDataEditor       PsppireDataEditor;
typedef struct _PsppireDataEditorClass  PsppireDataEditorClass;

/* All members are private. */
struct _PsppireDataEditor
{
  GtkNotebook parent;

  /* <private> */
  PsppireDataStore *data_store;
  PsppireDict *dict;

  /* Font to use in var sheet and data sheet(s), NULL to use system default. */
  struct _PangoFontDescription *font;

  /* Variable sheet tab. */
  GtkWidget *var_sheet;
  GtkWidget *data_sheet;

  /* Data sheet popup menu */
  GtkWidget *data_sheet_cases_row_popup;
  GtkWidget *data_clear_cases_menu_item;


  GtkWidget *data_sheet_cases_column_popup;
  GtkWidget *data_clear_variables_menu_item;
  GtkWidget *data_sort_ascending_menu_item;
  GtkWidget *data_sort_descending_menu_item;

  /* Var sheet popup menu */
  GtkWidget *var_sheet_row_popup;
  GtkWidget *var_clear_variables_menu_item;

  /* Data sheet tab. */
  GtkWidget *vbox;             /* Top-level widget in tab. */
  GtkWidget *cell_ref_label;   /* GtkLabel that shows selected case and var. */
  GtkWidget *datum_entry;      /* PsppireValueEntry for editing current cell. */

  gboolean split;              /* True if the sheets are in split view. */

  GtkCellRenderer *value_label_renderer;
};

struct _PsppireDataEditorClass
{
  GtkNotebookClass parent_class;
};


GType          psppire_data_editor_get_type        (void);
GtkWidget*     psppire_data_editor_new             (PsppireDict *, PsppireDataStore *);
void           psppire_data_editor_show_grid       (PsppireDataEditor *, gboolean);
void           psppire_data_editor_set_font        (PsppireDataEditor *, PangoFontDescription *);
void           psppire_data_editor_split_window    (PsppireDataEditor *, gboolean );

void psppire_data_editor_goto_variable               (PsppireDataEditor *, gint dict_index);
void psppire_data_editor_data_delete_variables       (PsppireDataEditor *de);
void psppire_data_editor_var_delete_variables        (PsppireDataEditor *de);
void psppire_data_editor_insert_new_case_at_posn     (PsppireDataEditor *de, gint posn);
void psppire_data_editor_insert_new_variable_at_posn (PsppireDataEditor *de, gint posn);

struct _PsppireDataSheet *psppire_data_editor_get_active_data_sheet (PsppireDataEditor *);

enum {PSPPIRE_DATA_EDITOR_DATA_VIEW = 0, PSPPIRE_DATA_EDITOR_VARIABLE_VIEW};

void psppire_data_editor_paste (PsppireDataEditor *de);

G_END_DECLS

#endif /* __PSPPIRE_DATA_EDITOR_H__ */
