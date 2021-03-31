/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-connection.h - EteSync connection.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_ETESYNC_CONNECTION_H
#define E_ETESYNC_CONNECTION_H

#include <libedataserver/libedataserver.h>
#include <libedata-book/libedata-book.h>
#include <libedata-cal/libedata-cal.h>
#include "e-etesync-defines.h"
#include <etebase.h>

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
		e_etesync_connection_new	(ESource *collection);
ESourceAuthenticationResult
		e_etesync_connection_check_session_key_validation_sync
						(EEteSyncConnection *connection,
						 EtebaseErrorCode *out_etebase_error,
						 GError **error);
gboolean	e_etesync_connection_set_connection_from_sources
						(EEteSyncConnection *connection,
						 const ENamedParameters *credentials);
gboolean	e_etesync_connection_login_connection_sync
						(EEteSyncConnection *connection,
						 const gchar *username,
						 const gchar *password,
						 const gchar *server_url,
						 EtebaseErrorCode *out_etebase_error);
gboolean	e_etesync_connection_is_connected
						(EEteSyncConnection *connection);
gboolean	e_etesync_connection_reconnect_sync
						(EEteSyncConnection *connection,
						 ESourceAuthenticationResult *out_result,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_maybe_reconnect_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_collection_create_upload_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 const gchar *col_type,
						 const gchar *display_name,
						 const gchar *description,
						 const gchar *color,
						 EtebaseCollection **out_col_obj,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_collection_modify_upload_sync
						(EEteSyncConnection *connection,
						 EtebaseCollection *col_obj,
						 const gchar *display_name,
						 const gchar *description,
						 const gchar *color,
						 GError **error);
gboolean	e_etesync_connection_collection_delete_upload_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 EtebaseCollection *col_obj,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_list_existing_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 const EteSyncType type,
						 const EtebaseCollection *col_obj,
						 gchar **out_new_sync_tag,
						 GSList **out_existing_objects,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_get_changes_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 const EteSyncType type,
						 const gchar *last_sync_tag,
						 const EtebaseCollection *col_obj,
						 ECache *cache,
						 gchar **out_new_sync_tag,
						 GSList **out_created_objects, /* EBookMetaBackendInfo* or ECalMetaBackendInfo* */
						 GSList **out_modified_objects, /* EBookMetaBackendInfo* or ECalMetaBackendInfo* */
						 GSList **out_removed_objects,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_item_upload_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 const EtebaseCollection *col_obj,
						 const EteSyncAction action,
						 const gchar *content,
						 const gchar *uid,
						 const gchar *extra,
						 gchar **out_new_uid,
						 gchar **out_new_extra,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_batch_create_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 const EtebaseCollection *col_obj,
						 const EteSyncType type,
						 const gchar *const *content,
						 guint content_len,
						 GSList **out_batch_info,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_batch_modify_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 const EtebaseCollection *col_obj,
						 const EteSyncType type,
						 const gchar *const *content,
						 const gchar *const *data_uids,
						 guint content_len,
						 ECache *cache,
						 GSList **out_batch_info,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_connection_batch_delete_sync
						(EEteSyncConnection *connection,
						 EBackend *backend,
						 const EtebaseCollection *col_obj,
						 const EteSyncType type,
						 const gchar *const *content,
						 const gchar *const *data_uids,
						 guint content_len,
						 ECache *cache,
						 GSList **out_batch_info,
						 GCancellable *cancellable,
						 GError **error);

GType		e_etesync_connection_get_type	(void) G_GNUC_CONST;
const gchar *	e_etesync_connection_get_token	(EEteSyncConnection *connection);
const gchar *	e_etesync_connection_get_derived_key
						(EEteSyncConnection *connection);
const gchar *
		e_etesync_connection_get_session_key
						(EEteSyncConnection *connection);
void		e_etesync_connection_set_session_key
						(EEteSyncConnection *connection,
						 const gchar* session_key);
gboolean 	e_etesync_connection_get_requested_credentials
						(EEteSyncConnection *connection);
EtebaseClient *	e_etesync_connection_get_etebase_client
						(EEteSyncConnection *connection);
EtebaseAccount *
		e_etesync_connection_get_etebase_account
						(EEteSyncConnection *connection);
EtebaseCollectionManager *
		e_etesync_connection_get_collection_manager
						(EEteSyncConnection *connection);

G_END_DECLS

#endif /* E_ETESYNC_CONNECTION_H */
