/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-connection.c - EteSync connection.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>
#include "e-etesync-connection.h"
#include "e-etesync-utils.h"
#include "common/e-source-etesync.h"
#include "common/e-etesync-service.h"

/* Stack-allocated buffer size for items read, to avoid often allocations */
#define BUFF_SIZE 2048

static GMutex connecting;
static GHashTable *loaded_connections_permissions = NULL; /* gchar* ~> EEteSyncConnection* */

struct _EEteSyncConnectionPrivate {
	EtebaseClient *etebase_client;
	EtebaseAccount *etebase_account;
	EtebaseCollectionManager *col_mgr;
	gchar *session_key;
	ESource *collection_source;

	/* Hash key for the loaded_connections_permissions table. */
	gchar *hash_key;
	GRecMutex connection_lock;
	gboolean requested_credentials;
};

G_DEFINE_TYPE_WITH_PRIVATE (EEteSyncConnection, e_etesync_connection, G_TYPE_OBJECT)

static void
e_etesync_connection_clear (EEteSyncConnection *connection)
{
	g_rec_mutex_lock (&connection->priv->connection_lock);

	if (connection->priv->col_mgr)
		g_clear_pointer (&connection->priv->col_mgr, etebase_collection_manager_destroy);

	if (connection->priv->etebase_client)
		g_clear_pointer (&connection->priv->etebase_client, etebase_client_destroy);

	if (connection->priv->etebase_account)
		g_clear_pointer (&connection->priv->etebase_account, etebase_account_destroy);

	g_clear_pointer (&connection->priv->session_key, g_free);

	g_rec_mutex_unlock (&connection->priv->connection_lock);
}

/* Returns either a new connection object or an already existing one with the same hash_key */
EEteSyncConnection *
e_etesync_connection_new (ESource *collection_source)
{
	EEteSyncConnection *connection;
	gchar *hash_key;
	const gchar *username = NULL, *server_url = NULL;

	if (collection_source)
		g_return_val_if_fail (E_IS_SOURCE (collection_source), NULL);
	else
		return g_object_new (E_TYPE_ETESYNC_CONNECTION, NULL);

	if (e_source_has_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION)) {
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);
		server_url = e_source_collection_get_calendar_url (collection_extension);
	}

	if (e_source_has_extension (collection_source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *authentication_extension;

		authentication_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_AUTHENTICATION);
		username = e_source_authentication_get_user (authentication_extension);
	}

	g_return_val_if_fail (username != NULL, NULL);
	g_return_val_if_fail (server_url != NULL, NULL);

	/* Trying to find if an object has been created before, then reference that if not return a new one */
	hash_key = g_strdup_printf ("%s@%s", username, server_url);

	g_mutex_lock (&connecting);
	/* search the connection in our hash table */
	if (loaded_connections_permissions != NULL) {
		connection = g_hash_table_lookup (
			loaded_connections_permissions, hash_key);

		if (E_IS_ETESYNC_CONNECTION (connection)) {
			g_object_ref (connection);

			g_free (hash_key);

			g_mutex_unlock (&connecting);
			return connection;
		}
	}

	/* not found, so create a new connection */
	connection = g_object_new (E_TYPE_ETESYNC_CONNECTION, NULL);
	connection->priv->hash_key = hash_key;  /* takes ownership */
	connection->priv->collection_source = g_object_ref (collection_source);

	/* add the connection to the loaded_connections_permissions hash table */
	if (!loaded_connections_permissions)
		loaded_connections_permissions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	g_hash_table_insert (
		loaded_connections_permissions,
		g_strdup (connection->priv->hash_key), connection);

	g_mutex_unlock (&connecting);
	return connection;
}

/*
 * Should return if the connection is accepted
 * or token is invalid or issue with server
 */
ESourceAuthenticationResult
e_etesync_connection_check_session_key_validation_sync (EEteSyncConnection *connection,
							EtebaseErrorCode *out_etebase_error,
							GError **error)
{
	EtebaseFetchOptions *fetch_options;
	EtebaseCollectionListResponse *col_list;
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	GError *local_error = NULL;

	g_return_val_if_fail (connection != NULL ,E_SOURCE_AUTHENTICATION_ERROR);
	g_return_val_if_fail (connection->priv->etebase_account != NULL ,E_SOURCE_AUTHENTICATION_ERROR);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	fetch_options = etebase_fetch_options_new ();
	etebase_fetch_options_set_prefetch(fetch_options, ETEBASE_PREFETCH_OPTION_MEDIUM);
	etebase_fetch_options_set_limit (fetch_options, 1);

	col_list = etebase_collection_manager_list_multi (connection->priv->col_mgr, e_etesync_util_get_collection_supported_types (), EETESYNC_UTILS_SUPPORTED_TYPES_SIZE, fetch_options);

	if (!col_list) {
		EtebaseErrorCode etebase_error = etebase_error_get_code ();

		if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED)
			result = E_SOURCE_AUTHENTICATION_REJECTED;
		else
			result = E_SOURCE_AUTHENTICATION_ERROR;

		e_etesync_utils_set_io_gerror (etebase_error_get_code (), etebase_error_get_message (), &local_error);
	} else {
		etebase_collection_list_response_destroy (col_list);
	}
	etebase_fetch_options_destroy (fetch_options);

	if (local_error) {
		g_propagate_error (error, local_error);

		if (out_etebase_error)
			*out_etebase_error = etebase_error_get_code ();
	}

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return result;
}

gboolean
e_etesync_connection_set_connection_from_sources (EEteSyncConnection *connection,
						  const ENamedParameters *credentials)
{
	const gchar *server_url , *session_key;
	gboolean success = TRUE;
	ESourceCollection *collection_extension;

	g_return_val_if_fail (connection != NULL ,FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	collection_extension = e_source_get_extension (connection->priv->collection_source, E_SOURCE_EXTENSION_COLLECTION);
	e_etesync_connection_clear (connection);

	/* 1) get_server from ESourceAuthentication as it was saved when the user was entering credentials in the dialog first time.
	      Set the etebase_client then for this backend so as long as the backend is alive, we don't have to do this process again */
	server_url = e_source_collection_get_calendar_url (collection_extension);
	connection->priv->etebase_client = etebase_client_new (PACKAGE "/" VERSION, server_url);

	/* problem with the server_url */
	if (!connection->priv->etebase_client) {
		g_rec_mutex_unlock (&connection->priv->connection_lock);
		return FALSE;
	}

	/* 2) get stored sessions from Credentials (Should be stored there) */
	session_key = e_named_parameters_get (credentials, E_ETESYNC_CREDENTIAL_SESSION_KEY);

	/* 3) check if the session key is NULL, if so that may mean that the password is wrong
	      or changed, or simply the session key is not stored. */
	if (!session_key) {
		g_rec_mutex_unlock (&connection->priv->connection_lock);
		return FALSE;
	}

	connection->priv->session_key = g_strdup (session_key);
	connection->priv->etebase_account = etebase_account_restore (connection->priv->etebase_client, session_key, NULL, 0);
	connection->priv->col_mgr = etebase_account_get_collection_manager (connection->priv->etebase_account);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

gboolean
e_etesync_connection_login_connection_sync (EEteSyncConnection *connection,
					    const gchar *username,
					    const gchar *password,
					    const gchar *server_url,
					    EtebaseErrorCode *out_etebase_error)
{
	EtebaseErrorCode local_etebase_error = ETEBASE_ERROR_CODE_NO_ERROR;
	EtebaseClient *etebase_client;
	gboolean success = TRUE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (username, FALSE);
	g_return_val_if_fail (password, FALSE);
	g_return_val_if_fail (server_url && *server_url, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	e_etesync_connection_clear (connection);

	etebase_client = etebase_client_new (PACKAGE "/" VERSION, server_url);

	if (etebase_client) {
		connection->priv->etebase_client = etebase_client;
		connection->priv->etebase_account = etebase_account_login (etebase_client, username, password);

		if (connection->priv->etebase_account) {
			connection->priv->col_mgr = etebase_account_get_collection_manager (connection->priv->etebase_account);
			connection->priv->session_key = etebase_account_save (connection->priv->etebase_account, NULL, 0);
		} else {
			local_etebase_error = etebase_error_get_code ();
			success = FALSE;
		}
	} else {
		/* Bad server name */
		local_etebase_error = etebase_error_get_code ();
		success = FALSE;
	}

	if (out_etebase_error)
		*out_etebase_error = local_etebase_error;

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

gboolean
e_etesync_connection_is_connected (EEteSyncConnection *connection)
{
	gboolean success;

	if (!connection)
		return FALSE;

	g_rec_mutex_lock (&connection->priv->connection_lock);

	success = (connection->priv->etebase_client &&
		   connection->priv->etebase_account &&
		   connection->priv->col_mgr) ? TRUE : FALSE;

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}


/* Fetches a new token and stores it in the keyring,
   returns a new ENamedParameters with the new session key */
static gboolean
e_etesync_connection_refresh_token_sync (EEteSyncConnection *connection,
					 ESource *collection,
					 ENamedParameters *credentials,
					 EtebaseErrorCode *etebase_error,
					 GError **error)
{
	EtebaseAccount *etebase_account;
	gboolean success;

	etebase_account = e_etesync_connection_get_etebase_account (connection);
	success = !etebase_account_fetch_token (etebase_account);

	if (success) {
		gchar *new_session_key, *label;
		const gchar *collection_uid;
		ESourceAuthentication *auth_extension;
		gboolean permanently;

		new_session_key = etebase_account_save (etebase_account, NULL, 0);
		label = e_source_dup_secret_label (collection);
		auth_extension = e_source_get_extension (collection, E_SOURCE_EXTENSION_AUTHENTICATION);
		permanently = e_source_authentication_get_remember_password (auth_extension);
		collection_uid = e_source_get_uid (collection);

		e_named_parameters_clear (credentials);
		e_named_parameters_set (credentials,
			E_ETESYNC_CREDENTIAL_SESSION_KEY, new_session_key);

		e_etesync_service_store_credentials_sync (collection_uid,
							  label,
							  credentials,
							  permanently,
							  NULL, NULL);

		g_free (new_session_key);
		g_free (label);
	} else {
		EtebaseErrorCode local_etebase_error = etebase_error_get_code ();

		if (error)
			e_etesync_utils_set_io_gerror (local_etebase_error, etebase_error_get_message (), error);
		if (etebase_error)
			*etebase_error = local_etebase_error;
	}

	return success;
}

/* Checks if token is valid if not it refreshes the token,
   and it sets the connection object with the latest stored-session key */
gboolean
e_etesync_connection_reconnect_sync (EEteSyncConnection *connection,
				     ESourceAuthenticationResult *out_result,
				     GCancellable *cancellable,
				     GError **error)
{
	ENamedParameters *credentials = NULL;
	const gchar *collection_uid;
	ESourceAuthenticationResult local_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	ESource *collection;
	gboolean success = FALSE;

	g_return_val_if_fail (connection != NULL, FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_rec_mutex_lock (&connection->priv->connection_lock);

	collection = connection->priv->collection_source;
	collection_uid = e_source_get_uid (collection);
	e_etesync_service_lookup_credentials_sync (collection_uid, &credentials, NULL, NULL);

	if (e_etesync_connection_is_connected (connection)) {
		const gchar *session_key = e_named_parameters_get (credentials, E_ETESYNC_CREDENTIAL_SESSION_KEY);

		if (session_key) {
			if (g_strcmp0 (session_key, connection->priv->session_key) == 0) {
				if (e_etesync_connection_check_session_key_validation_sync (connection, NULL, error) == E_SOURCE_AUTHENTICATION_REJECTED) {
					EtebaseErrorCode etebase_error;

					g_clear_error (error);

					if (e_etesync_connection_refresh_token_sync (connection, collection, credentials, &etebase_error, error)) {
						local_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
					} else {
						if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED)
							local_result = E_SOURCE_AUTHENTICATION_REJECTED;
						else
							local_result = E_SOURCE_AUTHENTICATION_ERROR;
					}
				}
			}
		} else {
			local_result = E_SOURCE_AUTHENTICATION_ERROR;
		}
	} else {
		if (!credentials || !e_named_parameters_exists (credentials, E_ETESYNC_CREDENTIAL_SESSION_KEY))
			local_result = E_SOURCE_AUTHENTICATION_REJECTED;
	}

	/* Set connection from collection source */
	if (local_result == E_SOURCE_AUTHENTICATION_ACCEPTED)
		success = e_etesync_connection_set_connection_from_sources (connection, credentials);

	if (out_result)
		*out_result = local_result;

	e_named_parameters_free (credentials);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

/* Calls connection,reconnect, and requests the credentials dialog if needed */
gboolean
e_etesync_connection_maybe_reconnect_sync (EEteSyncConnection *connection,
					   EBackend *backend,
					   GCancellable *cancellable,
					   GError **error)
{
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	gboolean success = FALSE;

	success = e_etesync_connection_reconnect_sync (connection, &result, cancellable, error);

	if (result == E_SOURCE_AUTHENTICATION_REJECTED) {
		e_backend_schedule_credentials_required (backend,
			E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}

	return success;
}

/* ----- Collection create/modify/delete then push to server functions ------ */

gboolean
e_etesync_connection_collection_create_upload_sync (EEteSyncConnection *connection,
						    EBackend *backend,
						    const gchar *col_type,
						    const gchar *display_name,
						    const gchar *description,
						    const gchar *color,
						    EtebaseCollection **out_col_obj,
						    GCancellable *cancellable,
						    GError **error)
{
	EtebaseCollection *col_obj;
	EtebaseItemMetadata *item_metadata;
	gboolean success = TRUE;
	time_t now;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);
	g_return_val_if_fail (col_type != NULL, FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_rec_mutex_lock (&connection->priv->connection_lock);

	item_metadata = etebase_item_metadata_new ();
	etebase_item_metadata_set_name (item_metadata, display_name);
	etebase_item_metadata_set_description (item_metadata, description);
	etebase_item_metadata_set_color (item_metadata, color);
	e_etesync_utils_get_time_now (&now);
	etebase_item_metadata_set_mtime (item_metadata, &now);

	col_obj = etebase_collection_manager_create (connection->priv->col_mgr, col_type, item_metadata, "", 0);
	success = !etebase_collection_manager_upload (connection->priv->col_mgr, col_obj, NULL);

	if (!success &&
	    etebase_error_get_code () == ETEBASE_ERROR_CODE_UNAUTHORIZED &&
	    e_etesync_connection_maybe_reconnect_sync (connection, backend, cancellable, error)) {

		success = !etebase_collection_manager_upload (connection->priv->col_mgr, col_obj, NULL);
	}

	if (!success)
		e_etesync_utils_set_io_gerror (etebase_error_get_code (), etebase_error_get_message (), error);

	etebase_item_metadata_destroy (item_metadata);

	if (out_col_obj && success)
		*out_col_obj = col_obj;
	else
		etebase_collection_destroy (col_obj);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

gboolean
e_etesync_connection_collection_modify_upload_sync (EEteSyncConnection *connection,
						    EtebaseCollection *col_obj,
						    const gchar *display_name,
						    const gchar *description,
						    const gchar *color,
						    GError **error)
{
	EtebaseItemMetadata *item_metadata;
	gboolean success = TRUE;
	time_t now;
	GError *local_error = NULL;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (col_obj != NULL, FALSE);
	g_return_val_if_fail (display_name && *display_name, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	item_metadata = etebase_collection_get_meta (col_obj);
	etebase_item_metadata_set_name (item_metadata, display_name);
	etebase_item_metadata_set_description (item_metadata, description);
	etebase_item_metadata_set_color (item_metadata, color);
	e_etesync_utils_get_time_now (&now);
	etebase_item_metadata_set_mtime (item_metadata, &now);

	etebase_collection_set_meta (col_obj, item_metadata);
	success = !etebase_collection_manager_upload (connection->priv->col_mgr, col_obj, NULL);

	if (!success &&
	    etebase_error_get_code () == ETEBASE_ERROR_CODE_UNAUTHORIZED &&
	    e_etesync_connection_reconnect_sync (connection, NULL, NULL, &local_error)) {

		success = !etebase_collection_manager_upload (connection->priv->col_mgr, col_obj, NULL);
	}

	if (!success)
		e_etesync_utils_set_io_gerror (etebase_error_get_code (), etebase_error_get_message (), &local_error);

	if (local_error)
		g_propagate_error (error, local_error);

	etebase_item_metadata_destroy (item_metadata);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

gboolean
e_etesync_connection_collection_delete_upload_sync (EEteSyncConnection *connection,
						    EBackend *backend,
						    EtebaseCollection *col_obj,
						    GCancellable *cancellable,
						    GError **error)
{
	EtebaseItemMetadata *item_metadata;
	gboolean success = TRUE;
	time_t now;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (col_obj != NULL, FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_rec_mutex_lock (&connection->priv->connection_lock);

	item_metadata = etebase_collection_get_meta (col_obj);
	e_etesync_utils_get_time_now (&now);
	etebase_item_metadata_set_mtime (item_metadata, &now);

	etebase_collection_set_meta (col_obj, item_metadata);
	etebase_collection_delete (col_obj);
	success = !etebase_collection_manager_upload (connection->priv->col_mgr, col_obj, NULL);

	if (!success &&
	    etebase_error_get_code () == ETEBASE_ERROR_CODE_UNAUTHORIZED &&
	    e_etesync_connection_maybe_reconnect_sync (connection, backend, cancellable, error)) {

		success = !etebase_collection_manager_upload (connection->priv->col_mgr, col_obj, NULL);
	}

	if (!success)
		e_etesync_utils_set_io_gerror (etebase_error_get_code (), etebase_error_get_message (), error);

	etebase_item_metadata_destroy (item_metadata);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

/* ------------------- Book and calendar common function ------------------- */
static gboolean
e_etesync_connection_backend_is_for_memos (EBackend *backend)
{
	return E_IS_CAL_BACKEND (backend) && e_cal_backend_get_kind (E_CAL_BACKEND (backend)) == I_CAL_VJOURNAL_COMPONENT;
}

static gchar *
e_etesync_connection_notes_new_ical_string (time_t creation_date,
					    time_t last_modified,
					    const gchar *uid,
					    const gchar *revision,
					    const gchar *summary,
					    const gchar *description)
{
	ICalComponent *icomp;
	ICalTime *itt;
	time_t tt;
	gchar *ical_str;

	icomp = i_cal_component_new_vjournal ();

	if (creation_date > 0)
		tt = creation_date;
	else if (last_modified > 0)
		tt = last_modified;
	else
		tt = time (NULL);

	itt = i_cal_time_new_from_timet_with_zone (tt, FALSE, i_cal_timezone_get_utc_timezone ());
	i_cal_component_take_property (icomp, i_cal_property_new_created (itt));
	g_object_unref (itt);

	if (last_modified > 0)
		tt = last_modified;
	else
		tt = time (NULL);

	itt = i_cal_time_new_from_timet_with_zone (tt, FALSE, i_cal_timezone_get_utc_timezone ());
	i_cal_component_take_property (icomp, i_cal_property_new_lastmodified (itt));
	g_object_unref (itt);

	i_cal_component_set_uid (icomp, uid);

	if (summary && g_str_has_suffix (summary, ".txt")) {
		gchar *tmp;

		tmp = g_strndup (summary, strlen (summary) - 4);
		i_cal_component_set_summary (icomp, tmp);
		g_free (tmp);
	} else if (summary && *summary) {
		i_cal_component_set_summary (icomp, summary);
	}

	if (description && *description)
		i_cal_component_set_description (icomp, description);

	ical_str = i_cal_component_as_ical_string (icomp);

	g_object_unref (icomp);

	return ical_str;
}

static gboolean
e_etesync_connection_chunk_itemlist_fetch_sync (EtebaseItemManager *item_mgr,
						const gchar *stoken,
						gintptr fetch_limit,
						EtebaseItemListResponse **out_item_list,
						guintptr *out_item_data_len,
						gchar **out_stoken,
						gboolean *out_done)
{
	EtebaseFetchOptions *fetch_options = etebase_fetch_options_new ();

	etebase_fetch_options_set_stoken (fetch_options, stoken);
	etebase_fetch_options_set_limit (fetch_options, fetch_limit);

	*out_item_list = etebase_item_manager_list (item_mgr, fetch_options);

	if (!*out_item_list) {
		etebase_fetch_options_destroy (fetch_options);
		return FALSE;
	}

	g_free (*out_stoken);
	*out_stoken = g_strdup (etebase_item_list_response_get_stoken (*out_item_list));
	*out_done = etebase_item_list_response_is_done (*out_item_list);
	*out_item_data_len = etebase_item_list_response_get_data_length (*out_item_list);

	etebase_fetch_options_destroy (fetch_options);

	return TRUE;
}

gboolean
e_etesync_connection_list_existing_sync (EEteSyncConnection *connection,
					 EBackend *backend,
					 const EteSyncType type,
					 const EtebaseCollection *col_obj,
					 gchar **out_new_sync_tag,
					 GSList **out_existing_objects,
					 GCancellable *cancellable,
					 GError **error)
{
	EtebaseItemManager *item_mgr;
	gchar *stoken = NULL;
	gboolean done = FALSE;
	gboolean success = TRUE;
	gboolean is_memo;

	*out_existing_objects = NULL;
	*out_new_sync_tag = NULL;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (!col_obj) {
		return FALSE;
	}

	is_memo = e_etesync_connection_backend_is_for_memos (backend);
	item_mgr = etebase_collection_manager_get_item_manager (connection->priv->col_mgr, col_obj);

	while (!done) {
		EtebaseItem **items_data;
		EtebaseItemListResponse *item_list;
		guintptr items_data_len, item_iter;

		if (e_etesync_connection_chunk_itemlist_fetch_sync (item_mgr, stoken, E_ETESYNC_ITEM_FETCH_LIMIT, &item_list, &items_data_len, &stoken, &done)) {

			items_data = g_alloca (sizeof (EtebaseItem *) * E_ETESYNC_ITEM_FETCH_LIMIT);
			etebase_item_list_response_get_data (item_list, (const EtebaseItem **) items_data);

			/* At this point, items_data are not empty, then we should loop on items and add each
			   one to the hashtable as meta backend info for each contact */
			if (items_data && *items_data) {
				for (item_iter = 0; item_iter < items_data_len; item_iter++) {
					const EtebaseItem *item = items_data[item_iter];

					/* check action add, change or delete */
					if (!etebase_item_is_deleted (item)) {
						gchar *content = NULL, *data_uid = NULL, *revision = NULL, *item_cache_b64, buf[BUFF_SIZE];
						gintptr content_len;

						content_len = etebase_item_get_content (item, buf, sizeof (buf));

						if (content_len < 0) {
							break;
						} else if (content_len >= sizeof (buf)) {
							content = g_slice_alloc0 (content_len + 1);
							etebase_item_get_content (item, content, content_len);
							content[content_len] = 0;
						} else {
							buf[content_len] = 0;
						}

						item_cache_b64 = e_etesync_utils_etebase_item_to_base64 (item, item_mgr);

						if (type == E_ETESYNC_ADDRESSBOOK) {
							EBookMetaBackendInfo *nfo;

							/* create EBookMetaBackendInfo * to be stored in GSList, data_uid is contact uid */
							e_etesync_utils_get_contact_uid_revision (content ? content : buf, &data_uid, &revision);
							nfo = e_book_meta_backend_info_new (data_uid, revision, content ? content : buf, item_cache_b64);
							*out_existing_objects = g_slist_append (*out_existing_objects, nfo);
						} else if (type == E_ETESYNC_CALENDAR) {
							ECalMetaBackendInfo *nfo;

							if (is_memo) {
								EtebaseItemMetadata *item_meta;
								const gchar *summary;
								gchar *ical_str;
								time_t now;

								item_meta = etebase_item_get_meta (item);
								summary = etebase_item_metadata_get_name (item_meta);
								data_uid = g_strdup (etebase_item_get_uid (item));
								e_etesync_utils_get_time_now (&now);

								/* change plain text to a icomp vjournal object */
								ical_str = e_etesync_connection_notes_new_ical_string ((time_t) now, (time_t) now, data_uid, NULL, summary, content ? content : buf);
								nfo = e_cal_meta_backend_info_new (data_uid, NULL, ical_str, item_cache_b64);

								g_free (ical_str);
								etebase_item_metadata_destroy (item_meta);
							} else {
								/* create ECalMetaBackendInfo * to be stored in GSList, data_uid is component uid */
								e_etesync_utils_get_component_uid_revision (content ? content : buf, &data_uid, &revision);
								nfo = e_cal_meta_backend_info_new (data_uid, revision, content ? content : buf, item_cache_b64);
							}

							*out_existing_objects = g_slist_prepend (*out_existing_objects, nfo);
						}

						g_free (data_uid);
						g_free (revision);
						g_slice_free1 (content_len + 1, content);
						g_free (item_cache_b64);
					}
				}
			}
			etebase_item_list_response_destroy (item_list);

		} else {
			EtebaseErrorCode etebase_error;

			etebase_error = etebase_error_get_code ();
			success = FALSE;

			if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED)
				success = e_etesync_connection_maybe_reconnect_sync (connection, backend, cancellable, error);

			if (!success) {
				e_etesync_utils_set_io_gerror (etebase_error, etebase_error_get_message (), error);
				break;
			} else /* as collection manager may have changed */
				item_mgr = etebase_collection_manager_get_item_manager (connection->priv->col_mgr, col_obj);
		}
	}

	etebase_item_manager_destroy (item_mgr);
	*out_new_sync_tag = stoken;

	return success;
}

gboolean
e_etesync_connection_get_changes_sync (EEteSyncConnection *connection,
				       EBackend *backend,
				       const EteSyncType type,
				       const gchar *last_sync_tag,
				       const EtebaseCollection *col_obj,
				       ECache *cache,
				       gchar **out_new_sync_tag,
				       GSList **out_created_objects, /* EBookMetaBackendInfo* or ECalMetaBackendInfo* */
				       GSList **out_modified_objects, /* EBookMetaBackendInfo* or ECalMetaBackendInfo* */
				       GSList **out_removed_objects, /* EBookMetaBackendInfo* or ECalMetaBackendInfo* */
				       GCancellable *cancellable,
				       GError **error)
{
	EtebaseItemManager *item_mgr;
	gchar *stoken;
	gboolean done = FALSE;
	gboolean success = TRUE;
	gboolean is_memo;

	stoken = g_strdup (last_sync_tag);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (!col_obj) {
		return FALSE;
	}

	is_memo = e_etesync_connection_backend_is_for_memos (backend);
	item_mgr = etebase_collection_manager_get_item_manager (connection->priv->col_mgr, col_obj);

	while (!done) {
		EtebaseItem **items_data;
		EtebaseItemListResponse *item_list;
		guintptr items_data_len, item_iter;

		if (e_etesync_connection_chunk_itemlist_fetch_sync (item_mgr, stoken, E_ETESYNC_ITEM_FETCH_LIMIT, &item_list, &items_data_len, &stoken, &done)) {

			items_data = g_alloca (sizeof (EtebaseItem *) * E_ETESYNC_ITEM_FETCH_LIMIT);
			etebase_item_list_response_get_data (item_list, (const EtebaseItem **) items_data);

			/* At this point, items_data are not empty, then we should loop on items and add each
			   one to the hashtable as meta backend info for each contact */
			if (items_data) {
				for (item_iter = 0; item_iter < items_data_len; item_iter++) {
					const EtebaseItem *item = items_data[item_iter];
					gchar *content = NULL, *data_uid = NULL, *revision = NULL, *item_cache_b64, buf[BUFF_SIZE];
					gintptr content_len;
					gboolean is_exist = FALSE;

					content_len = etebase_item_get_content (item, buf, sizeof (buf));

					if (content_len < 0) {
						break;
					} else if (content_len >= sizeof (buf)) {
						content = g_slice_alloc0 (content_len + 1);
						etebase_item_get_content (item, content, content_len);
						content[content_len] = 0;
					} else {
						buf[content_len] = 0;
					}

					item_cache_b64 = e_etesync_utils_etebase_item_to_base64 (item, item_mgr);

					if (type == E_ETESYNC_ADDRESSBOOK) {
						EBookMetaBackendInfo *nfo;

						/* create EBookMetaBackendInfo * to be stored in GSList, data uid is contact uid */
						e_etesync_utils_get_contact_uid_revision (content ? content : buf, &data_uid, &revision);

						nfo = e_book_meta_backend_info_new (data_uid, revision, content ? content : buf, item_cache_b64);
						is_exist = e_cache_contains (cache, data_uid, E_CACHE_EXCLUDE_DELETED);

						/* data with uid exist, then it is modified or deleted, else it is new data */
						if (is_exist) {
							if (etebase_item_is_deleted (item))
								*out_removed_objects = g_slist_prepend (*out_removed_objects, nfo);
							else
								*out_modified_objects = g_slist_prepend (*out_modified_objects, nfo);
						} else {
							if (!etebase_item_is_deleted (item))
								*out_created_objects = g_slist_prepend (*out_created_objects, nfo);
							else
								e_book_meta_backend_info_free (nfo);
						}
					} else if (type == E_ETESYNC_CALENDAR) {
						ECalMetaBackendInfo *nfo;

						if (is_memo) {
							EtebaseItemMetadata *item_meta;
							const gchar *summary;
							gchar *ical_str;
							time_t now;

							item_meta = etebase_item_get_meta (item);
							summary = etebase_item_metadata_get_name (item_meta);
							data_uid = g_strdup (etebase_item_get_uid (item));
							e_etesync_utils_get_time_now (&now);

							/* change plain text to a icomp vjournal object */
							ical_str = e_etesync_connection_notes_new_ical_string ((time_t) now, (time_t) now, data_uid, NULL, summary, content ? content : buf);
							nfo = e_cal_meta_backend_info_new (data_uid, NULL, ical_str, item_cache_b64);

							g_free (ical_str);
							etebase_item_metadata_destroy (item_meta);
						} else {
							/* create ECalMetaBackendInfo * to be stored in GSList, data uid is compounent uid */
							e_etesync_utils_get_component_uid_revision (content ? content : buf, &data_uid, &revision);
							nfo = e_cal_meta_backend_info_new (data_uid, revision, content ? content : buf, item_cache_b64);
						}

						is_exist = e_cache_contains (cache, data_uid, E_CACHE_EXCLUDE_DELETED);

						/* data with uid exist, then it is modified or deleted, else it is new data */
						if (is_exist) {
							if (etebase_item_is_deleted (item))
								*out_removed_objects = g_slist_prepend (*out_removed_objects, nfo);
							else
								*out_modified_objects = g_slist_prepend (*out_modified_objects, nfo);
						} else {
							if (!etebase_item_is_deleted (item))
								*out_created_objects = g_slist_prepend (*out_created_objects, nfo);
							else
								e_cal_meta_backend_info_free (nfo);
						}
					}

					g_free (revision);
					g_slice_free1 (content_len + 1, content);
					g_free (data_uid);
					g_free (item_cache_b64);
				}
			}
			etebase_item_list_response_destroy (item_list);

		} else {
			EtebaseErrorCode etebase_error;

			etebase_error = etebase_error_get_code ();
			success = FALSE;

			if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED)
				success = e_etesync_connection_maybe_reconnect_sync (connection, backend, cancellable, error);

			if (!success) {
				e_etesync_utils_set_io_gerror (etebase_error, etebase_error_get_message (), error);
				break;
			} else /* as collection manager may have changed */
				item_mgr = etebase_collection_manager_get_item_manager (connection->priv->col_mgr, col_obj);
		}
	}

	etebase_item_manager_destroy (item_mgr);
	*out_new_sync_tag = stoken;

	return success;
}

/* ------------------------ Uploading item functions -----------------------*/

gboolean
e_etesync_connection_item_upload_sync (EEteSyncConnection *connection,
				       EBackend *backend,
				       const EtebaseCollection *col_obj,
				       const EteSyncAction action,
				       const gchar *content,
				       const gchar *uid,
				       const gchar *extra, /* item_cache_b64 */
				       gchar **out_new_uid,
				       gchar **out_new_extra,
				       GCancellable *cancellable,
				       GError **error)
{
	EtebaseItemManager *item_mgr;
	gboolean success = TRUE;
	gboolean is_memo;
	const gchar *item_cache_b64 = extra;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (col_obj != NULL, FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_rec_mutex_lock (&connection->priv->connection_lock);

	is_memo = e_etesync_connection_backend_is_for_memos (backend);
	item_mgr = etebase_collection_manager_get_item_manager (connection->priv->col_mgr, col_obj);

	if (item_mgr) {
		EtebaseItemMetadata *item_metadata = NULL;
		EtebaseItem *item;
		time_t now;
		gchar *item_name, *item_content; /* Added to support EteSync notes type */

		e_etesync_utils_get_time_now (&now);

		/* If it is etesync note item, then set the EteSyncitem metadata name (uid) to summary in VJOURNAL
		   and set the EteSyncItem content to the description in VJOURNAL */
		if (action != E_ETESYNC_ITEM_ACTION_DELETE && is_memo) {
			ICalComponent *icomp = i_cal_component_new_from_string (content);

			item_name = g_strdup (i_cal_component_get_summary (icomp)); /* set data_uid to summary */
			item_content = g_strdup (i_cal_component_get_description (icomp)); /* set content to description */

			g_object_unref (icomp);
		} else {
			item_name = g_strdup (uid);
			item_content = g_strdup (content);
		}

		if (action == E_ETESYNC_ITEM_ACTION_CREATE) {
			item_metadata = etebase_item_metadata_new ();

			etebase_item_metadata_set_name (item_metadata, item_name);
			etebase_item_metadata_set_mtime (item_metadata, &now);

			item = etebase_item_manager_create (item_mgr, item_metadata, item_content ? item_content : "" , item_content ? strlen (item_content) : 0);
		} else {
			item = e_etesync_utils_etebase_item_from_base64 (item_cache_b64, item_mgr);
			if (!item) {
				success = FALSE;
				g_clear_error (error);
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Item not found"));
			} else {
				item_metadata = etebase_item_get_meta (item);

				etebase_item_metadata_set_name (item_metadata, item_name);
				etebase_item_metadata_set_mtime (item_metadata, &now);
				etebase_item_set_meta(item, item_metadata);

				if (action == E_ETESYNC_ITEM_ACTION_MODIFY)
					etebase_item_set_content (item, item_content ? item_content : "" , item_content ? strlen (item_content) : 0);
				else if (action == E_ETESYNC_ITEM_ACTION_DELETE)
					etebase_item_delete (item);
			}
		}

		/* This could fail when trying to fetch an item and it wasn't found in modify/delete */
		if (success) {
			success = !etebase_item_manager_batch (item_mgr, (const EtebaseItem **) &item, 1, NULL);

			if (!success) {
				EtebaseErrorCode etebase_error = etebase_error_get_code ();
				/* This is used to check if the error was due to expired token, if so try to get a new token, then try again */
				if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED &&
				    e_etesync_connection_maybe_reconnect_sync (connection, backend, cancellable, error)) {

					success = !etebase_item_manager_batch (item_mgr, (const EtebaseItem **) &item, 1, NULL);
				}

				if (!success)
					e_etesync_utils_set_io_gerror (etebase_error, etebase_error_get_message (), error);
			}

			if (out_new_extra)
				*out_new_extra = success ? e_etesync_utils_etebase_item_to_base64 (item, item_mgr) : NULL;

			/* Set the new uid for notes from the EteSyncitem uid, as EteSync notes item doesn't contain
			   uid in its content as other etesync types (calendar, tasks, contacts) */
			if (action == E_ETESYNC_ITEM_ACTION_CREATE &&
			    out_new_uid &&
			    is_memo)
				*out_new_uid = g_strdup (etebase_item_get_uid (item));

			if (item_metadata)
				etebase_item_metadata_destroy (item_metadata);
			etebase_item_destroy (item);
		}

		g_free (item_name);
		g_free (item_content);
		etebase_item_manager_destroy (item_mgr);
	}

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

static gboolean
e_etesync_connection_batch_modify_delete_sync (EEteSyncConnection *connection,
					       EBackend *backend,
					       const EtebaseCollection *col_obj,
					       const EteSyncAction action,
					       const EteSyncType type,
					       const gchar *const *content,
					       const gchar *const *data_uids,
					       guint content_len,
					       ECache *cache,
					       GSList **out_batch_info,
					       GCancellable *cancellable,
					       GError **error)
{
	EtebaseItemManager *item_mgr;
	gboolean success = TRUE;
	gboolean is_memo;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (col_obj != NULL, FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_rec_mutex_lock (&connection->priv->connection_lock);

	is_memo = e_etesync_connection_backend_is_for_memos (backend);
	item_mgr = etebase_collection_manager_get_item_manager (connection->priv->col_mgr, col_obj);

	if (item_mgr) {
		EtebaseItem *items[content_len];
		guint ii;
		time_t now;

		e_etesync_utils_get_time_now (&now);

		memset (items, 0, content_len);

		for (ii = 0; ii < content_len && success; ii++) {
			EtebaseItemMetadata *item_metadata = NULL;
			gchar *data_uid = NULL, *revision = NULL, *item_cache_b64 = NULL;

			if (type == E_ETESYNC_ADDRESSBOOK) {/* Contact */
				e_etesync_utils_get_contact_uid_revision (content[ii], &data_uid, &revision);
				e_book_cache_get_contact_extra (E_BOOK_CACHE (cache), data_uid, &item_cache_b64, NULL, NULL);
			} else if (type == E_ETESYNC_CALENDAR) {/* Calendar */

				if (is_memo)
					data_uid = g_strdup (data_uids[ii]);
				else
					e_etesync_utils_get_component_uid_revision (content[ii], &data_uid, &revision);

				e_cal_cache_get_component_extra (E_CAL_CACHE (cache), data_uid, NULL, &item_cache_b64, NULL, NULL);
			}

			items[ii] = e_etesync_utils_etebase_item_from_base64 (item_cache_b64, item_mgr);

			if (!items[ii]) {
				success = FALSE;
				g_clear_error (error);
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Item not found"));
			} else {
				item_metadata = etebase_item_get_meta (items[ii]);

				etebase_item_metadata_set_mtime (item_metadata, &now);
				etebase_item_set_meta(items[ii], item_metadata);

				if (action == E_ETESYNC_ITEM_ACTION_MODIFY) {/* Modify */

					if (is_memo) { /* notes */
						ICalComponent *icomp;
						const gchar *notes_item_content; /* notes_item_content is add to support EteSync notes */

						icomp = i_cal_component_new_from_string (content[ii]);
						notes_item_content = i_cal_component_get_description (icomp);

						etebase_item_metadata_set_name (item_metadata, i_cal_component_get_summary (icomp));
						etebase_item_set_meta (items[ii], item_metadata);

						etebase_item_set_content (items[ii], notes_item_content ? notes_item_content : "", notes_item_content ? strlen (notes_item_content) : 0);

						g_object_unref (icomp);
					} else { /* Events and Tasks */
						etebase_item_set_content (items[ii], content[ii], strlen (content[ii]));
					}
				} else if (action == E_ETESYNC_ITEM_ACTION_DELETE) /* Delete */
					etebase_item_delete (items[ii]);

				g_free (item_cache_b64);
				item_cache_b64 = e_etesync_utils_etebase_item_to_base64 (items[ii], item_mgr);

				if (type == E_ETESYNC_ADDRESSBOOK) { /* Contact */
					EBookMetaBackendInfo *nfo;

					nfo = e_book_meta_backend_info_new (data_uid, revision, content[ii], item_cache_b64);
					*out_batch_info = g_slist_prepend (*out_batch_info, nfo);
				} else if (type == E_ETESYNC_CALENDAR) { /* Calendar */
					ECalMetaBackendInfo *nfo;

					nfo = e_cal_meta_backend_info_new (data_uid, revision, content[ii], item_cache_b64);
					*out_batch_info = g_slist_prepend (*out_batch_info, nfo);
				}
			}
			g_free (data_uid);
			g_free (revision);
			g_free (item_cache_b64);
			if (item_metadata)
				etebase_item_metadata_destroy (item_metadata);
		}

		/* This could fail when trying to fetch an item and it wasn't found in modify */
		if (success) {
			success = !etebase_item_manager_batch (item_mgr, (const EtebaseItem **) items, ETEBASE_UTILS_C_ARRAY_LEN (items), NULL);

			if (!success) {
				EtebaseErrorCode etebase_error = etebase_error_get_code ();

				/* This is used to check if the error was due to expired token, if so try to get a new token, then try again */
				if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED &&
				    e_etesync_connection_maybe_reconnect_sync (connection, backend, cancellable, error)) {

					success = !etebase_item_manager_batch (item_mgr, (const EtebaseItem **) items, ETEBASE_UTILS_C_ARRAY_LEN (items), NULL);
				}

				if (!success)
					e_etesync_utils_set_io_gerror (etebase_error, etebase_error_get_message (), error);
			}
		}
		for (ii = 0; ii < content_len && items[ii]; ii++)
			etebase_item_destroy (items[ii]);
	}
	etebase_item_manager_destroy (item_mgr);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

gboolean
e_etesync_connection_batch_create_sync (EEteSyncConnection *connection,
					EBackend *backend,
					const EtebaseCollection *col_obj,
					const EteSyncType type,
					const gchar *const *content,
					guint content_len,
					GSList **out_batch_info,
					GCancellable *cancellable,
					GError **error)
{
	EtebaseItemManager *item_mgr;
	gboolean success = TRUE;
	gboolean is_memo;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (col_obj != NULL, FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_rec_mutex_lock (&connection->priv->connection_lock);

	is_memo = e_etesync_connection_backend_is_for_memos (backend);
	item_mgr = etebase_collection_manager_get_item_manager (connection->priv->col_mgr, col_obj);

	if (item_mgr) {
		EtebaseItem *items[content_len];
		guint ii;
		time_t now;

		e_etesync_utils_get_time_now (&now);

		for (ii = 0; ii < content_len; ii++) {
			EtebaseItemMetadata *item_metadata = NULL;
			gchar *data_uid = NULL, *revision = NULL, *notes_item_content = NULL; /* notes_item_content is add to support EteSync notes */
			gchar *item_cache_b64;

			if (type == E_ETESYNC_ADDRESSBOOK) /* Contact */
				e_etesync_utils_get_contact_uid_revision (content[ii], &data_uid, &revision);
			else if (type == E_ETESYNC_CALENDAR) { /* Calendar */
				if (is_memo) { /* Notes */
					ICalComponent *icomp = i_cal_component_new_from_string (content[ii]);

					data_uid = g_strdup (i_cal_component_get_summary (icomp));
					notes_item_content = g_strdup (i_cal_component_get_description (icomp));

					g_object_unref (icomp);
				} else { /* Events and Tasks */
					e_etesync_utils_get_component_uid_revision (content[ii], &data_uid, &revision);
				}
			}

			item_metadata = etebase_item_metadata_new ();

			etebase_item_metadata_set_name (item_metadata, data_uid);
			etebase_item_metadata_set_mtime (item_metadata, &now);

			if (is_memo) { /* Notes */
				items[ii] = etebase_item_manager_create (item_mgr, item_metadata, notes_item_content ? notes_item_content : "", notes_item_content ? strlen (notes_item_content) : 0);
				g_free (notes_item_content);
			} else { /* Addressbook, Calendar, Task */
				items[ii] = etebase_item_manager_create (item_mgr, item_metadata, content[ii], strlen (content[ii]));
			}

			item_cache_b64 = e_etesync_utils_etebase_item_to_base64 (items[ii], item_mgr);

			if (type == E_ETESYNC_ADDRESSBOOK) { /* Contact */
				EBookMetaBackendInfo *nfo;

				nfo = e_book_meta_backend_info_new (data_uid, revision, content[ii], item_cache_b64);
				*out_batch_info = g_slist_prepend (*out_batch_info, nfo);
			} else if (type == E_ETESYNC_CALENDAR) { /* Calendar */
				ECalMetaBackendInfo *nfo;

				if (is_memo) { /* Notes */
					g_free (data_uid);
					data_uid = g_strdup (etebase_item_get_uid (items[ii]));
				}

				nfo = e_cal_meta_backend_info_new (data_uid, revision, content[ii], item_cache_b64);
				*out_batch_info = g_slist_prepend (*out_batch_info, nfo);
			}
			g_free (data_uid);
			g_free (revision);
			g_free (item_cache_b64);
			etebase_item_metadata_destroy (item_metadata);
		}

		success = !etebase_item_manager_batch (item_mgr, (const EtebaseItem **) items, ETEBASE_UTILS_C_ARRAY_LEN (items), NULL);

		if (!success) {
			EtebaseErrorCode etebase_error = etebase_error_get_code ();

			/* This is used to check if the error was due to expired token, if so try to get a new token, then try again */
			if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED &&
			    e_etesync_connection_maybe_reconnect_sync (connection, backend, cancellable, error)) {

				success = !etebase_item_manager_batch (item_mgr, (const EtebaseItem **) items, ETEBASE_UTILS_C_ARRAY_LEN (items), NULL);
			}

			if (!success)
				e_etesync_utils_set_io_gerror (etebase_error, etebase_error_get_message (), error);
		}

		for (ii = 0; ii < content_len && success; ii++)
			etebase_item_destroy (items[ii]);
	}
	etebase_item_manager_destroy (item_mgr);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

gboolean
e_etesync_connection_batch_modify_sync (EEteSyncConnection *connection,
					EBackend *backend,
					const EtebaseCollection *col_obj,
					const EteSyncType type,
					const gchar *const *content,
					const gchar *const *data_uids,
					guint content_len,
					ECache *cache,
					GSList **out_batch_info,
					GCancellable *cancellable,
					GError **error)
{
	return e_etesync_connection_batch_modify_delete_sync (connection, backend, col_obj, E_ETESYNC_ITEM_ACTION_MODIFY, type, content, data_uids, content_len, cache, out_batch_info, cancellable, error);
}

gboolean
e_etesync_connection_batch_delete_sync (EEteSyncConnection *connection,
					EBackend *backend,
					const EtebaseCollection *col_obj,
					const EteSyncType type,
					const gchar *const *content,
					const gchar *const *data_uids,
					guint content_len,
					ECache *cache,
					GSList **out_batch_info,
					GCancellable *cancellable,
					GError **error)
{
	return e_etesync_connection_batch_modify_delete_sync (connection, backend, col_obj, E_ETESYNC_ITEM_ACTION_DELETE, type, content, data_uids, content_len, cache, out_batch_info, cancellable, error);
}

/*----------------------------GObject functions----------------------*/

static void
etesync_connection_finalize (GObject *object)
{
	EEteSyncConnection *connection = E_ETESYNC_CONNECTION (object);

	g_mutex_lock (&connecting);
	/* remove the connection from the hash table */
	if (loaded_connections_permissions != NULL && connection->priv->hash_key &&
	    g_hash_table_lookup (loaded_connections_permissions, connection->priv->hash_key) == (gpointer) object) {
		g_hash_table_remove (loaded_connections_permissions, connection->priv->hash_key);
		if (g_hash_table_size (loaded_connections_permissions) == 0) {
			g_hash_table_destroy (loaded_connections_permissions);
			loaded_connections_permissions = NULL;
		}
	}
	g_mutex_unlock (&connecting);

	g_rec_mutex_lock (&connection->priv->connection_lock);
	e_etesync_connection_clear (connection);
	g_free (connection->priv->hash_key);
	g_clear_object (&connection->priv->collection_source);
	g_rec_mutex_unlock (&connection->priv->connection_lock);

	g_rec_mutex_clear (&connection->priv->connection_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_etesync_connection_parent_class)->finalize (object);
}

static void
e_etesync_connection_class_init (EEteSyncConnectionClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = etesync_connection_finalize;
}

static void
e_etesync_connection_init (EEteSyncConnection *connection)
{
	connection->priv = e_etesync_connection_get_instance_private (connection);

	connection->priv->requested_credentials = FALSE;
	connection->priv->hash_key = NULL;
	g_rec_mutex_init (&connection->priv->connection_lock);
}

/* ---------------------Encapsulation functions------------------- */

const gchar *
e_etesync_connection_get_session_key (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->session_key;
}

void
e_etesync_connection_set_session_key (EEteSyncConnection *connection,
				      const gchar* session_key)
{
	g_return_if_fail (E_IS_ETESYNC_CONNECTION (connection));

	g_free (connection->priv->session_key);
	connection->priv->session_key = g_strdup (session_key);
}

gboolean
e_etesync_connection_get_requested_credentials (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), FALSE);

	return connection->priv->requested_credentials;
}

EtebaseClient *
e_etesync_connection_get_etebase_client (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->etebase_client;
}

EtebaseAccount *
e_etesync_connection_get_etebase_account (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->etebase_account;
}

EtebaseCollectionManager *
e_etesync_connection_get_collection_manager (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->col_mgr;
}
