/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-book-config-etesync.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_BOOK_CONFIG_ETESYNC_H
#define E_BOOK_CONFIG_ETESYNC_H

#include <e-util/e-util.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_CONFIG_ETESYNC \
	(e_book_config_etesync_get_type ())
#define E_BOOK_CONFIG_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_CONFIG_ETESYNC, EBookConfigEteSync))
#define E_BOOK_CONFIG_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_CONFIG_ETESYNC, EBookConfigEteSyncClass))
#define E_IS_BOOK_CONFIG_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_CONFIG_ETESYNC))
#define E_IS_BOOK_CONFIG_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_CONFIG_ETESYNC))
#define E_BOOK_CONFIG_ETESYNC_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_CONFIG_ETESYNC, EBookConfigEteSyncClass))

G_BEGIN_DECLS

typedef struct _EBookConfigEteSync EBookConfigEteSync;
typedef struct _EBookConfigEteSyncClass EBookConfigEteSyncClass;
typedef struct _EBookConfigEteSyncPrivate EBookConfigEteSyncPrivate;

struct _EBookConfigEteSync {
	ESourceConfigBackend parent;
	EBookConfigEteSyncPrivate *priv;
};

struct _EBookConfigEteSyncClass {
	ESourceConfigBackendClass parent_class;
};

GType		e_book_config_etesync_get_type	(void) G_GNUC_CONST;
void		e_book_config_etesync_type_register
						(GTypeModule *type_module);
G_END_DECLS

#endif /* E_BOOK_CONFIG_ETESYNC_H */
