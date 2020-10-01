/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-source-credentials-provider-impl-etesync.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC_H
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC \
	(e_source_credentials_provider_impl_etesync_get_type ())
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC, ESourceCredentialsProviderImplEteSync))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC, ESourceCredentialsProviderImplEteSyncClass))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC, ESourceCredentialsProviderImplEteSyncClass))

G_BEGIN_DECLS

typedef struct _ESourceCredentialsProviderImplEteSync ESourceCredentialsProviderImplEteSync;
typedef struct _ESourceCredentialsProviderImplEteSyncClass ESourceCredentialsProviderImplEteSyncClass;
typedef struct _ESourceCredentialsProviderImplEteSyncPrivate ESourceCredentialsProviderImplEteSyncPrivate;

struct _ESourceCredentialsProviderImplEteSync {
	ESourceCredentialsProviderImpl parent;
	ESourceCredentialsProviderImplEteSyncPrivate *priv;
};

struct _ESourceCredentialsProviderImplEteSyncClass {
	ESourceCredentialsProviderImplClass parent_class;
};

GType		e_source_credentials_provider_impl_etesync_get_type
						(void);
void		e_source_credentials_provider_impl_etesync_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC_H */
