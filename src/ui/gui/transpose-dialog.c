/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012  Free Software Foundation

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

#include <config.h>

#include "transpose-dialog.h"
#include "psppire-selector.h"
#include "psppire-var-view.h"
#include "psppire-dialog.h"
#include "executor.h"
#include "psppire-data-window.h"
#include "dict-display.h"
#include "builder-wrapper.h"
#include "helper.h"

#include "dialog-common.h"

#include <gtk/gtk.h>

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static gchar * generate_syntax (PsppireDict *dict, GtkBuilder *xml);

static void
refresh (PsppireDialog *dialog, gpointer data)
{
  GtkBuilder *xml = data;
  GtkWidget *dest = get_widget_assert (xml, "variables-treeview");
  GtkWidget *entry = get_widget_assert (xml, "new-name-entry");
  GtkTreeModel *dmodel = gtk_tree_view_get_model (GTK_TREE_VIEW (dest));

  gtk_list_store_clear (GTK_LIST_STORE (dmodel));
  gtk_entry_set_text (GTK_ENTRY (entry), "");
}

static gboolean
dialog_state_valid (gpointer data)
{
  GtkBuilder *xml = data;

  GtkWidget *tv = get_widget_assert (xml, "variables-treeview");
  GtkWidget *entry = get_widget_assert (xml, "new-name-entry");

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (tv));

  gint n_rows = gtk_tree_model_iter_n_children  (model, NULL);

  if ( n_rows == 0 )
    return FALSE;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (entry))))
    return FALSE;

  return TRUE;
}


void
transpose_dialog (PsppireDataWindow *de)
{
  gint response ;
  PsppireDict *dict = NULL;

  GtkBuilder *xml = builder_new ("psppire.ui");

  GtkWidget *dialog = get_widget_assert (xml, "transpose-dialog");
  GtkWidget *source = get_widget_assert (xml, "source-treeview");
  GtkWidget *selector2 = get_widget_assert (xml, "psppire-selector3");

  g_object_get (de->data_editor, "dictionary", &dict, NULL);
  g_object_set (source, "model", dict, NULL);

  psppire_selector_set_filter_func (PSPPIRE_SELECTOR (selector2),
				 is_currently_in_entry);


  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  xml);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, xml);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (dict, xml)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (dict, xml);
        paste_syntax_to_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}


  /*
     FLIP /VARIABLES=var_list /NEWNAMES=var_name.
  */
static gchar *
generate_syntax (PsppireDict *dict, GtkBuilder *xml)
{
  const gchar *text;
  GString *string = g_string_new ("FLIP");
  gchar *syntax ;

  GtkWidget *dest = get_widget_assert (xml, "variables-treeview");
  GtkWidget *entry = get_widget_assert (xml, "new-name-entry");

  g_string_append (string, " /VARIABLES = ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (dest), 0, string);

  text = gtk_entry_get_text (GTK_ENTRY (entry));

  if ( text)
    g_string_append_printf (string, " /NEWNAME = %s", text);

  g_string_append (string, ".");

  syntax = string->str;

  g_string_free (string, FALSE);

  return syntax;
}
