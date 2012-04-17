/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2008, 2009, 2010, 2012  Free Software Foundation

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

#include <data/datasheet.h>
#include <data/data-out.h>
#include <data/variable.h>

#include <ui/gui/sheet/psppire-sheetmodel.h>
#include <ui/gui/psppire-marshal.h>

#include <pango/pango-context.h>

#include "psppire-data-store.h"
#include <libpspp/i18n.h>
#include "helper.h"

#include <data/dictionary.h>
#include <data/missing-values.h>
#include <data/value-labels.h>
#include <data/data-in.h>
#include <data/format.h>

#include <math/sort.h>

#include "xalloc.h"
#include "xmalloca.h"



static void psppire_data_store_init            (PsppireDataStore      *data_store);
static void psppire_data_store_class_init      (PsppireDataStoreClass *class);
static void psppire_data_store_sheet_model_init (PsppireSheetModelIface *iface);

static void psppire_data_store_finalize        (GObject           *object);
static void psppire_data_store_dispose        (GObject           *object);

static gboolean psppire_data_store_clear_datum (PsppireSheetModel *model,
					  glong row, glong column);


static gboolean psppire_data_store_insert_case (PsppireDataStore *ds,
						struct ccase *cc,
						casenumber posn);


static gboolean psppire_data_store_data_in (PsppireDataStore *ds,
					    casenumber casenum, gint idx,
					    struct substring input,
					    const struct fmt_spec *fmt);



static GObjectClass *parent_class = NULL;


enum
  {
    BACKEND_CHANGED,
    CASES_DELETED,
    CASE_INSERTED,
    CASE_CHANGED,
    n_SIGNALS
  };

static guint signals [n_SIGNALS];


GType
psppire_data_store_get_type (void)
{
  static GType data_store_type = 0;

  if (!data_store_type)
    {
      static const GTypeInfo data_store_info =
      {
	sizeof (PsppireDataStoreClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) psppire_data_store_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (PsppireDataStore),
	0,
        (GInstanceInitFunc) psppire_data_store_init,
      };

      static const GInterfaceInfo sheet_model_info =
      {
	(GInterfaceInitFunc) psppire_data_store_sheet_model_init,
	NULL,
	NULL
      };


      data_store_type = g_type_register_static (G_TYPE_OBJECT,
						"PsppireDataStore",
						&data_store_info, 0);

      g_type_add_interface_static (data_store_type,
				   PSPPIRE_TYPE_SHEET_MODEL,
				   &sheet_model_info);

    }

  return data_store_type;
}


static void
psppire_data_store_class_init (PsppireDataStoreClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_data_store_finalize;
  object_class->dispose = psppire_data_store_dispose;

  signals [BACKEND_CHANGED] =
    g_signal_new ("backend-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);

  signals [CASE_INSERTED] =
    g_signal_new ("case-inserted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  signals [CASE_CHANGED] =
    g_signal_new ("case-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);

  signals [CASES_DELETED] =
    g_signal_new ("cases-deleted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  psppire_marshal_VOID__INT_INT,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_INT);
}



static gboolean
psppire_data_store_insert_value (PsppireDataStore *ds,
				  gint width, gint where);

static bool
psppire_data_store_get_value (const PsppireDataStore *ds,
			      casenumber casenum, size_t idx,
			      union value *value);


static gboolean
psppire_data_store_set_value (PsppireDataStore *ds, casenumber casenum,
			      gint idx, union value *v);




static glong
psppire_data_store_get_var_count (const PsppireSheetModel *model)
{
  const PsppireDataStore *store = PSPPIRE_DATA_STORE (model);

  return psppire_dict_get_var_cnt (store->dict);
}

casenumber
psppire_data_store_get_case_count (const PsppireDataStore *store)
{
  return datasheet_get_n_rows (store->datasheet);
}

size_t
psppire_data_store_get_value_count (const PsppireDataStore *store)
{
  return psppire_dict_get_value_cnt (store->dict);
}

const struct caseproto *
psppire_data_store_get_proto (const PsppireDataStore *store)
{
  return psppire_dict_get_proto (store->dict);
}

static casenumber
psppire_data_store_get_case_count_wrapper (const PsppireSheetModel *model)
{
  const PsppireDataStore *store = PSPPIRE_DATA_STORE (model);
  return psppire_data_store_get_case_count (store);
}

static void
psppire_data_store_init (PsppireDataStore *data_store)
{
  data_store->dict = NULL;
  data_store->datasheet = NULL;
  data_store->dispose_has_run = FALSE;
}

static inline gchar *
psppire_data_store_get_string_wrapper (const PsppireSheetModel *model, glong row,
				       glong column)
{
  return psppire_data_store_get_string (PSPPIRE_DATA_STORE (model), row, column);
}


static inline gboolean
psppire_data_store_set_string_wrapper (PsppireSheetModel *model,
				       const gchar *text,
				       glong row, glong column)
{
  return psppire_data_store_set_string (PSPPIRE_DATA_STORE (model), text,
					row, column);
}



static gchar * get_column_subtitle (const PsppireSheetModel *model, gint col);
static gchar * get_column_button_label (const PsppireSheetModel *model, gint col);
static gboolean get_column_sensitivity (const PsppireSheetModel *model, gint col);
static GtkJustification get_column_justification (const PsppireSheetModel *model, gint col);

static gchar * get_row_button_label (const PsppireSheetModel *model, gint row);
static gboolean get_row_sensitivity (const PsppireSheetModel *model, gint row);
static gboolean get_row_overstrike (const PsppireSheetModel *model, gint row);


static void
psppire_data_store_sheet_model_init (PsppireSheetModelIface *iface)
{
  iface->free_strings = TRUE;
  iface->get_string = psppire_data_store_get_string_wrapper;
  iface->set_string = psppire_data_store_set_string_wrapper;
  iface->clear_datum = psppire_data_store_clear_datum;
  iface->is_editable = NULL;
  iface->get_foreground = NULL;
  iface->get_background = NULL;
  iface->get_column_count = psppire_data_store_get_var_count;
  iface->get_row_count = psppire_data_store_get_case_count_wrapper;

  iface->get_column_subtitle = get_column_subtitle;
  iface->get_column_title = get_column_button_label;
  iface->get_column_sensitivity = get_column_sensitivity;
  iface->get_column_justification = get_column_justification;

  iface->get_row_title = get_row_button_label;
  iface->get_row_sensitivity = get_row_sensitivity;
  iface->get_row_overstrike = get_row_overstrike;
}


/*
   A callback which occurs after a variable has been deleted.
 */
static void
delete_variable_callback (GObject *obj, const struct variable *var UNUSED,
                          gint dict_index, gint case_index,
			  gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);


  psppire_sheet_model_columns_deleted (PSPPIRE_SHEET_MODEL (store), dict_index, 1);
  datasheet_delete_columns (store->datasheet, case_index, 1);
  datasheet_insert_column (store->datasheet, NULL, -1, case_index);
#if AXIS_TRANSITION


  psppire_sheet_column_columns_changed (PSPPIRE_SHEET_COLUMN (store),
				   dict_index, -1);
#endif
}

static void
variable_changed_callback (GObject *obj, gint var_num, gpointer data)
{
#if AXIS_TRANSITION
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);

  psppire_sheet_column_columns_changed (PSPPIRE_SHEET_COLUMN (store),
				  var_num, 1);

  psppire_sheet_model_range_changed (PSPPIRE_SHEET_MODEL (store),
			       -1, var_num,
			       -1, var_num);
#endif
}

static void
insert_variable_callback (GObject *obj, gint var_num, gpointer data)
{
  struct variable *variable;
  PsppireDataStore *store;
  gint posn;

  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE (data);

  variable = psppire_dict_get_variable (store->dict, var_num);
  posn = var_get_case_index (variable);
  psppire_data_store_insert_value (store, var_get_width (variable), posn);

#if AXIS_TRANSITION

  psppire_sheet_column_columns_changed (PSPPIRE_SHEET_COLUMN (store),
				  var_num, 1);
#endif

  psppire_sheet_model_columns_inserted (PSPPIRE_SHEET_MODEL (store), var_num, 1);
}

struct resize_datum_aux
  {
    int old_width;
    int new_width;
  };


void
resize_datum (const union value *old, union value *new, void *aux_)
{
  struct resize_datum_aux *aux = aux_;

  if (aux->new_width == 0)
    {
      /* FIXME: try to parse string as number. */
      new->f = SYSMIS;
    }
  else if (aux->old_width == 0)
    {
      /* FIXME: format number as string. */
      value_set_missing (new, aux->new_width);
    }
  else
    value_copy_rpad (new, aux->new_width, old, aux->old_width, ' ');
}

static void
dict_size_change_callback (GObject *obj,
			  gint var_num, gint old_width, gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);
  struct variable *variable;
  int posn;

  variable = psppire_dict_get_variable (store->dict, var_num);
  posn = var_get_case_index (variable);

  if (old_width != var_get_width (variable))
    {
      struct resize_datum_aux aux;
      aux.old_width = old_width;
      aux.new_width = var_get_width (variable);
      datasheet_resize_column (store->datasheet, posn, aux.new_width,
                               resize_datum, &aux);
    }
}



/**
 * psppire_data_store_new:
 * @dict: The dictionary for this data_store.
 *
 *
 * Return value: a new #PsppireDataStore
 **/
PsppireDataStore *
psppire_data_store_new (PsppireDict *dict)
{
  PsppireDataStore *retval;

  retval = g_object_new (PSPPIRE_TYPE_DATA_STORE, NULL);

  psppire_data_store_set_dictionary (retval, dict);

  return retval;
}

void
psppire_data_store_set_reader (PsppireDataStore *ds,
			       struct casereader *reader)
{
  gint i;

  if ( ds->datasheet)
    datasheet_destroy (ds->datasheet);

  ds->datasheet = datasheet_create (reader);

  psppire_sheet_model_range_changed (PSPPIRE_SHEET_MODEL (ds),
			       -1, -1, -1, -1);

  if ( ds->dict )
    for (i = 0 ; i < n_dict_signals; ++i )
      {
	if ( ds->dict_handler_id [i] > 0)
	  {
	    g_signal_handler_unblock (ds->dict,
				      ds->dict_handler_id[i]);
	  }
      }

  g_signal_emit (ds, signals[BACKEND_CHANGED], 0);
}


/**
 * psppire_data_store_replace_set_dictionary:
 * @data_store: The variable store
 * @dict: The dictionary to set
 *
 * If a dictionary is already associated with the data-store, then it will be
 * destroyed.
 **/
void
psppire_data_store_set_dictionary (PsppireDataStore *data_store, PsppireDict *dict)
{
  int i;

  /* Disconnect any existing handlers */
  if ( data_store->dict )
    for (i = 0 ; i < n_dict_signals; ++i )
      {
	g_signal_handler_disconnect (data_store->dict,
				     data_store->dict_handler_id[i]);
      }

  data_store->dict = dict;

  if ( dict != NULL)
    {

      data_store->dict_handler_id [VARIABLE_INSERTED] =
	g_signal_connect (dict, "variable-inserted",
			  G_CALLBACK (insert_variable_callback),
			  data_store);

      data_store->dict_handler_id [VARIABLE_DELETED] =
	g_signal_connect (dict, "variable-deleted",
			  G_CALLBACK (delete_variable_callback),
			  data_store);

      data_store->dict_handler_id [VARIABLE_CHANGED] =
	g_signal_connect (dict, "variable-changed",
			  G_CALLBACK (variable_changed_callback),
			  data_store);

      data_store->dict_handler_id [SIZE_CHANGED] =
	g_signal_connect (dict, "dict-size-changed",
			  G_CALLBACK (dict_size_change_callback),
			  data_store);
    }



  /* The entire model has changed */
  psppire_sheet_model_range_changed (PSPPIRE_SHEET_MODEL (data_store), -1, -1, -1, -1);

#if AXIS_TRANSITION
  psppire_sheet_column_columns_changed (PSPPIRE_SHEET_COLUMN (data_store), 0, -1);
#endif

  if ( data_store->dict )
    for (i = 0 ; i < n_dict_signals; ++i )
      {
	if ( data_store->dict_handler_id [i] > 0)
	  {
	    g_signal_handler_block (data_store->dict,
				    data_store->dict_handler_id[i]);
	  }
      }
}

static void
psppire_data_store_finalize (GObject *object)
{

  /* must chain up */
  (* parent_class->finalize) (object);
}


static void
psppire_data_store_dispose (GObject *object)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (object);

  if (ds->dispose_has_run)
    return;

  if (ds->datasheet)
    {
      datasheet_destroy (ds->datasheet);
      ds->datasheet = NULL;
    }

  /* must chain up */
  (* parent_class->dispose) (object);

  ds->dispose_has_run = TRUE;
}



/* Insert a blank case before POSN */
gboolean
psppire_data_store_insert_new_case (PsppireDataStore *ds, casenumber posn)
{
  gboolean result;
  const struct caseproto *proto;
  struct ccase *cc;
  g_return_val_if_fail (ds, FALSE);

  proto = datasheet_get_proto (ds->datasheet);
  g_return_val_if_fail (caseproto_get_n_widths (proto) > 0, FALSE);
  g_return_val_if_fail (posn <= psppire_data_store_get_case_count (ds), FALSE);

  cc = case_create (proto);
  case_set_missing (cc);

  result = psppire_data_store_insert_case (ds, cc, posn);

  case_unref (cc);

  return result;
}


gchar *
psppire_data_store_get_string (PsppireDataStore *store, glong row, glong column)
{
  gint idx;
  char *text;
  const struct fmt_spec *fp ;
  const struct variable *pv ;
  const struct dictionary *dict;
  union value v;
  int width;

  g_return_val_if_fail (store->dict, NULL);
  g_return_val_if_fail (store->datasheet, NULL);

  dict = store->dict->dict;

  if (column >= psppire_dict_get_var_cnt (store->dict))
    return NULL;

  if ( row >= psppire_data_store_get_case_count (store))
    return NULL;

  pv = psppire_dict_get_variable (store->dict, column);

  g_assert (pv);

  idx = var_get_case_index (pv);
  width = var_get_width (pv);

  g_assert (idx >= 0);

  value_init (&v, width);
  if (!psppire_data_store_get_value (store, row, idx, &v))
    return NULL;

  if ( store->show_labels)
    {
      const gchar *label = var_lookup_value_label (pv, &v);
      if (label)
        {
          value_destroy (&v, width);
	  return g_strdup (label);
        }
    }

  fp = var_get_print_format (pv);

  text = data_out (&v, dict_get_encoding (dict), fp);

  g_strchomp (text);

  value_destroy (&v, width);
  return text;
}


static gboolean
psppire_data_store_clear_datum (PsppireSheetModel *model,
					  glong row, glong col)
{
  PsppireDataStore *store = PSPPIRE_DATA_STORE (model);

  union value v;
  const struct variable *pv = psppire_dict_get_variable (store->dict, col);
  int width = var_get_width (pv);

  const gint index = var_get_case_index (pv) ;

  value_init (&v, width);
  value_set_missing (&v, width);
  psppire_data_store_set_value (store, row, index, &v);
  value_destroy (&v, width);

  psppire_sheet_model_range_changed (model, row, col, row, col);


  return TRUE;
}


/* Attempts to update that part of the variable store which corresponds
   to ROW, COL with  the value TEXT.
   Returns true if anything was updated, false otherwise.
*/
gboolean
psppire_data_store_set_string (PsppireDataStore *store,
			       const gchar *text, glong row, glong col)
{
  glong n_cases;
  const struct variable *pv = psppire_dict_get_variable (store->dict, col);
  if ( NULL == pv)
    return FALSE;

  n_cases = psppire_data_store_get_case_count (store);

  if ( row > n_cases)
    return FALSE;

  if (row == n_cases)
    psppire_data_store_insert_new_case (store, row);

  psppire_data_store_data_in (store, row,
			      var_get_case_index (pv), ss_cstr (text),
			      var_get_print_format (pv));

  psppire_sheet_model_range_changed (PSPPIRE_SHEET_MODEL (store), row, col, row, col);

  return TRUE;
}



void
psppire_data_store_show_labels (PsppireDataStore *store, gboolean show_labels)
{
  g_return_if_fail (store);
  g_return_if_fail (PSPPIRE_IS_DATA_STORE (store));

  store->show_labels = show_labels;

  psppire_sheet_model_range_changed (PSPPIRE_SHEET_MODEL (store),
				 -1, -1, -1, -1);
}


void
psppire_data_store_clear (PsppireDataStore *ds)
{
  datasheet_destroy (ds->datasheet);
  ds->datasheet = NULL;

  psppire_dict_clear (ds->dict);

  g_signal_emit (ds, signals [CASES_DELETED], 0, 0, -1);
}



/* Return a casereader made from this datastore */
struct casereader *
psppire_data_store_get_reader (PsppireDataStore *ds)
{
  int i;
  struct casereader *reader ;

  if ( ds->dict )
    for (i = 0 ; i < n_dict_signals; ++i )
      {
	g_signal_handler_block (ds->dict,
				ds->dict_handler_id[i]);
      }

  reader = datasheet_make_reader (ds->datasheet);

  /* We must not reference this again */
  ds->datasheet = NULL;

  return reader;
}



/* Column related funcs */


static const gchar null_var_name[]=N_("var");



/* Row related funcs */

static gchar *
get_row_button_label (const PsppireSheetModel *model, gint unit)
{
  // PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  return g_strdup_printf (_("%d"), unit + FIRST_CASE_NUMBER);
}


static gboolean
get_row_sensitivity (const PsppireSheetModel *model, gint unit)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  return (unit < psppire_data_store_get_case_count (ds));
}




/* Column related stuff */

static gchar *
get_column_subtitle (const PsppireSheetModel *model, gint col)
{
  const struct variable *v ;
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  if ( col >= psppire_dict_get_var_cnt (ds->dict) )
    return NULL;

  v = psppire_dict_get_variable (ds->dict, col);

  if ( ! var_has_label (v))
    return NULL;

  return xstrdup (var_get_label (v));
}

static gchar *
get_column_button_label (const PsppireSheetModel *model, gint col)
{
  struct variable *pv ;
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  if ( col >= psppire_dict_get_var_cnt (ds->dict) )
    return xstrdup (gettext (null_var_name));

  pv = psppire_dict_get_variable (ds->dict, col);

  if (NULL == pv)
    return NULL;

  return xstrdup (var_get_name (pv));
}

static gboolean
get_column_sensitivity (const PsppireSheetModel *model, gint col)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  return (col < psppire_dict_get_var_cnt (ds->dict));
}



static GtkJustification
get_column_justification (const PsppireSheetModel *model, gint col)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);
  const struct variable *pv ;

  if ( col >= psppire_dict_get_var_cnt (ds->dict) )
    return GTK_JUSTIFY_LEFT;

  pv = psppire_dict_get_variable (ds->dict, col);

  return (var_get_alignment (pv) == ALIGN_LEFT ? GTK_JUSTIFY_LEFT
          : var_get_alignment (pv) == ALIGN_RIGHT ? GTK_JUSTIFY_RIGHT
          : GTK_JUSTIFY_CENTER);
}






/* Returns the CASENUMth case, or a null pointer on failure.
 */
struct ccase *
psppire_data_store_get_case (const PsppireDataStore *ds,
			     casenumber casenum)
{
  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  return datasheet_get_row (ds->datasheet, casenum);
}


gboolean
psppire_data_store_delete_cases (PsppireDataStore *ds, casenumber first,
				 casenumber n_cases)
{
  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (first + n_cases <=
			psppire_data_store_get_case_count (ds), FALSE);


  datasheet_delete_rows (ds->datasheet, first, n_cases);

  g_signal_emit (ds, signals [CASES_DELETED], 0, first, n_cases);
  psppire_sheet_model_rows_deleted (PSPPIRE_SHEET_MODEL (ds), first, n_cases);

  return TRUE;
}



/* Insert case CC into the case file before POSN */
static gboolean
psppire_data_store_insert_case (PsppireDataStore *ds,
				struct ccase *cc,
				casenumber posn)
{
  bool result ;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  cc = case_ref (cc);
  result = datasheet_insert_rows (ds->datasheet, posn, &cc, 1);

  if ( result )
    {
      g_signal_emit (ds, signals [CASE_INSERTED], 0, posn);
      psppire_sheet_model_rows_inserted (PSPPIRE_SHEET_MODEL (ds), posn, 1);
    }
  else
    g_warning ("Cannot insert case at position %ld\n", posn);

  return result;
}


/* Copies the IDXth value from case CASENUM into VALUE, which
   must be of the correct width for IDX.
   Returns true if successful, false on failure. */
static bool
psppire_data_store_get_value (const PsppireDataStore *ds,
			      casenumber casenum, size_t idx,
			      union value *value)
{
  g_return_val_if_fail (ds, false);
  g_return_val_if_fail (ds->datasheet, false);
  g_return_val_if_fail (idx < datasheet_get_n_columns (ds->datasheet), false);

  return datasheet_get_value (ds->datasheet, casenum, idx, value);
}



/* Set the IDXth value of case C to V.
   V must be the correct width for IDX.
   Returns true if successful, false on I/O error. */
static gboolean
psppire_data_store_set_value (PsppireDataStore *ds, casenumber casenum,
			      gint idx, union value *v)
{
  bool ok;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_n_columns (ds->datasheet), FALSE);

  ok = datasheet_put_value (ds->datasheet, casenum, idx, v);
  if (ok)
    g_signal_emit (ds, signals [CASE_CHANGED], 0, casenum);

  return ok;
}




/* Set the IDXth value of case C using D_IN */
static gboolean
psppire_data_store_data_in (PsppireDataStore *ds, casenumber casenum, gint idx,
			    struct substring input, const struct fmt_spec *fmt)
{
  union value value;
  int width;
  bool ok;

  PsppireDict *dict;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_n_columns (ds->datasheet), FALSE);

  dict = ds->dict;

  width = fmt_var_width (fmt);
  g_return_val_if_fail (caseproto_get_width (
                          datasheet_get_proto (ds->datasheet), idx) == width,
                        FALSE);
  value_init (&value, width);
  ok = (datasheet_get_value (ds->datasheet, casenum, idx, &value)
        && data_in_msg (input, UTF8, fmt->type, &value, width,
                        dict_get_encoding (dict->dict))
        && datasheet_put_value (ds->datasheet, casenum, idx, &value));
  value_destroy (&value, width);

  if (ok)
    g_signal_emit (ds, signals [CASE_CHANGED], 0, casenum);

  return ok;
}

/* Resize the cases in the casefile, by inserting a value of the
   given WIDTH into every one of them at the position immediately
   preceding WHERE.
*/
static gboolean
psppire_data_store_insert_value (PsppireDataStore *ds,
                                 gint width, gint where)
{
  union value value;

  g_return_val_if_fail (ds, FALSE);

  g_assert (width >= 0);

  if ( ! ds->datasheet )
    ds->datasheet = datasheet_create (NULL);

  value_init (&value, width);
  if (width == 0)
    value.f = 0;
  else
    value_set_missing (&value, width);

  datasheet_insert_column (ds->datasheet, &value, width, where);

  return TRUE;
}

static gboolean
get_row_overstrike (const PsppireSheetModel *model, gint row)
{
  union value val;
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  const struct dictionary *dict = ds->dict->dict;

  const struct variable *filter = dict_get_filter (dict);

  if ( row < 0 || row >= datasheet_get_n_rows (ds->datasheet))
    return FALSE;

  if ( ! filter)
    return FALSE;

  g_assert (var_is_numeric (filter));

  value_init (&val, 0);
  if ( ! datasheet_get_value (ds->datasheet, row,
			      var_get_case_index (filter),
			      &val) )
    return FALSE;


  return (val.f == 0.0);
}
