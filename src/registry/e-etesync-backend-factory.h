/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-etesync-backend-factory.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_ETESYNC_BACKEND_FACTORY_H
#define E_ETESYNC_BACKEND_FACTORY_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_ETESYNC_BACKEND_FACTORY \
	(e_etesync_backend_factory_get_type ())
#define E_ETESYNC_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_ETESYNC_BACKEND_FACTORY, EEteSyncBackendFactory))
#define E_ETESYNC_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_ETESYNC_BACKEND_FACTORY, EEteSyncBackendFactoryClass))
#define E_IS_ETESYNC_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_ETESYNC_BACKEND_FACTORY))
#define E_IS_ETESYNC_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_ETESYNC_BACKEND_FACTORY))
#define E_ETESYNC_BACKEND_FACTORY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_ETESYNC_BACKEND_FACTORY, EEteSyncBackendFactoryClass))

G_BEGIN_DECLS

typedef struct _EEteSyncBackendFactory EEteSyncBackendFactory;
typedef struct _EEteSyncBackendFactoryClass EEteSyncBackendFactoryClass;
typedef struct _EEteSyncBackendFactoryPrivate EEteSyncBackendFactoryPrivate;

struct _EEteSyncBackendFactory {
	ECollectionBackendFactory parent;
	EEteSyncBackendFactoryPrivate *priv;
};

struct _EEteSyncBackendFactoryClass {
	ECollectionBackendFactoryClass parent_class;
};

GType		e_etesync_backend_factory_get_type
						(void) G_GNUC_CONST;
void		e_etesync_backend_factory_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_ETESYNC_BACKEND_FACTORY_H */
