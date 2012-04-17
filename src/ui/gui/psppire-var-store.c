/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2009, 2010, 2011, 2012  Free Software Foundation

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
#include <string.h>
#include <stdlib.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <libpspp/i18n.h>

#include <gobject/gvaluecollector.h>

#include <ui/gui/sheet/psppire-sheetmodel.h>

#include "psppire-var-store.h"
#include "helper.h"

#include <data/dictionary.h>
#include <data/variable.h>
#include <data/format.h>
#include <data/missing-values.h>

#include "val-labs-dialog.h"
#include "missing-val-dialog.h"
#include <data/value-labels.h>

#include "var-display.h"

static void
var_change_callback (GtkWidget *w, gint n, gpointer data)
{
  PsppireSheetModel *model = PSPPIRE_SHEET_MODEL (data);

  psppire_sheet_model_range_changed (model,
				 n, 0, n, PSPPIRE_VAR_STORE_n_COLS);
}


static void
var_delete_callback (GtkWidget *w, const struct variable *var UNUSED,
                     gint dict_idx, gint case_idx UNUSED, gpointer data)
{
  PsppireSheetModel *model = PSPPIRE_SHEET_MODEL (data);

  psppire_sheet_model_rows_deleted (model, dict_idx, 1);
}



static void
var_insert_callback (GtkWidget *w, glong row, gpointer data)
{
  PsppireSheetModel *model = PSPPIRE_SHEET_MODEL (data);

  psppire_sheet_model_rows_inserted (model, row, 1);
}

static void
refresh (PsppireDict  *d, gpointer data)
{
  PsppireVarStore *vs = data;

  psppire_sheet_model_range_changed (PSPPIRE_SHEET_MODEL (vs), -1, -1, -1, -1);
}

enum
  {
    PROP_0,
    PSPPIRE_VAR_STORE_FORMAT_TYPE,
    PSPPIRE_VAR_STORE_DICT
  };

static void         psppire_var_store_init            (PsppireVarStore      *var_store);
static void         psppire_var_store_class_init      (PsppireVarStoreClass *class);
static void         psppire_var_store_sheet_model_init (PsppireSheetModelIface *iface);
static void         psppire_var_store_finalize        (GObject           *object);
static void         psppire_var_store_dispose        (GObject           *object);


static gchar *psppire_var_store_get_string (const PsppireSheetModel *sheet_model, glong row, glong column);

static gboolean  psppire_var_store_clear (PsppireSheetModel *model,  glong row, glong col);


static gboolean psppire_var_store_set_string (PsppireSheetModel *model,
					  const gchar *text, glong row, glong column);

static glong psppire_var_store_get_row_count (const PsppireSheetModel * model);
static glong psppire_var_store_get_column_count (const PsppireSheetModel * model);

static gchar *text_for_column (PsppireVarStore *vs, const struct variable *pv,
			       gint c, GError **err);


static GObjectClass *parent_class = NULL;

GType
psppire_var_store_format_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0)
    {
      static const GEnumValue values[] =
	{
	  { PSPPIRE_VAR_STORE_INPUT_FORMATS,
            "PSPPIRE_VAR_STORE_INPUT_FORMATS",
            "input" },
	  { PSPPIRE_VAR_STORE_OUTPUT_FORMATS,
            "PSPPIRE_VAR_STORE_OUTPUT_FORMATS",
            "output" },
	  { 0, NULL, NULL }
	};

      etype = g_enum_register_static
	(g_intern_static_string ("PsppireVarStoreFormatType"), values);

    }
  return etype;
}

GType
psppire_var_store_get_type (void)
{
  static GType var_store_type = 0;

  if (!var_store_type)
    {
      static const GTypeInfo var_store_info =
      {
	sizeof (PsppireVarStoreClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) psppire_var_store_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (PsppireVarStore),
	0,
        (GInstanceInitFunc) psppire_var_store_init,
      };

      static const GInterfaceInfo sheet_model_info =
      {
	(GInterfaceInitFunc) psppire_var_store_sheet_model_init,
	NULL,
	NULL
      };

      var_store_type = g_type_register_static (G_TYPE_OBJECT, "PsppireVarStore", &var_store_info, 0);

      g_type_add_interface_static (var_store_type,
				   PSPPIRE_TYPE_SHEET_MODEL,
				   &sheet_model_info);
    }

  return var_store_type;
}

static void
psppire_var_store_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PsppireVarStore *self = (PsppireVarStore *) object;

  switch (property_id)
    {
    case PSPPIRE_VAR_STORE_FORMAT_TYPE:
      self->format_type = g_value_get_enum (value);
      break;

    case PSPPIRE_VAR_STORE_DICT:
      if ( self->dictionary)
	g_object_unref (self->dictionary);
      self->dictionary = g_value_dup_object (value);
      g_signal_connect (self->dictionary, "variable-changed", G_CALLBACK (var_change_callback),
			self);

      g_signal_connect (self->dictionary, "variable-deleted", G_CALLBACK (var_delete_callback),
			self);

      g_signal_connect (self->dictionary, "variable-inserted",
			G_CALLBACK (var_insert_callback), self);

      g_signal_connect (self->dictionary, "backend-changed", G_CALLBACK (refresh),
			self);

      /* The entire model has changed */
      psppire_sheet_model_range_changed (PSPPIRE_SHEET_MODEL (self), -1, -1, -1, -1);

      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
    }
}

static void
psppire_var_store_get_property (GObject      *object,
                        guint         property_id,
                        GValue       *value,
                        GParamSpec   *pspec)
{
  PsppireVarStore *self = (PsppireVarStore *) object;

  switch (property_id)
    {
    case PSPPIRE_VAR_STORE_FORMAT_TYPE:
      g_value_set_enum (value, self->format_type);
      break;

    case PSPPIRE_VAR_STORE_DICT:
      g_value_take_object (value, self->dictionary);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void
psppire_var_store_class_init (PsppireVarStoreClass *class)
{
  GObjectClass *object_class;
  GParamSpec *format_pspec;
  GParamSpec *dict_pspec;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_var_store_finalize;
  object_class->dispose = psppire_var_store_dispose;
  object_class->set_property = psppire_var_store_set_property;
  object_class->get_property = psppire_var_store_get_property;

  format_pspec = g_param_spec_enum ("format-type",
                             "Variable format type",
                             ("Whether variables have input or output "
                              "formats"),
                             PSPPIRE_TYPE_VAR_STORE_FORMAT_TYPE,
                             PSPPIRE_VAR_STORE_OUTPUT_FORMATS,
                             G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PSPPIRE_VAR_STORE_FORMAT_TYPE,
                                   format_pspec);

  dict_pspec = g_param_spec_object ("dictionary",
				    "Dictionary",
				    "The PsppireDict represented by this var store",
				    PSPPIRE_TYPE_DICT,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
				    
  g_object_class_install_property (object_class,
                                   PSPPIRE_VAR_STORE_DICT,
                                   dict_pspec);
}

#define DISABLED_COLOR "gray"

static void
psppire_var_store_init (PsppireVarStore *var_store)
{
  if ( ! gdk_color_parse (DISABLED_COLOR, &var_store->disabled))
	g_critical ("Could not parse color `%s'", DISABLED_COLOR);

  var_store->dictionary = NULL;
  var_store->format_type = PSPPIRE_VAR_STORE_OUTPUT_FORMATS;
}

static gboolean
psppire_var_store_item_editable (PsppireVarStore *var_store, glong row, glong column)
{
  const struct fmt_spec *write_spec ;

  struct variable *pv = psppire_var_store_get_var (var_store, row);

  if ( !pv )
    return TRUE;

  if ( var_is_alpha (pv) && column == PSPPIRE_VAR_STORE_COL_DECIMALS )
    return FALSE;

  write_spec = var_get_print_format (pv);

  switch ( write_spec->type )
    {
    case FMT_DATE:
    case FMT_EDATE:
    case FMT_SDATE:
    case FMT_ADATE:
    case FMT_JDATE:
    case FMT_QYR:
    case FMT_MOYR:
    case FMT_WKYR:
    case FMT_DATETIME:
    case FMT_TIME:
    case FMT_DTIME:
    case FMT_WKDAY:
    case FMT_MONTH:
      if ( column == PSPPIRE_VAR_STORE_COL_DECIMALS || column == PSPPIRE_VAR_STORE_COL_WIDTH)
	return FALSE;
      break;
    default:
      break;
    }

  return TRUE;
}


struct variable *
psppire_var_store_get_var (PsppireVarStore *store, glong row)
{
  return psppire_dict_get_variable (store->dictionary, row);
}

static gboolean
psppire_var_store_is_editable (const PsppireSheetModel *model, glong row, glong column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE (model);
  return psppire_var_store_item_editable (store, row, column);
}


static GdkColor *
psppire_var_store_get_foreground (const PsppireSheetModel *model, glong row, glong column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE (model);

  if ( ! psppire_var_store_item_editable (store, row, column) )
    return &store->disabled;

  return NULL;
}


static gchar *get_column_title (const PsppireSheetModel *model, gint col);
static gchar *get_row_title (const PsppireSheetModel *model, gint row);
static gboolean get_row_sensitivity (const PsppireSheetModel *model, gint row);

static void
psppire_var_store_sheet_model_init (PsppireSheetModelIface *iface)
{
  iface->get_row_count = psppire_var_store_get_row_count;
  iface->get_column_count = psppire_var_store_get_column_count;
  iface->free_strings = TRUE;
  iface->get_string = psppire_var_store_get_string;
  iface->set_string = psppire_var_store_set_string;
  iface->clear_datum = psppire_var_store_clear;
  iface->is_editable = psppire_var_store_is_editable;
  iface->get_foreground = psppire_var_store_get_foreground;
  iface->get_background = NULL;
  iface->get_justification = NULL;

  iface->get_column_title = get_column_title;

  iface->get_row_title = get_row_title;
  iface->get_row_sensitivity = get_row_sensitivity;

  iface->get_row_overstrike = NULL;
}

/**
 * psppire_var_store_new:
 * @dict: The dictionary for this var_store.
 *
 *
 * Return value: a new #PsppireVarStore
 **/
PsppireVarStore *
psppire_var_store_new (PsppireDict *dict)
{
  PsppireVarStore *retval;

  retval = g_object_new (GTK_TYPE_VAR_STORE, "dictionary", dict, NULL);

  //  psppire_var_store_set_dictionary (retval, dict);

  return retval;
}

#if 0
/**
 * psppire_var_store_replace_set_dictionary:
 * @var_store: The variable store
 * @dict: The dictionary to set
 *
 * If a dictionary is already associated with the var-store, then it will be
 * destroyed.
 **/
void
psppire_var_store_set_dictionary (PsppireVarStore *var_store, PsppireDict *dict)
{
  if ( var_store->dict ) g_object_unref (var_store->dict);

  var_store->dict = dict;

  g_signal_connect (dict, "variable-changed", G_CALLBACK (var_change_callback),
		   var_store);

  g_signal_connect (dict, "variable-deleted", G_CALLBACK (var_delete_callback),
		   var_store);

  g_signal_connect (dict, "variable-inserted",
		    G_CALLBACK (var_insert_callback), var_store);

  g_signal_connect (dict, "backend-changed", G_CALLBACK (refresh),
		    var_store);

  /* The entire model has changed */
  psppire_sheet_model_range_changed (PSPPIRE_SHEET_MODEL (var_store), -1, -1, -1, -1);
}
#endif

static void
psppire_var_store_finalize (GObject *object)
{
  /* must chain up */
  (* parent_class->finalize) (object);
}

static void
psppire_var_store_dispose (GObject *object)
{
  PsppireVarStore *self = PSPPIRE_VAR_STORE (object);

  if (self->dictionary)
    g_object_unref (self->dictionary);

  /* must chain up */
  (* parent_class->finalize) (object);
}


static gchar *
psppire_var_store_get_string (const PsppireSheetModel *model,
			      glong row, glong column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE (model);

  struct variable *pv;

  if ( row >= psppire_dict_get_var_cnt (store->dictionary))
    return 0;

  pv = psppire_dict_get_variable (store->dictionary, row);

  return text_for_column (store, pv, column, 0);
}


/* Clears that part of the variable store, if possible, which corresponds
   to ROW, COL.
   Returns true if anything was updated, false otherwise.
*/
static gboolean
psppire_var_store_clear (PsppireSheetModel *model,  glong row, glong col)
{
  struct variable *pv ;

  PsppireVarStore *var_store = PSPPIRE_VAR_STORE (model);

  if ( row >= psppire_dict_get_var_cnt (var_store->dictionary))
      return FALSE;

  pv = psppire_var_store_get_var (var_store, row);

  if ( !pv )
    return FALSE;

  switch (col)
    {
    case PSPPIRE_VAR_STORE_COL_LABEL:
      var_clear_label (pv);
      return TRUE;
      break;
    }

  return FALSE;
}

/* Attempts to update that part of the variable store which corresponds
   to ROW, COL with  the value TEXT.
   Returns true if anything was updated, false otherwise.
*/
static gboolean
psppire_var_store_set_string (PsppireSheetModel *model,
			  const gchar *text, glong row, glong col)
{
  struct variable *pv ;

  PsppireVarStore *var_store = PSPPIRE_VAR_STORE (model);

  if ( row >= psppire_dict_get_var_cnt (var_store->dictionary))
      return FALSE;

  pv = psppire_var_store_get_var (var_store, row);

  if ( !pv )
    return FALSE;

  switch (col)
    {
    case PSPPIRE_VAR_STORE_COL_NAME:
      {
	gboolean ok;
	ok =  psppire_dict_rename_var (var_store->dictionary, pv, text);
	return ok;
      }
    case PSPPIRE_VAR_STORE_COL_COLUMNS:
      if ( ! text) return FALSE;
      var_set_display_width (pv, atoi (text));
      return TRUE;
      break;
    case PSPPIRE_VAR_STORE_COL_WIDTH:
      {
	const int width = atoi (text);
	if ( ! text)
	  return FALSE;

	if (width < 0)
	  return FALSE;

	if ( var_is_alpha (pv))
	  {
	    if ( width > MAX_STRING )
	      return FALSE;
	    var_set_width (pv, width);
	  }
	else
	  {
            bool for_input
              = var_store->format_type == PSPPIRE_VAR_STORE_INPUT_FORMATS;
	    struct fmt_spec fmt ;
	    fmt = *var_get_print_format (pv);
	    if ( width < fmt_min_width (fmt.type, for_input)
		 ||
		 width > fmt_max_width (fmt.type, for_input))
	      return FALSE;

	    fmt.w = width;
	    fmt.d = MIN (fmt_max_decimals (fmt.type, width, for_input), fmt.d);

	    var_set_both_formats (pv, &fmt);
	  }

	return TRUE;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_DECIMALS:
      {
        bool for_input
          = var_store->format_type == PSPPIRE_VAR_STORE_INPUT_FORMATS;
	int decimals;
	struct fmt_spec fmt;
	if ( ! text) return FALSE;
	decimals = atoi (text);
	fmt = *var_get_print_format (pv);
	if ( decimals >
	     fmt_max_decimals (fmt.type,
                               fmt.w,
                               for_input
                               ))
	  return FALSE;

	fmt.d = decimals;
	var_set_both_formats (pv, &fmt);
	return TRUE;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_LABEL:
      {
	var_set_label (pv, text, true);
	return TRUE;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_TYPE:
    case PSPPIRE_VAR_STORE_COL_VALUES:
    case PSPPIRE_VAR_STORE_COL_MISSING:
    case PSPPIRE_VAR_STORE_COL_ALIGN:
    case PSPPIRE_VAR_STORE_COL_MEASURE:
      /* These can be modified only by their respective dialog boxes */
      return FALSE;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
    }

  return TRUE;
}


static const gchar none[] = N_("None");

static  gchar *
text_for_column (PsppireVarStore *vs,
		 const struct variable *pv, gint c, GError **err)
{
  PsppireDict *dict = vs->dictionary;

  const struct fmt_spec *format = var_get_print_format (pv);

  switch (c)
    {
    case PSPPIRE_VAR_STORE_COL_NAME:
      return xstrdup (var_get_name (pv));
      break;
    case PSPPIRE_VAR_STORE_COL_TYPE:
      return xstrdup (fmt_gui_name (format->type));
      break;
    case PSPPIRE_VAR_STORE_COL_WIDTH:
      {
	gchar *s;
	GString *gstr = g_string_sized_new (10);
	g_string_printf (gstr, _("%d"), format->w);
	s = g_locale_to_utf8 (gstr->str, gstr->len, 0, 0, err);
	g_string_free (gstr, TRUE);
	return s;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_DECIMALS:
      {
	gchar *s;
	GString *gstr = g_string_sized_new (10);
	g_string_printf (gstr, _("%d"), format->d);
	s = g_locale_to_utf8 (gstr->str, gstr->len, 0, 0, err);
	g_string_free (gstr, TRUE);
	return s;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_COLUMNS:
      {
	gchar *s;
	GString *gstr = g_string_sized_new (10);
	g_string_printf (gstr, _("%d"), var_get_display_width (pv));
	s = g_locale_to_utf8 (gstr->str, gstr->len, 0, 0, err);
	g_string_free (gstr, TRUE);
	return s;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_LABEL:
      {
	const char *label = var_get_label (pv);
	if (label)
	  return xstrdup (label);
	return NULL;
      }
      break;

    case PSPPIRE_VAR_STORE_COL_MISSING:
      {
	return missing_values_to_string (dict, pv, err);
      }
      break;
    case PSPPIRE_VAR_STORE_COL_VALUES:
      {
	if ( ! var_has_value_labels (pv))
	  return xstrdup (gettext (none));
	else
	  {
	    const struct val_labs *vls = var_get_value_labels (pv);
            const struct val_lab **labels = val_labs_sorted (vls);
	    const struct val_lab *vl = labels[0];
            free (labels);

	    g_assert (vl);

	    {
	      gchar *const vstr = value_to_text (vl->value, pv);

	      return g_strdup_printf (_("{%s,`%s'}_"), vstr,
                                      val_lab_get_escaped_label (vl));
	    }
	  }
      }
      break;
    case PSPPIRE_VAR_STORE_COL_ALIGN:
      {
	const gint align = var_get_alignment (pv);

	g_assert (align < n_ALIGNMENTS);
	return xstrdup (alignment_to_string (align));
      }
      break;
    case PSPPIRE_VAR_STORE_COL_MEASURE:
      {
	return xstrdup (measure_to_string (var_get_measure (pv)));
      }
      break;
    }
  return 0;
}



/* Return the number of variables */
gint
psppire_var_store_get_var_cnt (PsppireVarStore  *store)
{
  return psppire_dict_get_var_cnt (store->dictionary);
}


static glong
psppire_var_store_get_row_count (const PsppireSheetModel * model)
{
  gint rows = 0;
  PsppireVarStore *vs = PSPPIRE_VAR_STORE (model);

  if (vs->dictionary)
    rows =  psppire_dict_get_var_cnt (vs->dictionary);

  return rows ;
}

static glong
psppire_var_store_get_column_count (const PsppireSheetModel * model)
{
  return PSPPIRE_VAR_STORE_n_COLS ;
}



/* Row related funcs */


static gboolean
get_row_sensitivity (const PsppireSheetModel *model, gint row)
{
  PsppireVarStore *vs = PSPPIRE_VAR_STORE (model);

  if ( ! vs->dictionary)
    return FALSE;

  return  row < psppire_dict_get_var_cnt (vs->dictionary);
}


static gchar *
get_row_title (const PsppireSheetModel *model, gint unit)
{
  return g_strdup_printf (_("%d"), unit + 1);
}




static const gchar *column_titles[] = {
  N_("Name"),
  N_("Type"),
  N_("Width"),
  N_("Decimals"),
  N_("Label"),
  N_("Values"),
  N_("Missing"),
  N_("Columns"),
  N_("Align"),
  N_("Measure"),
};


static gchar *
get_column_title (const PsppireSheetModel *model, gint col)
{
  if ( col >= 10)
    return NULL;
  return g_strdup (gettext (column_titles[col]));
}
