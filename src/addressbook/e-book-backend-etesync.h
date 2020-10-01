/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-backend-etesync.h - EteSync contact backend.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_BOOK_BACKEND_ETESYNC_H
#define E_BOOK_BACKEND_ETESYNC_H

#include <libedata-book/libedata-book.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_ETESYNC \
	(e_book_backend_etesync_get_type ())
#define E_BOOK_BACKEND_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_ETESYNC, EBookBackendEteSync))
#define E_BOOK_BACKEND_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_ETESYNC, EBookBackendEteSyncClass))
#define E_IS_BOOK_BACKEND_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_ETESYNC))
#define E_IS_BOOK_BACKEND_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_ETESYNC))
#define E_BOOK_BACKEND_ETESYNC_GET_CLASS(cls) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_ETESYNC, EBookBackendEteSyncClass))

G_BEGIN_DECLS

typedef struct _EBookBackendEteSync EBookBackendEteSync;
typedef struct _EBookBackendEteSyncClass EBookBackendEteSyncClass;
typedef struct _EBookBackendEteSyncPrivate EBookBackendEteSyncPrivate;

struct _EBookBackendEteSync {
	EBookMetaBackend parent;
	EBookBackendEteSyncPrivate *priv;
};

struct _EBookBackendEteSyncClass {
	EBookMetaBackendClass parent_class;
};

GType		e_book_backend_etesync_get_type	(void);

G_END_DECLS

#endif /* E_BOOK_BACKEND_ETESYNC_H */
