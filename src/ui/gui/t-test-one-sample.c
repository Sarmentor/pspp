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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <config.h>
#include <gtk/gtk.h>
#include "t-test-one-sample.h"
#include "psppire-dict.h"
#include "psppire-var-view.h"
#include "builder-wrapper.h"
#include "psppire-data-window.h"
#include "psppire-dialog.h"
#include "dialog-common.h"
#include "dict-display.h"
#include "widget-io.h"
#include "executor.h"

#include "t-test-options.h"
#include "helper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct tt_one_sample_dialog
{
  PsppireDict *dict;
  GtkWidget *vars_treeview;
  GtkWidget *test_value_entry;
  struct tt_options_dialog *opt;
};


static gchar *
generate_syntax (const struct tt_one_sample_dialog *d)
{
  gchar *text;

  GString *str = g_string_new ("T-TEST ");

  g_string_append_printf (str, "/TESTVAL=%s",
			  gtk_entry_get_text (GTK_ENTRY (d->test_value_entry)));

  g_string_append (str, "\n\t/VARIABLES=");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (d->vars_treeview), 0, str);

  tt_options_dialog_append_syntax (d->opt, str);

  g_string_append (str, ".\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}



static void
refresh (const struct tt_one_sample_dialog *d)
{
  GtkTreeModel *model =
    gtk_tree_view_get_model (GTK_TREE_VIEW (d->vars_treeview));

  gtk_entry_set_text (GTK_ENTRY (d->test_value_entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));
}



static gboolean
dialog_state_valid (gpointer data)
{
  gchar *s = NULL;
  const gchar *text;
  struct tt_one_sample_dialog *tt_d = data;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (tt_d->vars_treeview));

  GtkTreeIter notused;

  text = gtk_entry_get_text (GTK_ENTRY (tt_d->test_value_entry));

  if ( 0 == strcmp ("", text))
    return FALSE;

  /* Check to see if the entry is numeric */
  g_strtod (text, &s);

  if (s - text != strlen (text))
    return FALSE;


  if ( 0 == gtk_tree_model_get_iter_first (vars, &notused))
    return FALSE;

  return TRUE;
}


/* Pops up the dialog box */
void
t_test_one_sample_dialog (PsppireDataWindow *de)
{
  struct tt_one_sample_dialog tt_d;
  gint response;

  GtkBuilder *xml = builder_new ("t-test.ui");

  GtkWidget *dict_view =
    get_widget_assert (xml, "one-sample-t-test-treeview2");

  GtkWidget *options_button =
    get_widget_assert (xml, "button1");

  GtkWidget *dialog = get_widget_assert (xml, "t-test-one-sample-dialog");

  g_object_get (de->data_editor, "dictionary", &tt_d.dict, NULL);
  tt_d.vars_treeview = get_widget_assert (xml, "one-sample-t-test-treeview1");
  tt_d.test_value_entry = get_widget_assert (xml, "test-value-entry");
  tt_d.opt = tt_options_dialog_create (GTK_WINDOW (de));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_set (dict_view, "model",
		tt_d.dict,
		"predicate",
		var_is_numeric, NULL);

  g_signal_connect_swapped (dialog, "refresh",
			    G_CALLBACK (refresh),  &tt_d);


  g_signal_connect_swapped (options_button, "clicked",
			    G_CALLBACK (tt_options_dialog_run), tt_d.opt);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &tt_d);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (&tt_d)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&tt_d)));
      break;
    default:
      break;
    }


  g_object_unref (xml);
  tt_options_dialog_destroy (tt_d.opt);
}


