/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-source-etesync-account.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_SOURCE_ETESYNC_ACCOUNT_H
#define E_SOURCE_ETESYNC_ACCOUNT_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_ETESYNC_ACCOUNT \
	(e_source_etesync_account_get_type ())
#define E_SOURCE_ETESYNC_ACCOUNT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_ETESYNC_ACCOUNT, ESourceEteSyncAccount))
#define E_SOURCE_ETESYNC_ACCOUNT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_ETESYNC_ACCOUNT, ESourceEteSyncAccountClass))
#define E_IS_SOURCE_ETESYNC_ACCOUNT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_ETESYNC_ACCOUNT))
#define E_IS_SOURCE_ETESYNC_ACCOUNT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_ETESYNC_ACCOUNT))
#define E_SOURCE_ETESYNC_ACCOUNT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_ETESYNC_ACCOUNT, ESourceEteSyncAccountClass))

#define E_SOURCE_EXTENSION_ETESYNC_ACCOUNT "EteSync Account"

G_BEGIN_DECLS

typedef struct _ESourceEteSyncAccount ESourceEteSyncAccount;
typedef struct _ESourceEteSyncAccountClass ESourceEteSyncAccountClass;
typedef struct _ESourceEteSyncAccountPrivate ESourceEteSyncAccountPrivate;

struct _ESourceEteSyncAccount {
	ESourceExtension parent;
	ESourceEteSyncAccountPrivate *priv;
};

struct _ESourceEteSyncAccountClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_etesync_account_get_type
					(void) G_GNUC_CONST;
void		e_source_etesync_account_type_register
					(GTypeModule *type_module);
const gchar *	e_source_etesync_account_get_collection_stoken
					(ESourceEteSyncAccount *extension);
gchar *		e_source_etesync_account_dup_collection_stoken
					(ESourceEteSyncAccount *extension);
void		e_source_etesync_account_set_collection_stoken
					(ESourceEteSyncAccount *extension,
					 const gchar *collection_stoken);

G_END_DECLS

#endif /* E_SOURCE_ETESYNC_ACCOUNT_H */
