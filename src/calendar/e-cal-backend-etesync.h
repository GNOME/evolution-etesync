/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cal-backend-etesync.h - EteSync calendar backend.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_BACKEND_ETESYNC_H
#define E_CAL_BACKEND_ETESYNC_H

#include <libedata-cal/libedata-cal.h>

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_ETESYNC \
	(e_cal_backend_etesync_get_type ())
#define E_CAL_BACKEND_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_ETESYNC, ECalBackendEteSync))
#define E_CAL_BACKEND_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_ETESYNC, ECalBackendEteSyncClass))
#define E_IS_CAL_BACKEND_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_ETESYNC))
#define E_IS_CAL_BACKEND_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_ETESYNC))
#define E_CAL_BACKEND_ETESYNC_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND_ETESYNC, ECalBackendEteSyncClass))

G_BEGIN_DECLS

typedef struct _ECalBackendEteSync ECalBackendEteSync;
typedef struct _ECalBackendEteSyncClass ECalBackendEteSyncClass;
typedef struct _ECalBackendEteSyncPrivate ECalBackendEteSyncPrivate;

struct _ECalBackendEteSync {
	ECalMetaBackend parent;
	ECalBackendEteSyncPrivate *priv;
};

struct _ECalBackendEteSyncClass {
	ECalMetaBackendClass parent_class;
};

GType		e_cal_backend_etesync_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_ETESYNC_H */
