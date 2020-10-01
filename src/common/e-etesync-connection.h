/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-connection.h - EteSync connection.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_ETESYNC_CONNECTION_H
#define E_ETESYNC_CONNECTION_H

#include <libedataserver/libedataserver.h>
#include <etesync.h>

/* Standard GObject macros */
#define E_TYPE_ETESYNC_CONNECTION \
	(e_etesync_connection_get_type ())
#define E_ETESYNC_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_ETESYNC_CONNECTION, EEteSyncConnection))
#define E_ETESYNC_CONNECTION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_ETESYNC_CONNECTION, EEteSyncConnectionClass))
#define E_IS_ETESYNC_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_ETESYNC_CONNECTION))
#define E_IS_ETESYNC_CONNECTION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_ETESYNC_CONNECTION))
#define E_ETESYNC_CONNECTION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_ETESYNC_CONNECTION, EEteSyncConnectionClass))

G_BEGIN_DECLS

typedef struct _EEteSyncConnection EEteSyncConnection;
typedef struct _EEteSyncConnectionClass EEteSyncConnectionClass;
typedef struct _EEteSyncConnectionPrivate EEteSyncConnectionPrivate;

struct _EEteSyncConnection {
	GObject parent;
	EEteSyncConnectionPrivate *priv;
};

struct _EEteSyncConnectionClass {
	GObjectClass parent_class;
};

EEteSyncConnection *
		e_etesync_connection_new	(void);
EEteSyncConnection *
		e_etesync_connection_new_from_user_url
						(const gchar* username,
						 const gchar* server_url);
ESourceAuthenticationResult
		e_etesync_connection_check_token_validation
						(EEteSyncConnection *connection);
gboolean	e_etesync_connection_init_userinfo
						(EEteSyncConnection *connection);
gboolean	e_etesync_connection_set_connection
						(EEteSyncConnection *connection,
						 const gchar *username,
						 const gchar *password,
						 const gchar *encryption_password,
						 const gchar *server_url,
						 EteSyncErrorCode *out_etesync_error);
ESourceAuthenticationResult
		e_etesync_connection_set_connection_from_sources
						(EEteSyncConnection *connection,
						 const ENamedParameters *credentials,
						 ESourceAuthentication *authentication_extension,
						 ESourceCollection *collection_extension);
gboolean	e_etesync_connection_needs_setting
						(EEteSyncConnection *connection,
						 const ENamedParameters *credentials,
						 ESourceAuthenticationResult *result);
void		e_etesync_connection_source_changed_cb
						(ESource *source,
						 ESource *collection,
						 gboolean is_contact);
EEteSyncConnection *
		e_etesync_connection_new_connection
						(ESource *collection);
gboolean	e_etesync_connection_get_journal_exists
						(EEteSyncConnection *connection,
						 const gchar *journal_id,
						 gboolean *is_read_only);
gboolean	e_etesync_connection_check_journal_changed_sync
						(EEteSyncConnection *connection,
						 const gchar *journal_id,
						 const gchar *last_sync_tag,
						 gint limit,
						 EteSyncEntry ***out_entries,
						 EteSyncCryptoManager **out_crypto_manager,
						 EteSyncEntryManager **out_entry_manager);
gboolean	e_etesync_connection_entry_write_action
						(EEteSyncConnection *connection,
						 const gchar *journal_id,
						 const gchar *last_sync_tag,
						 const gchar *content,
						 const gchar *action,
						 gchar **out_last_sync_tag,
						 EteSyncErrorCode *out_etesync_error);
gboolean	e_etesync_connection_multiple_entry_write_action
						(EEteSyncConnection *connection,
						 const gchar *journal_id,
						 const gchar * const * content,
						 guint length,
						 const gchar *action,
						 gchar **out_last_sync_tag,
						 EteSyncErrorCode *out_etesync_error);
gboolean	e_etesync_connection_write_journal_action
						(EEteSyncConnection *connection,
						 const gchar *action,
						 const gchar *journal_uid,
						 const gchar *journal_type,
						 const gchar *display_name,
						 const gchar *description,
						 gint32 color,
						 EteSyncJournal **out_journal);

GType		e_etesync_connection_get_type	(void) G_GNUC_CONST;
const gchar *	e_etesync_connection_get_token	(EEteSyncConnection *connection);
const gchar *	e_etesync_connection_get_derived_key
						(EEteSyncConnection *connection);
const gchar *	e_etesync_connection_get_server_url
						(EEteSyncConnection *connection);
const gchar *	e_etesync_connection_get_username
						(EEteSyncConnection *connection);
gboolean 	e_etesync_connection_get_requested_credentials
						(EEteSyncConnection *connection);
EteSync *	e_etesync_connection_get_etesync
						(EEteSyncConnection *connection);
EteSyncJournalManager *
		e_etesync_connection_get_journal_manager
						(EEteSyncConnection *connection);
EteSyncAsymmetricKeyPair *
		e_etesync_connection_get_keypair
						(EEteSyncConnection *connection);

G_END_DECLS

#endif /* E_ETESYNC_CONNECTION_H */
