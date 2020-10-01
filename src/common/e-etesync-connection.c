/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-connection.c - EteSync connection.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>
#include "e-etesync-connection.h"
#include "common/e-source-etesync.h"
#include "e-etesync-defines.h"

static GMutex connecting;
static GHashTable *loaded_connections_permissions = NULL; /* gchar* ~> EEteSyncConnection* */

struct _EEteSyncConnectionPrivate {
	EteSync *etesync;
	EteSyncJournalManager *journal_manager;
	EteSyncAsymmetricKeyPair *keypair;

	gchar *token;
	gchar *derived;
	gchar *server_url;
	gchar *username;

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
	if (connection) {
		if (connection->priv->keypair) {
			etesync_keypair_destroy (connection->priv->keypair);
			connection->priv->keypair = NULL;
		}

		if (connection->priv->journal_manager) {
			etesync_journal_manager_destroy (connection->priv->journal_manager);
			connection->priv->journal_manager = NULL;
		}

		if (connection->priv->etesync) {
			etesync_destroy (connection->priv->etesync);
			connection->priv->etesync = NULL;
		}

		g_clear_pointer (&connection->priv->token, g_free);
		g_clear_pointer (&connection->priv->derived, g_free);
		g_clear_pointer (&connection->priv->username, g_free);
		g_clear_pointer (&connection->priv->server_url, g_free);
	}
	g_rec_mutex_unlock (&connection->priv->connection_lock);
}

EEteSyncConnection *
e_etesync_connection_new (void)
{
	return g_object_new (E_TYPE_ETESYNC_CONNECTION, NULL);
}

EEteSyncConnection *
e_etesync_connection_new_from_user_url (const gchar *username,
					const gchar *server_url)
{
	EEteSyncConnection *connection;
	gchar *hash_key;

	g_return_val_if_fail (username != NULL, NULL);
	g_return_val_if_fail (server_url != NULL, NULL);

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
	connection = e_etesync_connection_new ();
	connection->priv->hash_key = hash_key;  /* takes ownership */

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
e_etesync_connection_check_token_validation (EEteSyncConnection *connection)
{
	EteSyncUserInfoManager *user_info_manager;
	EteSyncUserInfo *user_info;
	EteSyncErrorCode etesync_error;
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	g_return_val_if_fail (connection != NULL ,E_SOURCE_AUTHENTICATION_ERROR);
	g_return_val_if_fail (connection->priv->etesync != NULL ,E_SOURCE_AUTHENTICATION_ERROR);
	g_return_val_if_fail (connection->priv->username != NULL ,E_SOURCE_AUTHENTICATION_ERROR);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	user_info_manager = etesync_user_info_manager_new (connection->priv->etesync);
	user_info = etesync_user_info_manager_fetch (user_info_manager, connection->priv->username);
	etesync_error = etesync_get_error_code ();

	if (user_info == NULL) {
		if (etesync_error == ETESYNC_ERROR_CODE_UNAUTHORIZED)
			result = E_SOURCE_AUTHENTICATION_REJECTED;
		else
			result = E_SOURCE_AUTHENTICATION_ERROR;
	} else {
		etesync_user_info_destroy (user_info);
	}
	etesync_user_info_manager_destroy (user_info_manager);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return result;
}

/* create: connection, journal_type, display_name */
static gboolean
e_etesync_connection_journal_create (EEteSyncConnection *connection,
				     const gchar *journal_type,
				     const gchar *display_name,
				     const gchar *description,
				     const gint32 color,
				     EteSyncJournal **out_journal)
{
	EteSyncJournal *journal;
	EteSyncCollectionInfo *info = NULL;
	EteSyncCryptoManager *crypto_manager = NULL;
	gint32 local_error;
	gchar *uid = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);
	g_return_val_if_fail (journal_type != NULL, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	uid = etesync_gen_uid ();
	journal = etesync_journal_new (uid, ETESYNC_CURRENT_VERSION);
	info = etesync_collection_info_new (journal_type, display_name, description, color);
	crypto_manager = etesync_journal_get_crypto_manager (journal, connection->priv->derived, connection->priv->keypair);

	etesync_journal_set_info (journal, crypto_manager, info);
	local_error = etesync_journal_manager_create (connection->priv->journal_manager, journal);

	/* zero means success, other means fail */
	if (local_error)
		success = FALSE;

	g_free (uid);
	etesync_crypto_manager_destroy (crypto_manager);
	etesync_collection_info_destroy (info);

	if (out_journal && success)
		*out_journal = journal;
	else
		etesync_journal_destroy (journal);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

/* modify: connection, journal_uid, journal_type, display_name */
static gboolean
e_etesync_connection_journal_modify (EEteSyncConnection *connection,
				     const gchar *journal_uid,
				     const gchar *journal_type,
				     const gchar *display_name,
				     const gchar *description,
				     const gint32 color,
				     EteSyncJournal **out_journal)
{
	EteSyncJournal *journal;
	EteSyncCryptoManager *crypto_manager = NULL;
	EteSyncCollectionInfo *info = NULL;
	gint32 local_error;
	gboolean success = TRUE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (journal_uid != NULL, FALSE);
	g_return_val_if_fail (journal_type != NULL, FALSE);
	g_return_val_if_fail (display_name && *display_name, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	journal = etesync_journal_manager_fetch(connection->priv->journal_manager, journal_uid);
	info = etesync_collection_info_new (journal_type, display_name, description, color);
	crypto_manager = etesync_journal_get_crypto_manager (journal, connection->priv->derived, connection->priv->keypair);

	etesync_journal_set_info (journal, crypto_manager, info);
	local_error = etesync_journal_manager_update (connection->priv->journal_manager, journal);

	/* zero means success, other means fail */
	if (local_error)
		success = FALSE;

	etesync_crypto_manager_destroy (crypto_manager);
	etesync_collection_info_destroy (info);

	if (out_journal && success)
		*out_journal = journal;
	else
		etesync_journal_destroy (journal);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

/* delete: connection, action, journal_uid */
static gboolean
e_etesync_connection_journal_delete (EEteSyncConnection *connection,
				     const gchar *journal_uid)
{
	EteSyncJournal *journal;
	EteSyncCryptoManager *crypto_manager = NULL;
	gint32 local_error;
	gboolean success = TRUE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (journal_uid != NULL, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	journal = etesync_journal_manager_fetch (connection->priv->journal_manager, journal_uid);
	crypto_manager = etesync_journal_get_crypto_manager (journal, connection->priv->derived, connection->priv->keypair);

	local_error = etesync_journal_manager_delete (connection->priv->journal_manager, journal);

	/* zero means success, other means fail */
	if (local_error)
		success = FALSE;

	etesync_crypto_manager_destroy (crypto_manager);
	etesync_journal_destroy (journal);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

/*
 * create: connection, action, journal_type, display_name, color, out_journal
 * modify: connection, action, journal_uid, journal_type, display_name, color
 * delete: connection, action, journal_uid
 * out_journal returns journal for created and modified,
 * out_journal should be freed with etesync_journal_destroy (),
 * when not needed anymore, it can be NULL if not needed
 */
gboolean
e_etesync_connection_write_journal_action (EEteSyncConnection *connection,
					   const gchar *action,
					   const gchar *journal_uid,
					   const gchar *journal_type,
					   const gchar *display_name,
					   const gchar *description,
					   const gint32 color,
					   EteSyncJournal **out_journal)
{
	gboolean success = FALSE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (action != NULL, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_ADD)) {
		success = e_etesync_connection_journal_create (connection, journal_type, display_name, description, color, out_journal);
	} else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_CHANGE)) {
		success =  e_etesync_connection_journal_modify (connection, journal_uid, journal_type, display_name, description, color, out_journal);
	} else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_DELETE)) {
		success =  e_etesync_connection_journal_delete (connection, journal_uid);
	}

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	/* action not known */
	return success;
}

/* This function sets the keypair in connection
   it also sets the result and error, both can be NULL */
static gboolean
e_etesync_connection_set_keypair (EEteSyncConnection *connection,
				  ESourceAuthenticationResult *out_result,
				  EteSyncErrorCode *out_etesync_error)
{
	EteSyncUserInfoManager *user_info_manager;
	EteSyncUserInfo *user_info;
	EteSyncErrorCode local_etesync_error = ETESYNC_ERROR_CODE_NO_ERROR;
	ESourceAuthenticationResult local_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	gboolean success = TRUE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (connection->priv->etesync != NULL, FALSE);
	g_return_val_if_fail (connection->priv->username != NULL, FALSE);
	g_return_val_if_fail (connection->priv->derived != NULL, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	user_info_manager = etesync_user_info_manager_new (connection->priv->etesync);
	user_info = etesync_user_info_manager_fetch (user_info_manager, connection->priv->username);

	if (user_info) {
		EteSyncCryptoManager *user_info_crypto_manager;

		user_info_crypto_manager = etesync_user_info_get_crypto_manager (user_info, connection->priv->derived);
		connection->priv->keypair = etesync_user_info_get_keypair (user_info, user_info_crypto_manager);

		if (!connection->priv->keypair) {
			local_etesync_error = etesync_get_error_code ();

			if (local_etesync_error == ETESYNC_ERROR_CODE_ENCRYPTION_MAC)
				local_result = E_SOURCE_AUTHENTICATION_REJECTED;
			else
				local_result = E_SOURCE_AUTHENTICATION_ERROR;
			success = FALSE;
		}

		etesync_user_info_destroy (user_info);
		etesync_crypto_manager_destroy (user_info_crypto_manager);
	} else {
		local_etesync_error = etesync_get_error_code ();
		success = FALSE;
	}

	etesync_user_info_manager_destroy (user_info_manager);

	if (out_etesync_error)
		*out_etesync_error = local_etesync_error;
	if (out_result)
		*out_result = local_result;

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

static gboolean
e_etesync_connection_is_new_account (EEteSyncConnection *connection,
				     gboolean *out_is_new_account,
				     EteSyncErrorCode *out_etesync_error)
{

	EteSyncUserInfoManager *user_info_manager;
	EteSyncUserInfo *user_info;
	EteSyncErrorCode local_etesync_error = ETESYNC_ERROR_CODE_NO_ERROR;
	gboolean success = TRUE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (connection->priv->etesync != NULL, FALSE);
	g_return_val_if_fail (connection->priv->username != NULL, FALSE);
	g_return_val_if_fail (out_is_new_account != NULL, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	user_info_manager = etesync_user_info_manager_new (connection->priv->etesync);
	user_info = etesync_user_info_manager_fetch (user_info_manager, connection->priv->username);

	if (!user_info) {
		local_etesync_error = etesync_get_error_code ();

		if (local_etesync_error == ETESYNC_ERROR_CODE_NOT_FOUND)
			*out_is_new_account = TRUE;
		else
			success = FALSE;
	} else {
		*out_is_new_account = FALSE;
		etesync_user_info_destroy (user_info);
	}

	if (out_etesync_error)
		*out_etesync_error = local_etesync_error;

	etesync_user_info_manager_destroy (user_info_manager);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

gboolean
e_etesync_connection_init_userinfo (EEteSyncConnection *connection)
{
	EteSyncUserInfoManager *user_info_manager;
	EteSyncUserInfo *user_info;
	EteSyncErrorCode etesync_error;
	gboolean success = TRUE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (connection->priv->etesync != NULL, FALSE);
	g_return_val_if_fail (connection->priv->username != NULL, FALSE);
	g_return_val_if_fail (connection->priv->derived != NULL, FALSE);

	user_info_manager = etesync_user_info_manager_new (connection->priv->etesync);
	user_info = etesync_user_info_manager_fetch (user_info_manager, connection->priv->username);

	etesync_error = etesync_get_error_code ();

	if (!user_info && etesync_error == ETESYNC_ERROR_CODE_NOT_FOUND) { /* Should create a user info and upload it */
		gint32 numeric_error;
		EteSyncCryptoManager *user_info_crypto_manager;

		user_info = etesync_user_info_new (connection->priv->username, ETESYNC_CURRENT_VERSION);
		user_info_crypto_manager = etesync_user_info_get_crypto_manager (user_info, connection->priv->derived);
		connection->priv->keypair = etesync_crypto_generate_keypair (connection->priv->etesync);

		etesync_user_info_set_keypair (user_info, user_info_crypto_manager, connection->priv->keypair);
		numeric_error = etesync_user_info_manager_create (user_info_manager, user_info);

		/* zero means success */
		if (numeric_error){
			etesync_error = etesync_get_error_code ();
			success = FALSE;
		} else {
			/* create defult journal for new accounts */
			e_etesync_connection_write_journal_action (connection, ETESYNC_SYNC_ENTRY_ACTION_ADD, NULL,
						ETESYNC_COLLECTION_TYPE_ADDRESS_BOOK, _("My Contacts"), NULL, ETESYNC_COLLECTION_DEFAULT_COLOR, NULL);
			e_etesync_connection_write_journal_action (connection, ETESYNC_SYNC_ENTRY_ACTION_ADD, NULL,
						ETESYNC_COLLECTION_TYPE_CALENDAR, _("My Calendar"), NULL, ETESYNC_COLLECTION_DEFAULT_COLOR, NULL);
			e_etesync_connection_write_journal_action (connection, ETESYNC_SYNC_ENTRY_ACTION_ADD, NULL,
						ETESYNC_COLLECTION_TYPE_TASKS, _("My Tasks"), NULL, ETESYNC_COLLECTION_DEFAULT_COLOR, NULL);
		}

		etesync_user_info_destroy (user_info);
		etesync_crypto_manager_destroy (user_info_crypto_manager);
	} else {
		success = FALSE;
	}
	etesync_user_info_manager_destroy (user_info_manager);

	return success;
}

ESourceAuthenticationResult
e_etesync_connection_set_connection_from_sources (EEteSyncConnection *connection,
						  const ENamedParameters *credentials,
						  ESourceAuthentication *authentication_extension,
						  ESourceCollection *collection_extension)
{
	ESourceAuthenticationResult result;
	const gchar *server_url , *username, *token, *derived;

	g_rec_mutex_lock (&connection->priv->connection_lock);

	username = e_source_authentication_get_user (authentication_extension);

	e_etesync_connection_clear (connection);

	/* 1) get_server from ESourceAuthentication as it was saved when the user was entering credentials in the dialog first time.
	      Set the etesync then for this backend so as long as the backend is alive, we don't have to do this process again */
	server_url = e_source_collection_get_calendar_url (collection_extension);
	connection->priv->etesync = etesync_new (PACKAGE "/" VERSION, server_url);

	/* problem with the server_url */
	if (!connection->priv->etesync) {
		g_rec_mutex_unlock (&connection->priv->connection_lock);
		return E_SOURCE_AUTHENTICATION_ERROR;
	}

	/* 2) get token and derived key from Credentials (Should be stored there) */
	token = e_named_parameters_get (credentials, E_ETESYNC_CREDENTIAL_TOKEN);
	derived = e_named_parameters_get (credentials, E_ETESYNC_CREDENTIAL_DERIVED_KEY);

	/* 3) check if the token or the derived key are NULL, if so that means this is the first time, so we will need
	      the credintials to start getting the token and derived key, then validate that token */
	if (!token || !derived) {
		g_rec_mutex_unlock (&connection->priv->connection_lock);
		return E_SOURCE_AUTHENTICATION_REJECTED;
	}

	connection->priv->username = g_strdup (username);
	connection->priv->server_url = g_strdup (server_url);
	connection->priv->token = g_strdup (token);
	connection->priv->derived = g_strdup (derived);

	etesync_set_auth_token(connection->priv->etesync, token);

	connection->priv->journal_manager = etesync_journal_manager_new (connection->priv->etesync);

	result = e_etesync_connection_check_token_validation (connection);

	/* setting the keypair */
	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED)
		e_etesync_connection_set_keypair (connection, &result, NULL);
	else
		e_etesync_connection_clear (connection);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return result;
}

gboolean
e_etesync_connection_set_connection (EEteSyncConnection *connection,
				     const gchar *username,
				     const gchar *password,
				     const gchar *encryption_password,
				     const gchar *server_url,
				     EteSyncErrorCode *out_etesync_error)
{
	EteSyncErrorCode local_etesync_error;
	EteSync *etesync;
	gboolean success;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (username, FALSE);
	g_return_val_if_fail (password, FALSE);
	g_return_val_if_fail (server_url && *server_url, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	e_etesync_connection_clear (connection);

	etesync = etesync_new (PACKAGE "/" VERSION, server_url);

	if (etesync) {
		connection->priv->etesync = etesync;
		connection->priv->username = g_strdup (username);
		connection->priv->server_url = g_strdup (server_url);

		if (encryption_password && *encryption_password)
			connection->priv->derived = etesync_crypto_derive_key (etesync, username, encryption_password);

		connection->priv->token = etesync_auth_get_token (etesync, username, password);

		if (connection->priv->token) {
			gboolean is_new_account;

			etesync_set_auth_token (etesync, connection->priv->token);

			connection->priv->journal_manager = etesync_journal_manager_new (etesync);
			success = e_etesync_connection_is_new_account (connection, &is_new_account, &local_etesync_error);

			if (success && !is_new_account)
				success = connection->priv->derived? e_etesync_connection_set_keypair (connection, NULL, &local_etesync_error) : FALSE;
			else
				/* This is a new account */
				success = FALSE;
		} else {
			local_etesync_error = etesync_get_error_code ();
			success = FALSE;
		}
	} else {
		/* Bad server name */
		local_etesync_error = etesync_get_error_code ();
		success = FALSE;
	}

	if (out_etesync_error)
		*out_etesync_error = local_etesync_error;

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

/* This function checks if the connection is set or not
   and if it needs setting or not */
gboolean
e_etesync_connection_needs_setting (EEteSyncConnection *connection,
				    const ENamedParameters *credentials,
				    ESourceAuthenticationResult *out_result)
{
	gboolean needs_setting = FALSE;
	ESourceAuthenticationResult local_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	g_return_val_if_fail (credentials != NULL, E_SOURCE_AUTHENTICATION_ERROR);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	if (connection && connection->priv->etesync) {
		const gchar *cred_token = e_named_parameters_get (credentials, E_ETESYNC_CREDENTIAL_TOKEN);

		if (cred_token) {
			if (!g_strcmp0 (cred_token, connection->priv->token)) {
				local_result = e_etesync_connection_check_token_validation (connection);

				/* This is kinda of a hack, if the token is invalid we need to first pull update
				   credentials using REQUIRED, as there might me another connection object that
				   update it, so before getting another token we check if one had been got */
				if (!connection->priv->requested_credentials && local_result == E_SOURCE_AUTHENTICATION_REJECTED) {
					connection->priv->requested_credentials = TRUE;
					local_result = E_SOURCE_AUTHENTICATION_REQUIRED;
				}
			} else
				if (cred_token)
					needs_setting = TRUE;
		} else
			local_result = E_SOURCE_AUTHENTICATION_REQUIRED;
	} else {
		if (credentials && e_named_parameters_count (credentials) >= 4) {
			needs_setting = TRUE;
		} else {
			if (connection->priv->requested_credentials) {
				local_result = E_SOURCE_AUTHENTICATION_REJECTED;
			} else {
				connection->priv->requested_credentials = TRUE;
				local_result = E_SOURCE_AUTHENTICATION_REQUIRED;
			}
		}
	}

	if (out_result)
		*out_result = local_result;
	if (local_result == E_SOURCE_AUTHENTICATION_ACCEPTED)
		connection->priv->requested_credentials = FALSE;

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return needs_setting;
}

/* ------------------- Book and calendar common function ------------------- */

void
e_etesync_connection_source_changed_cb (ESource *source,
					ESource *collection,
					gboolean is_contact)
{
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (E_IS_SOURCE (collection));

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
		const gchar *extension_name = NULL;
		const gchar *journal_type = NULL;
		gboolean success = TRUE;

		/* To avoid duplication (run in one of the callbacks in calendar/book backend) */
		if (is_contact) {
			if (e_source_has_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
				extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
				journal_type = ETESYNC_COLLECTION_TYPE_ADDRESS_BOOK;
			}
		} else {
			if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR)) {
				extension_name = E_SOURCE_EXTENSION_CALENDAR;
				journal_type = ETESYNC_COLLECTION_TYPE_CALENDAR;
			} else if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST)) {
				extension_name = E_SOURCE_EXTENSION_TASK_LIST;
				journal_type = ETESYNC_COLLECTION_TYPE_TASKS;
			}
		}

		if (journal_type == NULL) {
			success = FALSE;
		}

		if (success) {
			gchar *display_name = NULL;
			gint32 color = ETESYNC_COLLECTION_DEFAULT_COLOR;
			const gchar *journal_id = NULL;
			EEteSyncConnection *connection;

			connection = e_etesync_connection_new_connection (collection);

			if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
				ESourceEteSync *etesync_extension;

				etesync_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
				journal_id = e_source_etesync_get_journal_id (etesync_extension);
			}

			if (!g_str_equal (extension_name, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
				ESourceBackend *source_backend;
				const gchar *source_color = NULL;

				source_backend = e_source_get_extension (source, extension_name);
				source_color = e_source_selectable_get_color (E_SOURCE_SELECTABLE (source_backend));

				if (source_color) {
					color = g_ascii_strtoll (&source_color[1], NULL, 16);
					color |= 0xFF000000;
				}
			}

			display_name = e_source_dup_display_name (source);

			success = e_etesync_connection_write_journal_action (connection,
									     ETESYNC_SYNC_ENTRY_ACTION_CHANGE,
									     journal_id,
									     journal_type,
									     display_name,
									     NULL,
									     color,
									     NULL);

			g_free (display_name);
			g_object_unref (connection);
		}
	}
}

EEteSyncConnection *
e_etesync_connection_new_connection (ESource *collection)
{
	const gchar *username = NULL, *server_url = NULL;

	g_return_val_if_fail (E_IS_SOURCE (collection), NULL);

	if (e_source_has_extension (collection, E_SOURCE_EXTENSION_COLLECTION)) {
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (collection, E_SOURCE_EXTENSION_COLLECTION);
		server_url = e_source_collection_get_calendar_url (collection_extension);
	}

	if (e_source_has_extension (collection, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *authentication_extension;

		authentication_extension = e_source_get_extension (collection, E_SOURCE_EXTENSION_AUTHENTICATION);
		username = e_source_authentication_get_user (authentication_extension);
	}

	return e_etesync_connection_new_from_user_url (username, server_url);
}

gboolean
e_etesync_connection_get_journal_exists (EEteSyncConnection *connection,
					 const gchar *journal_id,
					 gboolean *out_is_read_only)
{
	EteSyncJournal *journal;
	gboolean success = TRUE;

	g_return_val_if_fail (connection->priv->journal_manager != NULL, FALSE);
	g_return_val_if_fail (journal_id && *journal_id, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	journal = etesync_journal_manager_fetch (connection->priv->journal_manager, journal_id);

	if (journal) {
		*out_is_read_only = etesync_journal_is_read_only (journal);
		etesync_journal_destroy (journal);
	} else {
		/* Journal ID is wrong or does not exist */
		success = FALSE;
	}

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

/*
 * returns True if it succeeded in reaching the server
 * False if there was a network error or the journal ID doesn't exist anymore
 * If *entries is null (first pointer in the array) then no changes else there are changes since last sync
 */
gboolean
e_etesync_connection_check_journal_changed_sync (EEteSyncConnection *connection,
						 const gchar *journal_id,
						 const gchar *last_sync_tag,
						 gint limit,
						 EteSyncEntry ***out_entries,
						 EteSyncCryptoManager **out_crypto_manager,
						 EteSyncEntryManager **out_entry_manager)
{
	EteSyncJournal *journal;
	gchar* journal_last_uid;

	g_return_val_if_fail (connection, FALSE);
	g_return_val_if_fail (journal_id, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	*out_crypto_manager = NULL;
	*out_entry_manager = NULL;
	*out_entries = NULL;

	journal = etesync_journal_manager_fetch (connection->priv->journal_manager, journal_id);

	/* Failed to connect to server or journal doesn't exist anymore */
	if (!journal) {
		g_rec_mutex_unlock (&connection->priv->connection_lock);
		return FALSE;
	}

	journal_last_uid = etesync_journal_get_last_uid (journal);

	/* If equal, it returns zero */
	if (g_strcmp0 (journal_last_uid, last_sync_tag) != 0) {
		*out_entry_manager = etesync_entry_manager_new (connection->priv->etesync, journal_id);
		*out_entries = etesync_entry_manager_list (*out_entry_manager, last_sync_tag, limit);

		if (*out_entries && **out_entries) {
			*out_crypto_manager = etesync_journal_get_crypto_manager (journal,
						connection->priv->derived, connection->priv->keypair);
		}
	}

	g_free (journal_last_uid);
	etesync_journal_destroy (journal);

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return TRUE;
}

/* This function is used to write query to server to
   add/modify/delete an entry inside a journal */
gboolean
e_etesync_connection_entry_write_action (EEteSyncConnection *connection,
					 const gchar *journal_id,
					 const gchar *last_sync_tag,
					 const gchar *content,
					 const gchar *action,
					 gchar **out_last_sync_tag,
					 EteSyncErrorCode *out_etesync_error)
{
	EteSyncJournal *journal;
	gboolean success = FALSE;

	g_return_val_if_fail (connection != NULL, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	journal = etesync_journal_manager_fetch (connection->priv->journal_manager, journal_id);

	if (journal) {
		EteSyncEntryManager *entry_manager;
		EteSyncCryptoManager *crypto_manager;

		entry_manager = etesync_entry_manager_new (connection->priv->etesync, journal_id);
		crypto_manager = etesync_journal_get_crypto_manager (journal, connection->priv->derived, connection->priv->keypair);

		if (crypto_manager) {
			EteSyncSyncEntry *sync_entry;
			EteSyncEntry *entries[2];

			/* create sync entry (data) and turn it to entry to save (encrypted) */
			sync_entry = etesync_sync_entry_new (action, content);

			entries[0] = etesync_entry_from_sync_entry (crypto_manager, sync_entry, last_sync_tag);
			entries[1] = NULL;

			*out_last_sync_tag = etesync_entry_get_uid (entries[0]);
			success = !etesync_entry_manager_create (entry_manager, (const EteSyncEntry* const*) entries, last_sync_tag);

			*out_etesync_error = etesync_get_error_code();

			etesync_entry_destroy (entries[0]);
			etesync_sync_entry_destroy (sync_entry);
			etesync_crypto_manager_destroy (crypto_manager);
		} else {
			/* Failed to save contact at etesync_journal_get_crypto_manager */
		}

		etesync_entry_manager_destroy (entry_manager);
		etesync_journal_destroy (journal);
	} else {
		/* Failed to save contact at etesync_journal_manager_fetch */
	}

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
}

gboolean
e_etesync_connection_multiple_entry_write_action (EEteSyncConnection *connection,
						  const gchar *journal_id,
						  const gchar * const * content,
						  guint length, /* length of content */
						  const gchar *action,
						  gchar **out_last_sync_tag,
						  EteSyncErrorCode *out_etesync_error)
{
	EteSyncJournal *journal;
	gboolean success = FALSE;

	g_return_val_if_fail (connection != NULL, FALSE);

	g_rec_mutex_lock (&connection->priv->connection_lock);

	journal = etesync_journal_manager_fetch (connection->priv->journal_manager, journal_id);

	if (journal) {
		EteSyncCryptoManager *crypto_manager;

		crypto_manager = etesync_journal_get_crypto_manager (journal,
								     connection->priv->derived,
								     connection->priv->keypair);
		if (crypto_manager) {
			EteSyncEntryManager *entry_manager;
			EteSyncEntry **entries;
			guint ii;

			entry_manager = etesync_entry_manager_new (connection->priv->etesync, journal_id);
			entries = g_slice_alloc0 ((length + 1) * sizeof (EteSyncEntry *));

			for (ii = 0; ii < length; ii++) {
				EteSyncSyncEntry *sync_entry;

				/* create sync entry (data) and turn it to entry to save (encrypted) */
				sync_entry = etesync_sync_entry_new (action, content[ii]);
				entries [ii] = etesync_entry_from_sync_entry (crypto_manager, sync_entry, *out_last_sync_tag);

				if (ii + 1 < length) {
					g_free (*out_last_sync_tag);
					*out_last_sync_tag = etesync_entry_get_uid (entries [ii]);
				}

				etesync_sync_entry_destroy (sync_entry);
			}

			entries[ii] = NULL;
			success = !etesync_entry_manager_create (entry_manager, (const EteSyncEntry* const*) entries, *out_last_sync_tag);

			g_free (*out_last_sync_tag);
			*out_last_sync_tag = etesync_entry_get_uid (entries [length -1]);

			*out_etesync_error = etesync_get_error_code();

			for (ii = 0; ii < length; ii++)
				etesync_entry_destroy (entries[ii]);
			g_slice_free1 ((length + 1) * sizeof (EteSyncEntry *), entries);
			etesync_entry_manager_destroy (entry_manager);
			etesync_crypto_manager_destroy (crypto_manager);
		}

		etesync_journal_destroy (journal);
	}

	g_rec_mutex_unlock (&connection->priv->connection_lock);

	return success;
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
e_etesync_connection_get_token (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->token;
}

const gchar *
e_etesync_connection_get_derived_key (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->derived;
}

const gchar *
e_etesync_connection_get_server_url (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->server_url;
}

const gchar *
e_etesync_connection_get_username (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->username;
}

gboolean
e_etesync_connection_get_requested_credentials (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), FALSE);

	return connection->priv->requested_credentials;
}

EteSync *
e_etesync_connection_get_etesync (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->etesync;
}

EteSyncJournalManager *
e_etesync_connection_get_journal_manager (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->journal_manager;
}

EteSyncAsymmetricKeyPair *
e_etesync_connection_get_keypair (EEteSyncConnection *connection)
{
	g_return_val_if_fail (E_IS_ETESYNC_CONNECTION (connection), NULL);

	return connection->priv->keypair;
}
