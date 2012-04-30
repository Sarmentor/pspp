/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2011, 2012  Free Software Foundation

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
#include "t-test-independent-samples-dialog.h"
#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "psppire-var-view.h"
#include "psppire-value-entry.h"
#include "executor.h"
#include "psppire-data-window.h"
#include "psppire-dialog.h"
#include "dialog-common.h"
#include "dict-display.h"
#include "widget-io.h"
#include "t-test-options.h"
#include <ui/syntax-gen.h>

#include "builder-wrapper.h"
#include "helper.h"

#include <gl/xalloc.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


enum group_definition
  {
    GROUPS_UNDEF,
    GROUPS_VALUES,
    GROUPS_CUT_POINT
  };

struct tt_groups_dialog
{
  GtkWidget *dialog;
  GtkWidget *label;
  GtkWidget *table1;
  GtkWidget *table2;
  GtkWidget *hbox1;

  GtkWidget *values_toggle_button;
  GtkWidget *cut_point_toggle_button;

  GtkWidget *grp_entry[2];
  GtkWidget *cut_point_entry;

  enum group_definition group_defn;

  union value grp_val[2];
  union value cut_point;
};

static void
set_group_criterion_type (GtkToggleButton *button,
			  struct tt_groups_dialog *groups)
{
  gboolean by_values = gtk_toggle_button_get_active (button);

  gtk_widget_set_sensitive (groups->label, by_values);
  gtk_widget_set_sensitive (groups->table2, by_values);

  gtk_widget_set_sensitive (groups->hbox1, !by_values);
}

static void
tt_groups_dialog_destroy (struct tt_groups_dialog *grps)
{

  g_object_unref (grps->table1);
  g_object_unref (grps->table2);

  g_free (grps);
}

static struct tt_groups_dialog *
tt_groups_dialog_create (GtkBuilder *xml, GtkWindow *parent)
{
  struct tt_groups_dialog *grps = xmalloc (sizeof (*grps));

  grps->group_defn = GROUPS_UNDEF;

  grps->dialog = get_widget_assert (xml, "define-groups-dialog");
  grps->table1 = get_widget_assert (xml, "table1");
  grps->table2 = get_widget_assert (xml, "table2");
  grps->label  = get_widget_assert (xml, "label4");
  grps->hbox1  = get_widget_assert (xml, "hbox1");

  grps->grp_entry[0] = get_widget_assert (xml, "group1-entry");
  grps->grp_entry[1] = get_widget_assert (xml, "group2-entry");
  grps->cut_point_entry = get_widget_assert (xml, "cut-point-entry");

  grps->cut_point_toggle_button = get_widget_assert (xml, "radiobutton4");
  grps->values_toggle_button = get_widget_assert (xml, "radiobutton3");

  g_object_ref (grps->table1);
  g_object_ref (grps->table2);

  g_signal_connect (grps->values_toggle_button, "toggled",
		    G_CALLBACK (set_group_criterion_type), grps);

  gtk_window_set_transient_for (GTK_WINDOW (grps->dialog), parent);

  return grps;
}


struct tt_indep_samples_dialog
{
  GtkBuilder *xml;  /* The xml that generated the widgets */
  GtkWidget *dialog;
  PsppireDict *dict;
  GtkWidget *define_groups_button;
  GtkWidget *groups_entry;

  const struct variable *grp_var;

  struct tt_groups_dialog *grps;
  struct tt_options_dialog *opts;
};


/* Called whenever the group variable entry widget's contents change */
static void
on_grp_var_change (GtkEntry *entry,
			       struct tt_indep_samples_dialog *tt_d)
{
  const gchar *text = gtk_entry_get_text (entry);

  const struct variable *v = psppire_dict_lookup_var (tt_d->dict, text);

  gtk_widget_set_sensitive (tt_d->define_groups_button, v != NULL);

  if (tt_d->grp_var)
    {
      int width = var_get_width (tt_d->grp_var);
      value_destroy (&tt_d->grps->cut_point, width);
      value_destroy (&tt_d->grps->grp_val[0], width);
      value_destroy (&tt_d->grps->grp_val[1], width);
    }

  if (v)
    {
      const int width = var_get_width (v);
      value_init (&tt_d->grps->cut_point, width);
      value_init (&tt_d->grps->grp_val[0], width);
      value_init (&tt_d->grps->grp_val[1], width);

      if (width == 0)
        {
          tt_d->grps->cut_point.f  = SYSMIS;
          tt_d->grps->grp_val[0].f = SYSMIS;
          tt_d->grps->grp_val[1].f = SYSMIS;
        }
      else
        {
          tt_d->grps->cut_point.short_string[0] = '\0';
          tt_d->grps->grp_val[0].short_string[0] = '\0';
          tt_d->grps->grp_val[1].short_string[0] = '\0';
        }
    }

  tt_d->grp_var = v;
}


static gchar *
generate_syntax (const struct tt_indep_samples_dialog *d)
{
  gchar *text;

  GtkWidget *tv =
    get_widget_assert (d->xml, "indep-samples-t-test-treeview2");

  GString *str = g_string_new ("T-TEST /VARIABLES=");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (tv), 0, str);

  g_string_append (str, "\n\t/GROUPS=");

  g_string_append (str, var_get_name (d->grp_var));

  if (d->grps->group_defn != GROUPS_UNDEF)
    {
      g_string_append (str, "(");

      {
        const union value *val = 
          (d->grps->group_defn == GROUPS_VALUES) ?
          &d->grps->grp_val[0] :
          &d->grps->cut_point;

        struct string strx;        
        ds_init_empty (&strx);
        syntax_gen_value (&strx, val, var_get_width (d->grp_var),
                          var_get_print_format (d->grp_var));
      
        g_string_append (str, ds_cstr (&strx));
        ds_destroy (&strx);
      }

      if (d->grps->group_defn == GROUPS_VALUES)
	{
	  g_string_append (str, ",");

          {
            struct string strx;
            ds_init_empty (&strx);
            
            syntax_gen_value (&strx, &d->grps->grp_val[1], var_get_width (d->grp_var),
                              var_get_print_format (d->grp_var));
            
            g_string_append (str, ds_cstr (&strx));
            ds_destroy (&strx);
          }
	}

      g_string_append (str, ")");
    }

  tt_options_dialog_append_syntax (d->opts, str);

  g_string_append (str, ".\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}

static void
refresh (struct tt_indep_samples_dialog *ttd)
{
  GtkWidget *tv =
    get_widget_assert (ttd->xml, "indep-samples-t-test-treeview2");

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (tv));

  gtk_entry_set_text (GTK_ENTRY (ttd->groups_entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));

  gtk_widget_set_sensitive (ttd->define_groups_button, FALSE);
}


/* Return TRUE if VE contains a text which is not valid for VAR or if it
   contains the SYSMIS value */
static gboolean
value_entry_contains_invalid (PsppireValueEntry *ve, const struct variable *var)
{
  union value val;
  gboolean result = FALSE;
  const int width = var_get_width (var);
  value_init (&val, width);

  if ( psppire_value_entry_get_value (ve, &val, width))
    {
      if (var_is_value_missing (var, &val, MV_SYSTEM))
        {
          result = TRUE;
        }
    }
  else
    result = TRUE;

  value_destroy (&val, width);


  return result;
}


/* Returns TRUE iff the define groups subdialog has a
   state which defines a valid group criterion */
static gboolean
define_groups_state_valid (gpointer data)
{
  struct tt_indep_samples_dialog *dialog = data;

  if (gtk_toggle_button_get_active
      (GTK_TOGGLE_BUTTON (dialog->grps->values_toggle_button)))
    {
      if (value_entry_contains_invalid (PSPPIRE_VALUE_ENTRY (dialog->grps->grp_entry[0]), dialog->grp_var))
        return FALSE;

      if (value_entry_contains_invalid (PSPPIRE_VALUE_ENTRY (dialog->grps->grp_entry[1]), dialog->grp_var))
        return FALSE;
    }
  else
    {
      if (value_entry_contains_invalid (PSPPIRE_VALUE_ENTRY (dialog->grps->cut_point_entry), dialog->grp_var))
        return FALSE;
    }

  return TRUE;
}

static void
run_define_groups (struct tt_indep_samples_dialog *ttd)
{
  struct tt_groups_dialog *grps = ttd->grps;

  gint response;

  GtkWidget *box = get_widget_assert (ttd->xml, "dialog-hbox2");

  const gchar *text = gtk_entry_get_text (GTK_ENTRY (ttd->groups_entry));

  const struct variable *v = psppire_dict_lookup_var (ttd->dict, text);

  if ( grps->table2->parent)
    gtk_container_remove (GTK_CONTAINER (grps->table2->parent), grps->table2);

  if ( grps->table1->parent)
    gtk_container_remove (GTK_CONTAINER (grps->table1->parent), grps->table1);


  if ( var_is_numeric (v))
    {
      gtk_table_attach_defaults (GTK_TABLE (grps->table1), grps->table2,
				 1, 2, 1, 2);

      gtk_container_add (GTK_CONTAINER (box), grps->table1);
    }
  else
    {
      gtk_container_add (GTK_CONTAINER (box), grps->table2);
      grps->group_defn = GROUPS_VALUES;
    }


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (grps->dialog),
				      define_groups_state_valid, ttd);

  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (grps->grp_entry[0]), ttd->grp_var);
  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (grps->grp_entry[1]), ttd->grp_var);
  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (grps->cut_point_entry), ttd->grp_var);

  if ( grps->group_defn != GROUPS_CUT_POINT )
    {
      gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON (grps->cut_point_toggle_button), TRUE);

      gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON (grps->values_toggle_button), TRUE);
    }
  else
    {
      gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON (grps->values_toggle_button), TRUE);

      gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON (grps->cut_point_toggle_button), TRUE);
    }

  g_signal_emit_by_name (grps->grp_entry[0], "changed");
  g_signal_emit_by_name (grps->grp_entry[1], "changed");
  g_signal_emit_by_name (grps->cut_point_entry, "changed");

  response = psppire_dialog_run (PSPPIRE_DIALOG (grps->dialog));

  if (response == PSPPIRE_RESPONSE_CONTINUE)
    {
      const int width = var_get_width (ttd->grp_var);

      if (gtk_toggle_button_get_active
	  (GTK_TOGGLE_BUTTON (grps->values_toggle_button)))
	{
	  grps->group_defn = GROUPS_VALUES;

          psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (grps->grp_entry[0]), &grps->grp_val[0], width);
          psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (grps->grp_entry[1]), &grps->grp_val[1], width);
	}
      else
	{
	  grps->group_defn = GROUPS_CUT_POINT;

          psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (grps->cut_point_entry), &grps->cut_point, width);
	}

      psppire_dialog_notify_change (PSPPIRE_DIALOG (ttd->dialog));
    }
}



static gboolean
dialog_state_valid (gpointer data)
{
  struct tt_indep_samples_dialog *tt_d = data;

  GtkWidget *tv_vars =
    get_widget_assert (tt_d->xml, "indep-samples-t-test-treeview2");

  GtkTreeModel *vars = gtk_tree_view_get_model (GTK_TREE_VIEW (tv_vars));

  GtkTreeIter notused;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (tt_d->groups_entry))))
    return FALSE;

  if ( 0 == gtk_tree_model_get_iter_first (vars, &notused))
    return FALSE;

  if ( tt_d->grps->group_defn == GROUPS_UNDEF)
    return FALSE;

  return TRUE;
}

/* Pops up the dialog box */
void
t_test_independent_samples_dialog (PsppireDataWindow *de)
{
  struct tt_indep_samples_dialog tt_d;
  gint response;

  PsppireVarStore *vs = NULL;

  GtkBuilder *xml = builder_new ("t-test.ui");

  GtkWidget *dict_view =
    get_widget_assert (xml, "indep-samples-t-test-treeview1");

  GtkWidget *selector2 =
    get_widget_assert (xml, "indep-samples-t-test-selector2");

  GtkWidget *selector1 =
    get_widget_assert (xml, "indep-samples-t-test-selector1");

  GtkWidget *options_button =
    get_widget_assert (xml, "indep-samples-t-test-options-button");

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  tt_d.dialog = get_widget_assert (xml, "t-test-independent-samples-dialog");
  tt_d.xml = xml;
  g_object_get (vs, "dictionary", &tt_d.dict, NULL);

  tt_d.define_groups_button = get_widget_assert (xml, "define-groups-button");
  tt_d.groups_entry = get_widget_assert (xml, "indep-samples-t-test-entry");
  tt_d.opts = tt_options_dialog_create (GTK_WINDOW (de));
  tt_d.grps = tt_groups_dialog_create (xml, GTK_WINDOW (de));

  tt_d.grp_var = NULL;

  gtk_window_set_transient_for (GTK_WINDOW (tt_d.dialog), GTK_WINDOW (de));

  g_object_set (dict_view, "model", tt_d.dict, NULL);

  psppire_selector_set_allow (PSPPIRE_SELECTOR (selector1),
			      numeric_only);


  psppire_selector_set_filter_func (PSPPIRE_SELECTOR (selector2),
				 is_currently_in_entry);

  g_signal_connect_swapped (tt_d.define_groups_button, "clicked",
			    G_CALLBACK (run_define_groups), &tt_d);


  g_signal_connect_swapped (options_button, "clicked",
			    G_CALLBACK (tt_options_dialog_run), tt_d.opts);


  g_signal_connect_swapped (tt_d.dialog, "refresh", G_CALLBACK (refresh),
			    &tt_d);

  g_signal_connect (tt_d.groups_entry, "changed",
		    G_CALLBACK (on_grp_var_change), &tt_d);


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (tt_d.dialog),
				      dialog_state_valid, &tt_d);

  response = psppire_dialog_run (PSPPIRE_DIALOG (tt_d.dialog));

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

  if (tt_d.grp_var)
  {
    int width = var_get_width (tt_d.grp_var);
    value_destroy (&tt_d.grps->cut_point, width);
    value_destroy (&tt_d.grps->grp_val[0], width);
    value_destroy (&tt_d.grps->grp_val[1], width);
  }

  tt_options_dialog_destroy (tt_d.opts);
  tt_groups_dialog_destroy (tt_d.grps);

  g_object_unref (xml);
}


