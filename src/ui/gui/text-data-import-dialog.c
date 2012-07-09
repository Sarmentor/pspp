/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012  Free Software Foundation

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

#include "ui/gui/text-data-import-dialog.h"

#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "data/data-in.h"
#include "data/data-out.h"
#include "data/format-guesser.h"
#include "data/value-labels.h"
#include "language/data-io/data-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/checkbox-treeview.h"
#include "ui/gui/dialog-common.h"
#include "ui/gui/executor.h"
#include "ui/gui/helper.h"
#include "ui/gui/pspp-sheet-selection.h"
#include "ui/gui/pspp-sheet-view.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-empty-list-store.h"
#include "ui/gui/psppire-scanf.h"
#include "ui/gui/psppire-var-sheet.h"
#include "ui/syntax-gen.h"

#include "gl/error.h"
#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct import_assistant;

/* The file to be imported. */
struct file
  {
    char *file_name;        /* File name. */
    gchar *encoding;        /* Encoding. */
    unsigned long int total_lines; /* Number of lines in file. */
    bool total_is_exact;    /* Is total_lines exact (or an estimate)? */

    /* The first several lines of the file. */
    struct string *lines;
    size_t line_cnt;
  };
static bool init_file (struct import_assistant *, GtkWindow *parent);
static void destroy_file (struct import_assistant *);

/* The main body of the GTK+ assistant and related data. */
struct assistant
  {
    GtkBuilder *builder;
    GtkAssistant *assistant;
    GMainLoop *main_loop;
    GtkWidget *paste_button;
    GtkWidget *reset_button;
    int response;
    int watch_cursor;

    GtkCellRenderer *prop_renderer;
    GtkCellRenderer *fixed_renderer;
  };
static void init_assistant (struct import_assistant *, GtkWindow *);
static void destroy_assistant (struct import_assistant *);
static GtkWidget *add_page_to_assistant (struct import_assistant *,
                                         GtkWidget *page,
                                         GtkAssistantPageType);

/* The introduction page of the assistant. */
struct intro_page
  {
    GtkWidget *page;
    GtkWidget *all_cases_button;
    GtkWidget *n_cases_button;
    GtkWidget *n_cases_spin;
    GtkWidget *percent_button;
    GtkWidget *percent_spin;
  };
static void init_intro_page (struct import_assistant *);
static void reset_intro_page (struct import_assistant *);

/* Page where the user chooses the first line of data. */
struct first_line_page
  {
    int skip_lines;    /* Number of initial lines to skip? */
    bool variable_names; /* Variable names above first line of data? */

    GtkWidget *page;
    PsppSheetView *tree_view;
    GtkWidget *variable_names_cb;
  };
static void init_first_line_page (struct import_assistant *);
static void reset_first_line_page (struct import_assistant *);

/* Page where the user chooses field separators. */
struct separators_page
  {
    /* How to break lines into columns. */
    struct string separators;   /* Field separators. */
    struct string quotes;       /* Quote characters. */
    bool escape;                /* Doubled quotes yield a quote mark? */

    /* The columns produced thereby. */
    struct column *columns;     /* Information about each column. */
    size_t column_cnt;          /* Number of columns. */

    GtkWidget *page;
    GtkWidget *custom_cb;
    GtkWidget *custom_entry;
    GtkWidget *quote_cb;
    GtkWidget *quote_combo;
    GtkEntry *quote_entry;
    GtkWidget *escape_cb;
    PsppSheetView *fields_tree_view;
  };
/* The columns that the separators divide the data into. */
struct column
  {
    /* Variable name for this column.  This is the variable name
       used on the separators page; it can be overridden by the
       user on the formats page. */
    char *name;

    /* Maximum length of any row in this column. */
    size_t width;

    /* Contents of this column: contents[row] is the contents for
       the given row.

       A null substring indicates a missing column for that row
       (because the line contains an insufficient number of
       separators).

       contents[] elements may be substrings of the lines[]
       strings that represent the whole lines of the file, to
       save memory.  Other elements are dynamically allocated
       with ss_alloc_substring. */
    struct substring *contents;
  };
static void init_separators_page (struct import_assistant *);
static void destroy_separators_page (struct import_assistant *);
static void prepare_separators_page (struct import_assistant *);
static void reset_separators_page (struct import_assistant *);

/* Page where the user verifies and adjusts input formats. */
struct formats_page
  {
    struct dictionary *dict;

    GtkWidget *page;
    PsppSheetView *data_tree_view;
    PsppireDict *psppire_dict;
    struct variable **modified_vars;
    size_t modified_var_cnt;
  };
static void init_formats_page (struct import_assistant *);
static void destroy_formats_page (struct import_assistant *);
static void prepare_formats_page (struct import_assistant *);
static void reset_formats_page (struct import_assistant *);

struct import_assistant
  {
    struct file file;
    struct assistant asst;
    struct intro_page intro;
    struct first_line_page first_line;
    struct separators_page separators;
    struct formats_page formats;
  };

static void apply_dict (const struct dictionary *, struct string *);
static char *generate_syntax (const struct import_assistant *);

static gboolean get_tooltip_location (GtkWidget *widget, gint wx, gint wy,
                                      const struct import_assistant *,
                                      size_t *row, size_t *column);
static void make_tree_view (const struct import_assistant *ia,
                            size_t first_line,
                            PsppSheetView **tree_view);
static void add_line_number_column (const struct import_assistant *,
                                    PsppSheetView *);
static gint get_monospace_width (PsppSheetView *, GtkCellRenderer *,
                                 size_t char_cnt);
static gint get_string_width (PsppSheetView *, GtkCellRenderer *,
                              const char *string);
static PsppSheetViewColumn *make_data_column (struct import_assistant *,
                                            PsppSheetView *, bool input,
                                            gint column_idx);
static PsppSheetView *create_data_tree_view (bool input, GtkContainer *parent,
                                           struct import_assistant *);
static void push_watch_cursor (struct import_assistant *);
static void pop_watch_cursor (struct import_assistant *);

/* Pops up the Text Data Import assistant. */
void
text_data_import_assistant (PsppireDataWindow *dw)
{
  GtkWindow *parent_window = GTK_WINDOW (dw);
  struct import_assistant *ia;

  ia = xzalloc (sizeof *ia);
  if (!init_file (ia, parent_window))
    {
      free (ia);
      return;
    }

  init_assistant (ia, parent_window);
  init_intro_page (ia);
  init_first_line_page (ia);
  init_separators_page (ia);
  init_formats_page (ia);

  gtk_widget_show_all (GTK_WIDGET (ia->asst.assistant));

  ia->asst.main_loop = g_main_loop_new (NULL, false);
  g_main_loop_run (ia->asst.main_loop);
  g_main_loop_unref (ia->asst.main_loop);

  switch (ia->asst.response)
    {
    case GTK_RESPONSE_APPLY:
      free (execute_syntax_string (dw, generate_syntax (ia)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      free (paste_syntax_to_window (generate_syntax (ia)));
      break;
    default:
      break;
    }

  destroy_formats_page (ia);
  destroy_separators_page (ia);
  destroy_assistant (ia);
  destroy_file (ia);
  free (ia);
}

/* Emits PSPP syntax to S that applies the dictionary attributes
   (such as missing values and value labels) of the variables in
   DICT.  */
static void
apply_dict (const struct dictionary *dict, struct string *s)
{
  size_t var_cnt = dict_get_var_cnt (dict);
  size_t i;

  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const char *name = var_get_name (var);
      enum val_type type = var_get_type (var);
      int width = var_get_width (var);
      enum measure measure = var_get_measure (var);
      enum alignment alignment = var_get_alignment (var);
      const struct fmt_spec *format = var_get_print_format (var);

      if (var_has_missing_values (var))
        {
          const struct missing_values *mv = var_get_missing_values (var);
          size_t j;

          syntax_gen_pspp (s, "MISSING VALUES %ss (", name);
          for (j = 0; j < mv_n_values (mv); j++)
            {
              if (j)
                ds_put_cstr (s, ", ");
              syntax_gen_value (s, mv_get_value (mv, j), width, format);
            }

          if (mv_has_range (mv))
            {
              double low, high;
              if (mv_has_value (mv))
                ds_put_cstr (s, ", ");
              mv_get_range (mv, &low, &high);
              syntax_gen_num_range (s, low, high, format);
            }
          ds_put_cstr (s, ").\n");
        }
      if (var_has_value_labels (var))
        {
          const struct val_labs *vls = var_get_value_labels (var);
          const struct val_lab **labels = val_labs_sorted (vls);
          size_t n_labels = val_labs_count (vls);
          size_t i;

          syntax_gen_pspp (s, "VALUE LABELS %ss", name);
          for (i = 0; i < n_labels; i++)
            {
              const struct val_lab *vl = labels[i];
              ds_put_cstr (s, "\n  ");
              syntax_gen_value (s, &vl->value, width, format);
              ds_put_byte (s, ' ');
              syntax_gen_string (s, ss_cstr (val_lab_get_escaped_label (vl)));
            }
          free (labels);
          ds_put_cstr (s, ".\n");
        }
      if (var_has_label (var))
        syntax_gen_pspp (s, "VARIABLE LABELS %ss %sq.\n",
                         name, var_get_label (var));
      if (measure != var_default_measure (type))
        syntax_gen_pspp (s, "VARIABLE LEVEL %ss (%ss).\n",
                         name,
                         (measure == MEASURE_NOMINAL ? "NOMINAL"
                          : measure == MEASURE_ORDINAL ? "ORDINAL"
                          : "SCALE"));
      if (alignment != var_default_alignment (type))
        syntax_gen_pspp (s, "VARIABLE ALIGNMENT %ss (%ss).\n",
                         name,
                         (alignment == ALIGN_LEFT ? "LEFT"
                          : alignment == ALIGN_CENTRE ? "CENTER"
                          : "RIGHT"));
      if (var_get_display_width (var) != var_default_display_width (width))
        syntax_gen_pspp (s, "VARIABLE WIDTH %ss (%d).\n",
                         name, var_get_display_width (var));
    }
}

/* Generates and returns PSPP syntax to execute the import
   operation described by IA.  The caller must free the syntax
   with free(). */
static char *
generate_syntax (const struct import_assistant *ia)
{
  struct string s = DS_EMPTY_INITIALIZER;
  size_t var_cnt;
  size_t i;

  syntax_gen_pspp (&s,
                   "GET DATA\n"
                   "  /TYPE=TXT\n"
                   "  /FILE=%sq\n",
                   ia->file.file_name);
  if (ia->file.encoding && strcmp (ia->file.encoding, "Auto"))
    syntax_gen_pspp (&s, "  /ENCODING=%sq\n", ia->file.encoding);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
                                      ia->intro.n_cases_button)))
    ds_put_format (&s, "  /IMPORTCASES=FIRST %d\n",
                   gtk_spin_button_get_value_as_int (
                     GTK_SPIN_BUTTON (ia->intro.n_cases_spin)));
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
                                           ia->intro.percent_button)))
    ds_put_format (&s, "  /IMPORTCASES=PERCENT %d\n",
                   gtk_spin_button_get_value_as_int (
                     GTK_SPIN_BUTTON (ia->intro.percent_spin)));
  else
    ds_put_cstr (&s, "  /IMPORTCASES=ALL\n");
  ds_put_cstr (&s,
               "  /ARRANGEMENT=DELIMITED\n"
               "  /DELCASE=LINE\n");
  if (ia->first_line.skip_lines > 0)
    ds_put_format (&s, "  /FIRSTCASE=%d\n", ia->first_line.skip_lines + 1);
  ds_put_cstr (&s, "  /DELIMITERS=\"");
  if (ds_find_byte (&ia->separators.separators, '\t') != SIZE_MAX)
    ds_put_cstr (&s, "\\t");
  if (ds_find_byte (&ia->separators.separators, '\\') != SIZE_MAX)
    ds_put_cstr (&s, "\\\\");
  for (i = 0; i < ds_length (&ia->separators.separators); i++)
    {
      char c = ds_at (&ia->separators.separators, i);
      if (c == '"')
        ds_put_cstr (&s, "\"\"");
      else if (c != '\t' && c != '\\')
        ds_put_byte (&s, c);
    }
  ds_put_cstr (&s, "\"\n");
  if (!ds_is_empty (&ia->separators.quotes))
    syntax_gen_pspp (&s, "  /QUALIFIER=%sq\n", ds_cstr (&ia->separators.quotes));
  if (!ds_is_empty (&ia->separators.quotes) && ia->separators.escape)
    ds_put_cstr (&s, "  /ESCAPE\n");
  ds_put_cstr (&s, "  /VARIABLES=\n");

  var_cnt = dict_get_var_cnt (ia->formats.dict);
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (ia->formats.dict, i);
      char format_string[FMT_STRING_LEN_MAX + 1];
      fmt_to_string (var_get_print_format (var), format_string);
      ds_put_format (&s, "    %s %s%s\n",
                     var_get_name (var), format_string,
                     i == var_cnt - 1 ? "." : "");
    }

  apply_dict (ia->formats.dict, &s);

  return ds_cstr (&s);
}

/* Choosing a file and reading it. */

static char *choose_file (GtkWindow *parent_window, gchar **encodingp);

/* Obtains the file to import from the user and initializes IA's
   file substructure.  PARENT_WINDOW must be the window to use
   as the file chooser window's parent.

   Returns true if successful, false if the file name could not
   be obtained or the file could not be read. */
static bool
init_file (struct import_assistant *ia, GtkWindow *parent_window)
{
  struct file *file = &ia->file;
  enum { MAX_PREVIEW_LINES = 1000 }; /* Max number of lines to read. */
  enum { MAX_LINE_LEN = 16384 }; /* Max length of an acceptable line. */
  struct line_reader *reader;

  file->file_name = choose_file (parent_window, &file->encoding);
  if (file->file_name == NULL)
    return false;

  reader = line_reader_for_file (file->encoding, file->file_name, O_RDONLY);
  if (reader == NULL)
    {
      msg (ME, _("Could not open `%s': %s"),
           file->file_name, strerror (errno));
      return false;
    }

  file->lines = xnmalloc (MAX_PREVIEW_LINES, sizeof *file->lines);
  for (; file->line_cnt < MAX_PREVIEW_LINES; file->line_cnt++)
    {
      struct string *line = &file->lines[file->line_cnt];

      ds_init_empty (line);
      if (!line_reader_read (reader, line, MAX_LINE_LEN + 1)
          || ds_length (line) > MAX_LINE_LEN)
        {
          if (line_reader_eof (reader))
            break;
          else if (line_reader_error (reader))
            msg (ME, _("Error reading `%s': %s"),
                 file->file_name, strerror (line_reader_error (reader)));
          else
            msg (ME, _("Failed to read `%s', because it contains a line "
                       "over %d bytes long and therefore appears not to be "
                       "a text file."),
                 file->file_name, MAX_LINE_LEN);
          line_reader_close (reader);
          destroy_file (ia);
          return false;
        }
    }

  if (file->line_cnt == 0)
    {
      msg (ME, _("`%s' is empty."), file->file_name);
      line_reader_close (reader);
      destroy_file (ia);
      return false;
    }

  /* Estimate the number of lines in the file. */
  if (file->line_cnt < MAX_PREVIEW_LINES)
    file->total_lines = file->line_cnt;
  else
    {
      struct stat s;
      off_t position = line_reader_tell (reader);
      if (fstat (line_reader_fileno (reader), &s) == 0 && position > 0)
        file->total_lines = (double) file->line_cnt / position * s.st_size;
      else
        file->total_lines = 0;
    }

  line_reader_close (reader);

  return true;
}

/* Frees IA's file substructure. */
static void
destroy_file (struct import_assistant *ia)
{
  struct file *f = &ia->file;
  size_t i;

  for (i = 0; i < f->line_cnt; i++)
    ds_destroy (&f->lines[i]);
  free (f->lines);
  g_free (f->file_name);
  g_free (f->encoding);
}

/* Obtains the file to read from the user.  If successful, returns the name of
   the file and stores the user's chosen encoding for the file into *ENCODINGP.
   The caller must free each of these strings with g_free().

   On failure, stores a null pointer and stores NULL in *ENCODINGP.

   PARENT_WINDOW must be the window to use as the file chooser window's
   parent. */
static char *
choose_file (GtkWindow *parent_window, gchar **encodingp)
{
  char *file_name;
  GtkFileFilter *filter = NULL;

  GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Import Delimited Text Data"),
                                        parent_window,
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                        NULL);

  g_object_set (dialog, "local-only", FALSE, NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Text files"));
  gtk_file_filter_add_mime_type (filter, "text/*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Text (*.txt) Files"));
  gtk_file_filter_add_pattern (filter, "*.txt");
  gtk_file_filter_add_pattern (filter, "*.TXT");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Plain Text (ASCII) Files"));
  gtk_file_filter_add_mime_type (filter, "text/plain");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Comma Separated Value Files"));
  gtk_file_filter_add_mime_type (filter, "text/csv");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  /* I've never encountered one of these, but it's listed here:
     http://www.iana.org/assignments/media-types/text/tab-separated-values  */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Tab Separated Value Files"));
  gtk_file_filter_add_mime_type (filter, "text/tab-separated-values");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  gtk_file_chooser_set_extra_widget (
    GTK_FILE_CHOOSER (dialog), psppire_encoding_selector_new ("Auto", true));

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      *encodingp = psppire_encoding_selector_get_encoding (
        gtk_file_chooser_get_extra_widget (GTK_FILE_CHOOSER (dialog)));
      break;
    default:
      file_name = NULL;
      *encodingp = NULL;
      break;
    }
  gtk_widget_destroy (dialog);

  return file_name;
}

/* Assistant. */

static void close_assistant (struct import_assistant *, int response);
static void on_prepare (GtkAssistant *assistant, GtkWidget *page,
                        struct import_assistant *);
static void on_cancel (GtkAssistant *assistant, struct import_assistant *);
static void on_close (GtkAssistant *assistant, struct import_assistant *);
static void on_paste (GtkButton *button, struct import_assistant *);
static void on_reset (GtkButton *button, struct import_assistant *);
static void close_assistant (struct import_assistant *, int response);

/* Initializes IA's asst substructure.  PARENT_WINDOW must be the
   window to use as the assistant window's parent.  */
static void
init_assistant (struct import_assistant *ia, GtkWindow *parent_window)
{
  struct assistant *a = &ia->asst;

  a->builder = builder_new ("text-data-import.ui");
  a->assistant = GTK_ASSISTANT (gtk_assistant_new ());
  g_signal_connect (a->assistant, "prepare", G_CALLBACK (on_prepare), ia);
  g_signal_connect (a->assistant, "cancel", G_CALLBACK (on_cancel), ia);
  g_signal_connect (a->assistant, "close", G_CALLBACK (on_close), ia);
  a->paste_button = gtk_button_new_from_stock (GTK_STOCK_PASTE);
  gtk_assistant_add_action_widget (a->assistant, a->paste_button);
  g_signal_connect (a->paste_button, "clicked", G_CALLBACK (on_paste), ia);
  a->reset_button = gtk_button_new_from_stock ("pspp-stock-reset");
  gtk_assistant_add_action_widget (a->assistant, a->reset_button);
  g_signal_connect (a->reset_button, "clicked", G_CALLBACK (on_reset), ia);
  gtk_window_set_title (GTK_WINDOW (a->assistant),
                        _("Importing Delimited Text Data"));
  gtk_window_set_transient_for (GTK_WINDOW (a->assistant), parent_window);
  gtk_window_set_icon_name (GTK_WINDOW (a->assistant), "pspp");

  a->prop_renderer = gtk_cell_renderer_text_new ();
  g_object_ref_sink (a->prop_renderer);
  a->fixed_renderer = gtk_cell_renderer_text_new ();
  g_object_ref_sink (a->fixed_renderer);
  g_object_set (G_OBJECT (a->fixed_renderer),
                "family", "Monospace",
                (void *) NULL);
}

/* Frees IA's asst substructure. */
static void
destroy_assistant (struct import_assistant *ia)
{
  struct assistant *a = &ia->asst;

  g_object_unref (a->prop_renderer);
  g_object_unref (a->fixed_renderer);
  g_object_unref (a->builder);
}

/* Appends a page of the given TYPE, with PAGE as its content, to
   the GtkAssistant encapsulated by IA.  Returns the GtkWidget
   that represents the page. */
static GtkWidget *
add_page_to_assistant (struct import_assistant *ia,
                       GtkWidget *page, GtkAssistantPageType type)
{
  const char *title;
  char *title_copy;
  GtkWidget *content;

  title = gtk_window_get_title (GTK_WINDOW (page));
  title_copy = xstrdup (title ? title : "");

  content = gtk_bin_get_child (GTK_BIN (page));
  assert (content);
  g_object_ref (content);
  gtk_container_remove (GTK_CONTAINER (page), content);

  gtk_widget_destroy (page);

  gtk_assistant_append_page (ia->asst.assistant, content);
  gtk_assistant_set_page_type (ia->asst.assistant, content, type);
  gtk_assistant_set_page_title (ia->asst.assistant, content, title_copy);
  gtk_assistant_set_page_complete (ia->asst.assistant, content, true);

  free (title_copy);

  return content;
}

/* Called just before PAGE is displayed as the current page of
   ASSISTANT, this updates IA content according to the new
   page. */
static void
on_prepare (GtkAssistant *assistant, GtkWidget *page,
            struct import_assistant *ia)
{

  if (gtk_assistant_get_page_type (assistant, page)
      == GTK_ASSISTANT_PAGE_CONFIRM)
    gtk_widget_grab_focus (assistant->apply);
  else
    gtk_widget_grab_focus (assistant->forward);

  if (page == ia->separators.page)
    prepare_separators_page (ia);
  else if (page == ia->formats.page)
    prepare_formats_page (ia);

  gtk_widget_show (ia->asst.reset_button);
  if (page == ia->formats.page)
    gtk_widget_show (ia->asst.paste_button);
  else
    gtk_widget_hide (ia->asst.paste_button);
}

/* Called when the Cancel button in the assistant is clicked. */
static void
on_cancel (GtkAssistant *assistant, struct import_assistant *ia)
{
  close_assistant (ia, GTK_RESPONSE_CANCEL);
}

/* Called when the Apply button on the last page of the assistant
   is clicked. */
static void
on_close (GtkAssistant *assistant, struct import_assistant *ia)
{
  close_assistant (ia, GTK_RESPONSE_APPLY);
}

/* Called when the Paste button on the last page of the assistant
   is clicked. */
static void
on_paste (GtkButton *button, struct import_assistant *ia)
{
  close_assistant (ia, PSPPIRE_RESPONSE_PASTE);
}

/* Called when the Reset button is clicked. */
static void
on_reset (GtkButton *button, struct import_assistant *ia)
{
  gint page_num = gtk_assistant_get_current_page (ia->asst.assistant);
  GtkWidget *page = gtk_assistant_get_nth_page (ia->asst.assistant, page_num);

  if (page == ia->intro.page)
    reset_intro_page (ia);
  else if (page == ia->first_line.page)
    reset_first_line_page (ia);
  else if (page == ia->separators.page)
    reset_separators_page (ia);
  else if (page == ia->formats.page)
    reset_formats_page (ia);
}

/* Causes the assistant to close, returning RESPONSE for
   interpretation by text_data_import_assistant. */
static void
close_assistant (struct import_assistant *ia, int response)
{
  ia->asst.response = response;
  g_main_loop_quit (ia->asst.main_loop);
  gtk_widget_hide (GTK_WIDGET (ia->asst.assistant));
}

/* The "intro" page of the assistant. */

static void on_intro_amount_changed (struct import_assistant *);

/* Initializes IA's intro substructure. */
static void
init_intro_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct intro_page *p = &ia->intro;
  struct string s;
  GtkWidget *hbox_n_cases ;
  GtkWidget *hbox_percent ;
  GtkWidget *table ;


  p->n_cases_spin = gtk_spin_button_new_with_range (0, INT_MAX, 100);

  hbox_n_cases = psppire_scanf_new (_("Only the first %4d cases"), &p->n_cases_spin);

  table  = get_widget_assert (builder, "button-table");

  gtk_table_attach_defaults (GTK_TABLE (table), hbox_n_cases,
		    1, 2,
		    1, 2);

  p->percent_spin = gtk_spin_button_new_with_range (0, 100, 10);

  hbox_percent = psppire_scanf_new (_("Only the first %3d %% of file (approximately)"), &p->percent_spin);

  gtk_table_attach_defaults (GTK_TABLE (table), hbox_percent,
			     1, 2,
			     2, 3);

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Intro"),
                                   GTK_ASSISTANT_PAGE_INTRO);

  p->all_cases_button = get_widget_assert (builder, "import-all-cases");

  p->n_cases_button = get_widget_assert (builder, "import-n-cases");

  p->percent_button = get_widget_assert (builder, "import-percent");

  g_signal_connect_swapped (p->all_cases_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), ia);
  g_signal_connect_swapped (p->n_cases_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), ia);
  g_signal_connect_swapped (p->percent_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), ia);

  on_intro_amount_changed (ia);

  ds_init_empty (&s);
  ds_put_cstr (&s, _("This assistant will guide you through the process of "
                     "importing data into PSPP from a text file with one line "
                     "per case,  in which fields are separated by tabs, "
                     "commas, or other delimiters.\n\n"));
  if (ia->file.total_is_exact)
    ds_put_format (
      &s, ngettext ("The selected file contains %zu line of text.  ",
                    "The selected file contains %zu lines of text.  ",
                    ia->file.line_cnt),
      ia->file.line_cnt);
  else if (ia->file.total_lines > 0)
    {
      ds_put_format (
        &s, ngettext (
          "The selected file contains approximately %lu line of text.  ",
          "The selected file contains approximately %lu lines of text.  ",
          ia->file.total_lines),
        ia->file.total_lines);
      ds_put_format (
        &s, ngettext (
          "Only the first %zu line of the file will be shown for "
          "preview purposes in the following screens.  ",
          "Only the first %zu lines of the file will be shown for "
          "preview purposes in the following screens.  ",
          ia->file.line_cnt),
        ia->file.line_cnt);
    }
  ds_put_cstr (&s, _("You may choose below how much of the file should "
                     "actually be imported."));
  gtk_label_set_text (GTK_LABEL (get_widget_assert (builder, "intro-label")),
                      ds_cstr (&s));
  ds_destroy (&s);
}

/* Resets IA's intro page to its initial state. */
static void
reset_intro_page (struct import_assistant *ia)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->intro.all_cases_button),
                                true);
}

/* Called when one of the radio buttons is clicked. */
static void
on_intro_amount_changed (struct import_assistant *ia)
{
  struct intro_page *p = &ia->intro;

  gtk_widget_set_sensitive (p->n_cases_spin,
                            gtk_toggle_button_get_active (
                              GTK_TOGGLE_BUTTON (p->n_cases_button)));

  gtk_widget_set_sensitive (p->percent_spin,
                            gtk_toggle_button_get_active (
                              GTK_TOGGLE_BUTTON (p->percent_button)));
}

/* The "first line" page of the assistant. */

static PsppSheetView *create_lines_tree_view (GtkContainer *parent_window,
                                            struct import_assistant *);
static void on_first_line_change (PsppSheetSelection *,
                                  struct import_assistant *);
static void on_variable_names_cb_toggle (GtkToggleButton *,
                                         struct import_assistant *);
static void set_first_line (struct import_assistant *);
static void get_first_line (struct import_assistant *);

/* Initializes IA's first_line substructure. */
static void
init_first_line_page (struct import_assistant *ia)
{
  struct first_line_page *p = &ia->first_line;
  GtkBuilder *builder = ia->asst.builder;

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "FirstLine"),
                                   GTK_ASSISTANT_PAGE_CONTENT);
  gtk_widget_destroy (get_widget_assert (builder, "first-line"));
  p->tree_view = create_lines_tree_view (
    GTK_CONTAINER (get_widget_assert (builder, "first-line-scroller")), ia);
  p->variable_names_cb = get_widget_assert (builder, "variable-names");
  pspp_sheet_selection_set_mode (
    pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (p->tree_view)),
    PSPP_SHEET_SELECTION_BROWSE);
  pspp_sheet_view_set_rubber_banding (PSPP_SHEET_VIEW (p->tree_view), TRUE);
  set_first_line (ia);
  g_signal_connect (pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (p->tree_view)),
                    "changed", G_CALLBACK (on_first_line_change), ia);
  g_signal_connect (p->variable_names_cb, "toggled",
                    G_CALLBACK (on_variable_names_cb_toggle), ia);
}

/* Resets the first_line page to its initial content. */
static void
reset_first_line_page (struct import_assistant *ia)
{
  ia->first_line.skip_lines = 0;
  ia->first_line.variable_names = false;
  set_first_line (ia);
}

static void
render_line (PsppSheetViewColumn *tree_column,
             GtkCellRenderer *cell,
             GtkTreeModel *tree_model,
             GtkTreeIter *iter,
             gpointer data)
{
  gint row = empty_list_store_iter_to_row (iter);
  struct string *lines;

  lines = g_object_get_data (G_OBJECT (tree_model), "lines");
  g_return_if_fail (lines != NULL);

  g_object_set (cell, "text", ds_cstr (&lines[row]), NULL);
}


/* Creates and returns a tree view that contains each of the
   lines in IA's file as a row. */
static PsppSheetView *
create_lines_tree_view (GtkContainer *parent, struct import_assistant *ia)
{
  PsppSheetView *tree_view;
  PsppSheetViewColumn *column;
  size_t max_line_length;
  gint content_width, header_width;
  size_t i;
  const gchar *title = _("Text");

  make_tree_view (ia, 0, &tree_view);

  column = pspp_sheet_view_column_new_with_attributes (
    title, ia->asst.fixed_renderer, (void *) NULL);
  pspp_sheet_view_column_set_cell_data_func (column, ia->asst.fixed_renderer,
                                           render_line, NULL, NULL);
  pspp_sheet_view_column_set_resizable (column, TRUE);

  max_line_length = 0;
  for (i = 0; i < ia->file.line_cnt; i++)
    {
      size_t w = ds_length (&ia->file.lines[i]);
      max_line_length = MAX (max_line_length, w);
    }

  content_width = get_monospace_width (tree_view, ia->asst.fixed_renderer,
                                       max_line_length);
  header_width = get_string_width (tree_view, ia->asst.prop_renderer, title);
  pspp_sheet_view_column_set_fixed_width (column, MAX (content_width,
                                                     header_width));
  pspp_sheet_view_append_column (tree_view, column);

  gtk_container_add (parent, GTK_WIDGET (tree_view));
  gtk_widget_show (GTK_WIDGET (tree_view));

  return tree_view;
}

/* Called when the line selected in the first_line tree view
   changes. */
static void
on_first_line_change (PsppSheetSelection *selection UNUSED,
                      struct import_assistant *ia)
{
  get_first_line (ia);
}

/* Called when the checkbox that indicates whether variable
   names are in the row above the first line is toggled. */
static void
on_variable_names_cb_toggle (GtkToggleButton *variable_names_cb UNUSED,
                             struct import_assistant *ia)
{
  get_first_line (ia);
}

/* Sets the widgets to match IA's first_line substructure. */
static void
set_first_line (struct import_assistant *ia)
{
  GtkTreePath *path;

  path = gtk_tree_path_new_from_indices (ia->first_line.skip_lines, -1);
  pspp_sheet_view_set_cursor (PSPP_SHEET_VIEW (ia->first_line.tree_view),
                            path, NULL, false);
  gtk_tree_path_free (path);

  gtk_toggle_button_set_active (
    GTK_TOGGLE_BUTTON (ia->first_line.variable_names_cb),
    ia->first_line.variable_names);
  gtk_widget_set_sensitive (ia->first_line.variable_names_cb,
                            ia->first_line.skip_lines > 0);
}

/* Sets IA's first_line substructure to match the widgets. */
static void
get_first_line (struct import_assistant *ia)
{
  PsppSheetSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;

  selection = pspp_sheet_view_get_selection (ia->first_line.tree_view);
  if (pspp_sheet_selection_get_selected (selection, &model, &iter))
    {
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      int row = gtk_tree_path_get_indices (path)[0];
      gtk_tree_path_free (path);

      ia->first_line.skip_lines = row;
      ia->first_line.variable_names =
        (ia->first_line.skip_lines > 0
         && gtk_toggle_button_get_active (
           GTK_TOGGLE_BUTTON (ia->first_line.variable_names_cb)));
    }
  gtk_widget_set_sensitive (ia->first_line.variable_names_cb,
                            ia->first_line.skip_lines > 0);
}

/* The "separators" page of the assistant. */

static void revise_fields_preview (struct import_assistant *ia);
static void choose_likely_separators (struct import_assistant *ia);
static void find_commonest_chars (unsigned long int histogram[UCHAR_MAX + 1],
                                  const char *targets, const char *def,
                                  struct string *result);
static void clear_fields (struct import_assistant *ia);
static void revise_fields_preview (struct import_assistant *);
static void set_separators (struct import_assistant *);
static void get_separators (struct import_assistant *);
static void on_separators_custom_entry_notify (GObject *UNUSED,
                                               GParamSpec *UNUSED,
                                               struct import_assistant *);
static void on_separators_custom_cb_toggle (GtkToggleButton *custom_cb,
                                            struct import_assistant *);
static void on_quote_combo_change (GtkComboBox *combo,
                                   struct import_assistant *);
static void on_quote_cb_toggle (GtkToggleButton *quote_cb,
                                struct import_assistant *);
static void on_separator_toggle (GtkToggleButton *, struct import_assistant *);
static void render_input_cell (PsppSheetViewColumn *tree_column,
                               GtkCellRenderer *cell,
                               GtkTreeModel *model, GtkTreeIter *iter,
                               gpointer ia);
static gboolean on_query_input_tooltip (GtkWidget *widget, gint wx, gint wy,
                                        gboolean keyboard_mode UNUSED,
                                        GtkTooltip *tooltip,
                                        struct import_assistant *);

/* A common field separator and its identifying name. */
struct separator
  {
    const char *name;           /* Name (for use with get_widget_assert). */
    int c;                      /* Separator character. */
  };

/* All the separators in the dialog box. */
static const struct separator separators[] =
  {
    {"space", ' '},
    {"tab", '\t'},
    {"bang", '!'},
    {"colon", ':'},
    {"comma", ','},
    {"hyphen", '-'},
    {"pipe", '|'},
    {"semicolon", ';'},
    {"slash", '/'},
  };
#define SEPARATOR_CNT (sizeof separators / sizeof *separators)

static void
set_quote_list (GtkComboBoxEntry *cb)
{
  GtkListStore *list =  gtk_list_store_new (1, G_TYPE_STRING);
  GtkTreeIter iter;
  gint i;
  const gchar *seperator[3] = {"'\"", "\'", "\""};

  for (i = 0; i < 3; i++)
    {
      const gchar *s = seperator[i];

      /* Add a new row to the model */
      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
                          0, s,
                          -1);

    }

  gtk_combo_box_set_model (GTK_COMBO_BOX (cb), GTK_TREE_MODEL (list));
  g_object_unref (list);

  gtk_combo_box_entry_set_text_column (cb, 0);
}

/* Initializes IA's separators substructure. */
static void
init_separators_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct separators_page *p = &ia->separators;
  size_t i;

  choose_likely_separators (ia);

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Separators"),
                                   GTK_ASSISTANT_PAGE_CONTENT);
  p->custom_cb = get_widget_assert (builder, "custom-cb");
  p->custom_entry = get_widget_assert (builder, "custom-entry");
  p->quote_combo = get_widget_assert (builder, "quote-combo");
  p->quote_entry = GTK_ENTRY (gtk_bin_get_child (GTK_BIN (p->quote_combo)));
  p->quote_cb = get_widget_assert (builder, "quote-cb");
  p->escape_cb = get_widget_assert (builder, "escape");

  set_separators (ia);
  set_quote_list (GTK_COMBO_BOX_ENTRY (p->quote_combo));
  p->fields_tree_view = PSPP_SHEET_VIEW (get_widget_assert (builder, "fields"));
  g_signal_connect (p->quote_combo, "changed",
                    G_CALLBACK (on_quote_combo_change), ia);
  g_signal_connect (p->quote_cb, "toggled",
                    G_CALLBACK (on_quote_cb_toggle), ia);
  g_signal_connect (p->custom_entry, "notify::text",
                    G_CALLBACK (on_separators_custom_entry_notify), ia);
  g_signal_connect (p->custom_cb, "toggled",
                    G_CALLBACK (on_separators_custom_cb_toggle), ia);
  for (i = 0; i < SEPARATOR_CNT; i++)
    g_signal_connect (get_widget_assert (builder, separators[i].name),
                      "toggled", G_CALLBACK (on_separator_toggle), ia);
  g_signal_connect (p->escape_cb, "toggled",
                    G_CALLBACK (on_separator_toggle), ia);
}

/* Frees IA's separators substructure. */
static void
destroy_separators_page (struct import_assistant *ia)
{
  struct separators_page *s = &ia->separators;

  ds_destroy (&s->separators);
  ds_destroy (&s->quotes);
  clear_fields (ia);
}

/* Called just before the separators page becomes visible in the
   assistant. */
static void
prepare_separators_page (struct import_assistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the Reset button is clicked on the separators
   page, resets the separators to the defaults. */
static void
reset_separators_page (struct import_assistant *ia)
{
  choose_likely_separators (ia);
  set_separators (ia);
}

/* Frees and clears the column data in IA's separators
   substructure. */
static void
clear_fields (struct import_assistant *ia)
{
  struct separators_page *s = &ia->separators;

  if (s->column_cnt > 0)
    {
      struct column *col;
      size_t row;

      for (row = 0; row < ia->file.line_cnt; row++)
        {
          const struct string *line = &ia->file.lines[row];
          const char *line_start = ds_data (line);
          const char *line_end = ds_end (line);

          for (col = s->columns; col < &s->columns[s->column_cnt]; col++)
            {
              char *s = ss_data (col->contents[row]);
              if (!(s >= line_start && s <= line_end))
                ss_dealloc (&col->contents[row]);
            }
        }

      for (col = s->columns; col < &s->columns[s->column_cnt]; col++)
        {
          free (col->name);
          free (col->contents);
        }

      free (s->columns);
      s->columns = NULL;
      s->column_cnt = 0;
    }
}

/* Breaks the file data in IA into columns based on the
   separators set in IA's separators substructure. */
static void
split_fields (struct import_assistant *ia)
{
  struct separators_page *s = &ia->separators;
  size_t columns_allocated;
  bool space_sep;
  size_t row;

  clear_fields (ia);

  /* Is space in the set of separators? */
  space_sep = ss_find_byte (ds_ss (&s->separators), ' ') != SIZE_MAX;

  /* Split all the lines, not just those from
     ia->first_line.skip_lines on, so that we split the line that
     contains variables names if ia->first_line.variable_names is
     true. */
  columns_allocated = 0;
  for (row = 0; row < ia->file.line_cnt; row++)
    {
      struct string *line = &ia->file.lines[row];
      struct substring text = ds_ss (line);
      size_t column_idx;

      for (column_idx = 0; ; column_idx++)
        {
          struct substring field;
          struct column *column;

          if (space_sep)
            ss_ltrim (&text, ss_cstr (" "));
          if (ss_is_empty (text))
            {
              if (column_idx != 0)
                break;
              field = text;
            }
          else if (!ds_is_empty (&s->quotes)
                   && ds_find_byte (&s->quotes, text.string[0]) != SIZE_MAX)
            {
              int quote = ss_get_byte (&text);
              if (!s->escape)
                ss_get_until (&text, quote, &field);
              else
                {
                  struct string s;
                  int c;

                  ds_init_empty (&s);
                  while ((c = ss_get_byte (&text)) != EOF)
                    if (c != quote)
                      ds_put_byte (&s, c);
                    else if (ss_match_byte (&text, quote))
                      ds_put_byte (&s, quote);
                    else
                      break;
                  field = ds_ss (&s);
                }
            }
          else
            ss_get_bytes (&text, ss_cspan (text, ds_ss (&s->separators)),
                          &field);

          if (column_idx >= s->column_cnt)
            {
              struct column *column;

              if (s->column_cnt >= columns_allocated)
                s->columns = x2nrealloc (s->columns, &columns_allocated,
                                         sizeof *s->columns);
              column = &s->columns[s->column_cnt++];
              column->name = NULL;
              column->width = 0;
              column->contents = xcalloc (ia->file.line_cnt,
                                          sizeof *column->contents);
            }
          column = &s->columns[column_idx];
          column->contents[row] = field;
          if (ss_length (field) > column->width)
            column->width = ss_length (field);

          if (space_sep)
            ss_ltrim (&text, ss_cstr (" "));
          if (ss_is_empty (text))
            break;
          if (ss_find_byte (ds_ss (&s->separators), ss_first (text))
              != SIZE_MAX)
            ss_advance (&text, 1);
        }
    }
}

/* Chooses a name for each column on the separators page */
static void
choose_column_names (struct import_assistant *ia)
{
  const struct first_line_page *f = &ia->first_line;
  struct separators_page *s = &ia->separators;
  struct dictionary *dict;
  unsigned long int generated_name_count = 0;
  struct column *col;
  size_t name_row;

  dict = dict_create (get_default_encoding ());
  name_row = f->variable_names && f->skip_lines ? f->skip_lines : 0;
  for (col = s->columns; col < &s->columns[s->column_cnt]; col++)
    {
      char *hint, *name;

      hint = name_row ? ss_xstrdup (col->contents[name_row - 1]) : NULL;
      name = dict_make_unique_var_name (dict, hint, &generated_name_count);
      free (hint);

      col->name = name;
      dict_create_var_assert (dict, name, 0);
    }
  dict_destroy (dict);
}

/* Picks the most likely separator and quote characters based on
   IA's file data. */
static void
choose_likely_separators (struct import_assistant *ia)
{
  unsigned long int histogram[UCHAR_MAX + 1] = { 0 };
  size_t row;

  /* Construct a histogram of all the characters used in the
     file. */
  for (row = 0; row < ia->file.line_cnt; row++)
    {
      struct substring line = ds_ss (&ia->file.lines[row]);
      size_t length = ss_length (line);
      size_t i;
      for (i = 0; i < length; i++)
        histogram[(unsigned char) line.string[i]]++;
    }

  find_commonest_chars (histogram, "\"'", "", &ia->separators.quotes);
  find_commonest_chars (histogram, ",;:/|!\t-", ",",
                        &ia->separators.separators);
  ia->separators.escape = true;
}

/* Chooses the most common character among those in TARGETS,
   based on the frequency data in HISTOGRAM, and stores it in
   RESULT.  If there is a tie for the most common character among
   those in TARGETS, the earliest character is chosen.  If none
   of the TARGETS appear at all, then DEF is used as a
   fallback. */
static void
find_commonest_chars (unsigned long int histogram[UCHAR_MAX + 1],
                      const char *targets, const char *def,
                      struct string *result)
{
  unsigned char max = 0;
  unsigned long int max_count = 0;

  for (; *targets != '\0'; targets++)
    {
      unsigned char c = *targets;
      unsigned long int count = histogram[c];
      if (count > max_count)
        {
          max = c;
          max_count = count;
        }
    }
  if (max_count > 0)
    {
      ds_clear (result);
      ds_put_byte (result, max);
    }
  else
    ds_assign_cstr (result, def);
}

/* Revises the contents of the fields tree view based on the
   currently chosen set of separators. */
static void
revise_fields_preview (struct import_assistant *ia)
{
  GtkWidget *w;

  push_watch_cursor (ia);

  w = GTK_WIDGET (ia->separators.fields_tree_view);
  gtk_widget_destroy (w);
  get_separators (ia);
  split_fields (ia);
  choose_column_names (ia);
  ia->separators.fields_tree_view = create_data_tree_view (
    true,
    GTK_CONTAINER (get_widget_assert (ia->asst.builder, "fields-scroller")),
    ia);

  pop_watch_cursor (ia);
}

/* Sets the widgets to match IA's separators substructure. */
static void
set_separators (struct import_assistant *ia)
{
  struct separators_page *s = &ia->separators;
  unsigned int seps;
  struct string custom;
  bool any_custom;
  bool any_quotes;
  size_t i;

  ds_init_empty (&custom);
  seps = 0;
  for (i = 0; i < ds_length (&s->separators); i++)
    {
      unsigned char c = ds_at (&s->separators, i);
      int j;

      for (j = 0; j < SEPARATOR_CNT; j++)
        {
          const struct separator *s = &separators[j];
          if (s->c == c)
            {
              seps += 1u << j;
              goto next;
            }
        }

      ds_put_byte (&custom, c);
    next:;
    }

  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *s = &separators[i];
      GtkWidget *button = get_widget_assert (ia->asst.builder, s->name);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                    (seps & (1u << i)) != 0);
    }
  any_custom = !ds_is_empty (&custom);
  gtk_entry_set_text (GTK_ENTRY (s->custom_entry), ds_cstr (&custom));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (s->custom_cb),
                                any_custom);
  gtk_widget_set_sensitive (s->custom_entry, any_custom);
  ds_destroy (&custom);

  any_quotes = !ds_is_empty (&s->quotes);

  gtk_entry_set_text (s->quote_entry,
                      any_quotes ? ds_cstr (&s->quotes) : "\"");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (s->quote_cb),
                                any_quotes);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (s->escape_cb),
                                s->escape);
  gtk_widget_set_sensitive (s->quote_combo, any_quotes);
  gtk_widget_set_sensitive (s->escape_cb, any_quotes);
}

/* Sets IA's separators substructure to match the widgets. */
static void
get_separators (struct import_assistant *ia)
{
  struct separators_page *s = &ia->separators;
  int i;

  ds_clear (&s->separators);
  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *sep = &separators[i];
      GtkWidget *button = get_widget_assert (ia->asst.builder, sep->name);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
        ds_put_byte (&s->separators, sep->c);
    }

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (s->custom_cb)))
    ds_put_cstr (&s->separators,
                 gtk_entry_get_text (GTK_ENTRY (s->custom_entry)));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (s->quote_cb)))
    {
      gchar *text = gtk_combo_box_get_active_text (
                      GTK_COMBO_BOX (s->quote_combo));
      ds_assign_cstr (&s->quotes, text);
      g_free (text);
    }
  else
    ds_clear (&s->quotes);
  s->escape = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (s->escape_cb));
}

/* Called when the user changes the entry field for custom
   separators. */
static void
on_separators_custom_entry_notify (GObject *gobject UNUSED,
                                   GParamSpec *arg1 UNUSED,
                                   struct import_assistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the user toggles the checkbox that enables custom
   separators. */
static void
on_separators_custom_cb_toggle (GtkToggleButton *custom_cb,
                                struct import_assistant *ia)
{
  bool is_active = gtk_toggle_button_get_active (custom_cb);
  gtk_widget_set_sensitive (ia->separators.custom_entry, is_active);
  revise_fields_preview (ia);
}

/* Called when the user changes the selection in the combo box
   that selects a quote character. */
static void
on_quote_combo_change (GtkComboBox *combo, struct import_assistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the user toggles the checkbox that enables
   quoting. */
static void
on_quote_cb_toggle (GtkToggleButton *quote_cb, struct import_assistant *ia)
{
  bool is_active = gtk_toggle_button_get_active (quote_cb);
  gtk_widget_set_sensitive (ia->separators.quote_combo, is_active);
  gtk_widget_set_sensitive (ia->separators.escape_cb, is_active);
  revise_fields_preview (ia);
}

/* Called when the user toggles one of the separators
   checkboxes. */
static void
on_separator_toggle (GtkToggleButton *toggle UNUSED,
                     struct import_assistant *ia)
{
  revise_fields_preview (ia);
}

/* Called to render one of the cells in the fields preview tree
   view. */
static void
render_input_cell (PsppSheetViewColumn *tree_column, GtkCellRenderer *cell,
                   GtkTreeModel *model, GtkTreeIter *iter,
                   gpointer ia_)
{
  struct import_assistant *ia = ia_;
  struct substring field;
  size_t row;
  gint column;

  column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                               "column-number"));
  row = empty_list_store_iter_to_row (iter) + ia->first_line.skip_lines;
  field = ia->separators.columns[column].contents[row];
  if (field.string != NULL)
    {
      GValue text = {0, };
      g_value_init (&text, G_TYPE_STRING);
      g_value_take_string (&text, ss_xstrdup (field));
      g_object_set_property (G_OBJECT (cell), "text", &text);
      g_value_unset (&text);
      g_object_set (cell, "background-set", FALSE, (void *) NULL);
    }
  else
    g_object_set (cell,
                  "text", "",
                  "background", "red",
                  "background-set", TRUE,
                  (void *) NULL);
}

/* Called to render a tooltip on one of the cells in the fields
   preview tree view. */
static gboolean
on_query_input_tooltip (GtkWidget *widget, gint wx, gint wy,
                        gboolean keyboard_mode UNUSED,
                        GtkTooltip *tooltip, struct import_assistant *ia)
{
  size_t row, column;

  if (!get_tooltip_location (widget, wx, wy, ia, &row, &column))
    return FALSE;

  if (ia->separators.columns[column].contents[row].string != NULL)
    return FALSE;

  gtk_tooltip_set_text (tooltip,
                        _("This input line has too few separators "
                          "to fill in this field."));
  return TRUE;
}

/* The "formats" page of the assistant. */

static void on_variable_change (PsppireDict *dict, int idx,
                                struct import_assistant *);
static void clear_modified_vars (struct import_assistant *);

/* Initializes IA's formats substructure. */
static void
init_formats_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct formats_page *p = &ia->formats;

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Formats"),
                                   GTK_ASSISTANT_PAGE_CONFIRM);
  p->data_tree_view = PSPP_SHEET_VIEW (get_widget_assert (builder, "data"));
  p->modified_vars = NULL;
  p->modified_var_cnt = 0;
  p->dict = NULL;
}

/* Frees IA's formats substructure. */
static void
destroy_formats_page (struct import_assistant *ia)
{
  struct formats_page *p = &ia->formats;

  if (p->psppire_dict != NULL)
    {
      dict_destroy (p->psppire_dict->dict);
      g_object_unref (p->psppire_dict);
    }
  clear_modified_vars (ia);
}

/* Called just before the formats page of the assistant is
   displayed. */
static void
prepare_formats_page (struct import_assistant *ia)
{
  struct dictionary *dict;
  PsppireDict *psppire_dict;
  GtkBin *vars_scroller;
  GtkWidget *old_var_sheet;
  PsppireVarSheet *var_sheet;
  struct separators_page *s = &ia->separators;
  struct formats_page *p = &ia->formats;
  struct fmt_guesser *fg;
  unsigned long int number = 0;
  size_t column_idx;

  push_watch_cursor (ia);

  dict = dict_create (get_default_encoding ());
  fg = fmt_guesser_create ();
  for (column_idx = 0; column_idx < s->column_cnt; column_idx++)
    {
      struct variable *modified_var;

      modified_var = (column_idx < p->modified_var_cnt
                      ? p->modified_vars[column_idx] : NULL);
      if (modified_var == NULL)
        {
          struct column *column = &s->columns[column_idx];
          struct variable *var;
          struct fmt_spec format;
          char *name;
          size_t row;

          /* Choose variable name. */
          name = dict_make_unique_var_name (dict, column->name, &number);

          /* Choose variable format. */
          fmt_guesser_clear (fg);
          for (row = ia->first_line.skip_lines; row < ia->file.line_cnt; row++)
            fmt_guesser_add (fg, column->contents[row]);
          fmt_guesser_guess (fg, &format);
          fmt_fix_input (&format);

          /* Create variable. */
          var = dict_create_var_assert (dict, name, fmt_var_width (&format));
          var_set_both_formats (var, &format);

          free (name);
        }
      else
        {
          char *name;

          name = dict_make_unique_var_name (dict, var_get_name (modified_var),
                                            &number);
          dict_clone_var_as_assert (dict, modified_var, name);
          free (name);
        }
    }
  fmt_guesser_destroy (fg);

  psppire_dict = psppire_dict_new_from_dict (dict);
  g_signal_connect (psppire_dict, "variable_changed",
                    G_CALLBACK (on_variable_change), ia);
  ia->formats.dict = dict;
  ia->formats.psppire_dict = psppire_dict;

  /* XXX: PsppireVarStore doesn't hold a reference to
     psppire_dict for now, but it should.  After it does, we
     should g_object_ref the psppire_dict here, since we also
     hold a reference via ia->formats.dict. */
  var_sheet = PSPPIRE_VAR_SHEET (psppire_var_sheet_new ());
  g_object_set (var_sheet,
                "dictionary", psppire_dict,
                "may-create-vars", FALSE,
                "may-delete-vars", FALSE,
                "format-use", FMT_FOR_INPUT,
                "enable-grid-lines", PSPP_SHEET_VIEW_GRID_LINES_BOTH,
                (void *) NULL);

  vars_scroller = GTK_BIN (get_widget_assert (ia->asst.builder, "vars-scroller"));
  old_var_sheet = gtk_bin_get_child (vars_scroller);
  if (old_var_sheet != NULL)
    gtk_widget_destroy (old_var_sheet);
  gtk_container_add (GTK_CONTAINER (vars_scroller), GTK_WIDGET (var_sheet));
  gtk_widget_show (GTK_WIDGET (var_sheet));

  gtk_widget_destroy (GTK_WIDGET (ia->formats.data_tree_view));
  ia->formats.data_tree_view = create_data_tree_view (
    false,
    GTK_CONTAINER (get_widget_assert (ia->asst.builder, "data-scroller")),
    ia);

  pop_watch_cursor (ia);
}

/* Clears the set of user-modified variables from IA's formats
   substructure.  This discards user modifications to variable
   formats, thereby causing formats to revert to their
   defaults.  */
static void
clear_modified_vars (struct import_assistant *ia)
{
  struct formats_page *p = &ia->formats;
  size_t i;

  for (i = 0; i < p->modified_var_cnt; i++)
    var_destroy (p->modified_vars[i]);
  free (p->modified_vars);
  p->modified_vars = NULL;
  p->modified_var_cnt = 0;
}

/* Resets the formats page to its defaults, discarding user
   modifications. */
static void
reset_formats_page (struct import_assistant *ia)
{
  clear_modified_vars (ia);
  prepare_formats_page (ia);
}

/* Called when the user changes one of the variables in the
   dictionary. */
static void
on_variable_change (PsppireDict *dict, int dict_idx,
                    struct import_assistant *ia)
{
  struct formats_page *p = &ia->formats;
  PsppSheetView *tv = ia->formats.data_tree_view;
  gint column_idx = dict_idx + 1;

  push_watch_cursor (ia);

  /* Remove previous column and replace with new column. */
  pspp_sheet_view_remove_column (tv, pspp_sheet_view_get_column (tv, column_idx));
  pspp_sheet_view_insert_column (tv, make_data_column (ia, tv, false, dict_idx),
                               column_idx);

  /* Save a copy of the modified variable in modified_vars, so
     that its attributes will be preserved if we back up to the
     previous page with the Prev button and then come back
     here. */
  if (dict_idx >= p->modified_var_cnt)
    {
      size_t i;
      p->modified_vars = xnrealloc (p->modified_vars, dict_idx + 1,
                                    sizeof *p->modified_vars);
      for (i = 0; i <= dict_idx; i++)
        p->modified_vars[i] = NULL;
      p->modified_var_cnt = dict_idx + 1;
    }
  if (p->modified_vars[dict_idx])
    var_destroy (p->modified_vars[dict_idx]);
  p->modified_vars[dict_idx]
    = var_clone (psppire_dict_get_variable (dict, dict_idx));

  pop_watch_cursor (ia);
}

/* Parses the contents of the field at (ROW,COLUMN) according to
   its variable format.  If OUTPUTP is non-null, then *OUTPUTP
   receives the formatted output for that field (which must be
   freed with free).  If TOOLTIPP is non-null, then *TOOLTIPP
   receives a message suitable for use in a tooltip, if one is
   needed, or a null pointer otherwise.  Returns true if a
   tooltip message is needed, otherwise false. */
static bool
parse_field (struct import_assistant *ia,
             size_t row, size_t column,
             char **outputp, char **tooltipp)
{
  struct substring field;
  union value val;
  struct variable *var;
  const struct fmt_spec *in;
  struct fmt_spec out;
  char *tooltip;
  bool ok;

  field = ia->separators.columns[column].contents[row];
  var = dict_get_var (ia->formats.dict, column);
  value_init (&val, var_get_width (var));
  in = var_get_print_format (var);
  out = fmt_for_output_from_input (in);
  tooltip = NULL;
  if (field.string != NULL)
    {
      char *error;

      error = data_in (field, "UTF-8", in->type, &val, var_get_width (var),
                       dict_get_encoding (ia->formats.dict));
      if (error != NULL)
        {
          tooltip = xasprintf (_("Cannot parse field content `%.*s' as "
                                 "format %s: %s"),
                               (int) field.length, field.string,
                               fmt_name (in->type), error);
          free (error);
        }
    }
  else
    {
      tooltip = xstrdup (_("This input line has too few separators "
                           "to fill in this field."));
      value_set_missing (&val, var_get_width (var));
    }
  if (outputp != NULL)
    {
      *outputp = data_out (&val, dict_get_encoding (ia->formats.dict),  &out);
    }
  value_destroy (&val, var_get_width (var));

  ok = tooltip == NULL;
  if (tooltipp != NULL)
    *tooltipp = tooltip;
  else
    free (tooltip);
  return ok;
}

/* Called to render one of the cells in the data preview tree
   view. */
static void
render_output_cell (PsppSheetViewColumn *tree_column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *model,
                    GtkTreeIter *iter,
                    gpointer ia_)
{
  struct import_assistant *ia = ia_;
  char *output;
  GValue gvalue = { 0, };
  bool ok;

  ok = parse_field (ia,
                    (empty_list_store_iter_to_row (iter)
                     + ia->first_line.skip_lines),
                    GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                                        "column-number")),
                    &output, NULL);

  g_value_init (&gvalue, G_TYPE_STRING);
  g_value_take_string (&gvalue, output);
  g_object_set_property (G_OBJECT (cell), "text", &gvalue);
  g_value_unset (&gvalue);

  if (ok)
    g_object_set (cell, "background-set", FALSE, (void *) NULL);
  else
    g_object_set (cell,
                  "background", "red",
                  "background-set", TRUE,
                  (void *) NULL);
}

/* Called to render a tooltip for one of the cells in the data
   preview tree view. */
static gboolean
on_query_output_tooltip (GtkWidget *widget, gint wx, gint wy,
                        gboolean keyboard_mode UNUSED,
                        GtkTooltip *tooltip, struct import_assistant *ia)
{
  size_t row, column;
  char *text;

  if (!get_tooltip_location (widget, wx, wy, ia, &row, &column))
    return FALSE;

  if (parse_field (ia, row, column, NULL, &text))
    return FALSE;

  gtk_tooltip_set_text (tooltip, text);
  free (text);
  return TRUE;
}

/* Utility functions used by multiple pages of the assistant. */

static gboolean
get_tooltip_location (GtkWidget *widget, gint wx, gint wy,
                      const struct import_assistant *ia,
                      size_t *row, size_t *column)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  gint bx, by;
  GtkTreePath *path;
  GtkTreeIter iter;
  PsppSheetViewColumn *tree_column;
  GtkTreeModel *tree_model;
  bool ok;

  /* Check that WIDGET is really visible on the screen before we
     do anything else.  This is a bug fix for a sticky situation:
     when text_data_import_assistant() returns, it frees the data
     necessary to compose the tool tip message, but there may be
     a tool tip under preparation at that point (even if there is
     no visible tool tip) that will call back into us a little
     bit later.  Perhaps the correct solution to this problem is
     to make the data related to the tool tips part of a GObject
     that only gets destroyed when all references are released,
     but this solution appears to be effective too. */
  if (!gtk_widget_get_mapped (widget))
    return FALSE;

  pspp_sheet_view_convert_widget_to_bin_window_coords (tree_view,
                                                     wx, wy, &bx, &by);
  if (!pspp_sheet_view_get_path_at_pos (tree_view, bx, by,
                                      &path, &tree_column, NULL, NULL))
    return FALSE;

  *column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                                "column-number"));

  tree_model = pspp_sheet_view_get_model (tree_view);
  ok = gtk_tree_model_get_iter (tree_model, &iter, path);
  gtk_tree_path_free (path);
  if (!ok)
    return FALSE;

  *row = empty_list_store_iter_to_row (&iter) + ia->first_line.skip_lines;
  return TRUE;
}

static void
make_tree_view (const struct import_assistant *ia,
                size_t first_line,
                PsppSheetView **tree_view)
{
  GtkTreeModel *model;

  *tree_view = PSPP_SHEET_VIEW (pspp_sheet_view_new ());
  pspp_sheet_view_set_grid_lines (*tree_view, PSPP_SHEET_VIEW_GRID_LINES_BOTH);
  model = GTK_TREE_MODEL (psppire_empty_list_store_new (
                            ia->file.line_cnt - first_line));
  g_object_set_data (G_OBJECT (model), "lines", ia->file.lines + first_line);
  g_object_set_data (G_OBJECT (model), "first-line",
                     GINT_TO_POINTER (first_line));
  pspp_sheet_view_set_model (*tree_view, model);
  g_object_unref (model);

  add_line_number_column (ia, *tree_view);
}

static void
render_line_number (PsppSheetViewColumn *tree_column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *tree_model,
                    GtkTreeIter *iter,
                    gpointer data)
{
  gint row = empty_list_store_iter_to_row (iter);
  char s[INT_BUFSIZE_BOUND (int)];
  int first_line;

  first_line = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_model),
                                                   "first-line"));
  sprintf (s, "%d", first_line + row);
  g_object_set (cell, "text", s, NULL);
}

static void
add_line_number_column (const struct import_assistant *ia,
                        PsppSheetView *treeview)
{
  PsppSheetViewColumn *column;

  column = pspp_sheet_view_column_new_with_attributes (
    _("Line"), ia->asst.prop_renderer, (void *) NULL);
  pspp_sheet_view_column_set_fixed_width (
    column, get_monospace_width (treeview, ia->asst.prop_renderer, 5));
  pspp_sheet_view_column_set_resizable (column, TRUE);
  pspp_sheet_view_column_set_cell_data_func (column, ia->asst.prop_renderer,
                                             render_line_number, NULL, NULL);
  pspp_sheet_view_append_column (treeview, column);
}

static gint
get_monospace_width (PsppSheetView *treeview, GtkCellRenderer *renderer,
                     size_t char_cnt)
{
  struct string s;
  gint width;

  ds_init_empty (&s);
  ds_put_byte_multiple (&s, '0', char_cnt);
  ds_put_byte (&s, ' ');
  width = get_string_width (treeview, renderer, ds_cstr (&s));
  ds_destroy (&s);

  return width;
}

static gint
get_string_width (PsppSheetView *treeview, GtkCellRenderer *renderer,
                  const char *string)
{
  gint width;
  g_object_set (G_OBJECT (renderer), "text", string, (void *) NULL);
  gtk_cell_renderer_get_size (renderer, GTK_WIDGET (treeview),
                              NULL, NULL, NULL, &width, NULL);
  return width;
}

static PsppSheetViewColumn *
make_data_column (struct import_assistant *ia, PsppSheetView *tree_view,
                  bool input, gint dict_idx)
{
  struct variable *var = NULL;
  struct column *column = NULL;
  size_t char_cnt;
  gint content_width, header_width;
  PsppSheetViewColumn *tree_column;
  char *name;

  if (input)
    column = &ia->separators.columns[dict_idx];
  else
    var = dict_get_var (ia->formats.dict, dict_idx);

  name = escape_underscores (input ? column->name : var_get_name (var));
  char_cnt = input ? column->width : var_get_print_format (var)->w;
  content_width = get_monospace_width (tree_view, ia->asst.fixed_renderer,
                                       char_cnt);
  header_width = get_string_width (tree_view, ia->asst.prop_renderer,
                                   name);

  tree_column = pspp_sheet_view_column_new ();
  g_object_set_data (G_OBJECT (tree_column), "column-number",
                     GINT_TO_POINTER (dict_idx));
  pspp_sheet_view_column_set_title (tree_column, name);
  pspp_sheet_view_column_pack_start (tree_column, ia->asst.fixed_renderer,
                                   FALSE);
  pspp_sheet_view_column_set_cell_data_func (
    tree_column, ia->asst.fixed_renderer,
    input ? render_input_cell : render_output_cell, ia, NULL);
  pspp_sheet_view_column_set_fixed_width (tree_column, MAX (content_width,
                                                          header_width));
  pspp_sheet_view_column_set_resizable (tree_column, TRUE);

  free (name);

  return tree_column;
}

static PsppSheetView *
create_data_tree_view (bool input, GtkContainer *parent,
                       struct import_assistant *ia)
{
  PsppSheetView *tree_view;
  gint i;

  make_tree_view (ia, ia->first_line.skip_lines, &tree_view);
  pspp_sheet_selection_set_mode (pspp_sheet_view_get_selection (tree_view),
                               PSPP_SHEET_SELECTION_NONE);

  for (i = 0; i < ia->separators.column_cnt; i++)
    pspp_sheet_view_append_column (tree_view,
                                 make_data_column (ia, tree_view, input, i));

  g_object_set (G_OBJECT (tree_view), "has-tooltip", TRUE, (void *) NULL);
  g_signal_connect (tree_view, "query-tooltip",
                    G_CALLBACK (input ? on_query_input_tooltip
                                : on_query_output_tooltip), ia);

  gtk_container_add (parent, GTK_WIDGET (tree_view));
  gtk_widget_show (GTK_WIDGET (tree_view));

  return tree_view;
}

/* Increments the "watch cursor" level, setting the cursor for
   the assistant window to a watch face to indicate to the user
   that the ongoing operation may take some time. */
static void
push_watch_cursor (struct import_assistant *ia)
{
  if (++ia->asst.watch_cursor == 1)
    {
      GtkWidget *widget = GTK_WIDGET (ia->asst.assistant);
      GdkDisplay *display = gtk_widget_get_display (widget);
      GdkCursor *cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
      gdk_window_set_cursor (widget->window, cursor);
      gdk_cursor_unref (cursor);
      gdk_display_flush (display);
    }
}

/* Decrements the "watch cursor" level.  If the level reaches
   zero, the cursor is reset to its default shape. */
static void
pop_watch_cursor (struct import_assistant *ia)
{
  if (--ia->asst.watch_cursor == 0)
    {
      GtkWidget *widget = GTK_WIDGET (ia->asst.assistant);
      gdk_window_set_cursor (widget->window, NULL);
    }
}
