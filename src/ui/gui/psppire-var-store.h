/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2011, 2012  Free Software Foundation

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

#ifndef __PSPPIRE_VAR_STORE_H__
#define __PSPPIRE_VAR_STORE_H__

#include "psppire-dict.h"
#include <gdk/gdk.h>

G_BEGIN_DECLS

/* PSPPIRE variable store. */
#define GTK_TYPE_VAR_STORE	       (psppire_var_store_get_type ())

#define PSPPIRE_VAR_STORE(obj)	       (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
								    GTK_TYPE_VAR_STORE, PsppireVarStore))

#define PSPPIRE_VAR_STORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), \
								 GTK_TYPE_VAR_STORE, \
                                                                 PsppireVarStoreClass))

#define PSPPIRE_IS_VAR_STORE(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_VAR_STORE))

#define PSPPIRE_IS_VAR_STORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_VAR_STORE))

#define PSPPIRE_VAR_STORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
								   GTK_TYPE_VAR_STORE, \
								   PsppireVarStoreClass))

typedef struct _PsppireVarStore       PsppireVarStore;
typedef struct _PsppireVarStoreClass  PsppireVarStoreClass;

struct _PsppireVarStore
{
  GObject parent;

  /*< private >*/
  PsppireDict *dictionary;
};

struct _PsppireVarStoreClass
{
  GObjectClass parent_class;

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};


GType         psppire_var_store_get_type         (void) G_GNUC_CONST;
PsppireVarStore *psppire_var_store_new              (PsppireDict *dict);
struct variable * psppire_var_store_get_var (PsppireVarStore *store, glong row);

/* Return the number of variables */
gint psppire_var_store_get_var_cnt (PsppireVarStore      *var_store);

G_END_DECLS

#endif /* __PSPPIRE_VAR_STORE_H__ */
