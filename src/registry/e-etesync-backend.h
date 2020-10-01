/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-etesync-backend.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_ETESYNC_BACKEND_H
#define E_ETESYNC_BACKEND_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_ETESYNC_BACKEND \
	(e_etesync_backend_get_type ())
#define E_ETESYNC_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_ETESYNC_BACKEND, EEteSyncBackend))
#define E_ETESYNC_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_ETESYNC_BACKEND, EEteSyncBackendClass))
#define E_IS_ETESYNC_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_ETESYNC_BACKEND))
#define E_IS_ETESYNC_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_ETESYNC_BACKEND))
#define E_ETESYNC_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_ETESYNC_BACKEND, EEteSyncBackendClass))

G_BEGIN_DECLS

typedef struct _EEteSyncBackend EEteSyncBackend;
typedef struct _EEteSyncBackendClass EEteSyncBackendClass;
typedef struct _EEteSyncBackendPrivate EEteSyncBackendPrivate;

struct _EEteSyncBackend {
	ECollectionBackend parent;
	EEteSyncBackendPrivate *priv;
};

struct _EEteSyncBackendClass {
	ECollectionBackendClass parent_class;
};

GType		e_etesync_backend_get_type	(void) G_GNUC_CONST;

G_END_DECLS

#endif /* E_ETESYNC_BACKEND_H */
