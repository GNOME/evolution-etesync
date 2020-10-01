/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-etesync-backend.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <etesync.h>

#include "e-etesync-backend.h"
#include "common/e-source-etesync.h"
#include "common/e-etesync-connection.h"
#include "common/e-etesync-defines.h"

struct _EEteSyncBackendPrivate {
	EEteSyncConnection *connection;
	GRecMutex etesync_lock;
};

G_DEFINE_TYPE_WITH_PRIVATE (EEteSyncBackend, e_etesync_backend, E_TYPE_COLLECTION_BACKEND)

static gchar *
etesync_backend_dup_resource_id (ECollectionBackend *backend,
				 ESource *child_source)
{
	ESourceEteSync *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_ETESYNC;
	extension = e_source_get_extension (child_source, extension_name);

	return e_source_etesync_dup_journal_id (extension);
}

static void
etesync_backend_update_enabled (ESource *data_source,
				ESource *collection_source)
{
	ESourceCollection *collection_extension = NULL;
	gboolean part_enabled = TRUE;

	g_return_if_fail (E_IS_SOURCE (data_source));

	if (!collection_source || !e_source_get_enabled (collection_source)) {
		e_source_set_enabled (data_source, FALSE);
		return;
	}

	if (e_source_has_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION))
		collection_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);

	if (e_source_has_extension (data_source, E_SOURCE_EXTENSION_CALENDAR) ||
	    e_source_has_extension (data_source, E_SOURCE_EXTENSION_TASK_LIST) ||
	    e_source_has_extension (data_source, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		part_enabled = !collection_extension
				|| e_source_collection_get_calendar_enabled (collection_extension)
				|| e_source_collection_get_contacts_enabled (collection_extension);
	}

	e_source_set_enabled (data_source, part_enabled);
}

static void
etesync_backend_populate (ECollectionBackend *backend)
{
	ESourceCollection *collection_extension;
	ESource *source;
	ESourceRegistryServer *server;
	GList *old_resources, *iter;

	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_CLASS (e_etesync_backend_parent_class)->populate (backend);

	server = e_collection_backend_ref_server (backend);
	old_resources = e_collection_backend_claim_all_resources (backend);

	for (iter = old_resources; iter; iter = g_list_next (iter)) {
		ESource *source = iter->data;

		etesync_backend_update_enabled (source, e_backend_get_source (E_BACKEND (backend)));
		e_source_registry_server_add_source (server, source);
	}

	source = e_backend_get_source (E_BACKEND (backend));
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);

	if (e_source_get_enabled (source) && (
	    e_source_collection_get_calendar_enabled (collection_extension) ||
	    e_source_collection_get_contacts_enabled (collection_extension))) {
		gboolean needs_credentials = TRUE;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
			ESourceAuthentication *auth_extension;
			gchar *method, *user;

			auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
			method = e_source_authentication_dup_method (auth_extension);
			user = e_source_authentication_dup_user (auth_extension);
			needs_credentials = user && *user && g_strcmp0 (method, "EteSync") != 0;
			g_free (method);
			g_free (user);
		}

		if (needs_credentials) {
			e_backend_schedule_credentials_required (E_BACKEND (backend),
				E_SOURCE_CREDENTIALS_REASON_REQUIRED, NULL, 0, NULL, NULL, G_STRFUNC);
		} else {
			e_backend_schedule_authenticate (E_BACKEND (backend), NULL);
		}
	}

	g_object_unref (server);
	g_list_free_full (old_resources, g_object_unref);
}

static ESource *
etesync_backend_new_child (EEteSyncBackend *backend,
			   EteSyncJournal *journal,
			   EteSyncCollectionInfo *info)
{
	ECollectionBackend *collection_backend;
	ESourceExtension *extension;
	ESourceBackend *source_backend;
	ESource *source;
	gchar *journal_uid, *display_name, *type, *description;
	const gchar *extension_name;
	gint32 color;

	journal_uid = etesync_journal_get_uid (journal);
	display_name = etesync_collection_info_get_display_name (info);
	type = etesync_collection_info_get_type (info);
	description = etesync_collection_info_get_description (info);
	color = etesync_collection_info_get_color (info);

	collection_backend = E_COLLECTION_BACKEND (backend);
	source = e_collection_backend_new_child (collection_backend, journal_uid);

	e_source_set_display_name (source, display_name);

	if (g_str_equal (type, ETESYNC_COLLECTION_TYPE_CALENDAR)) {
		extension_name = E_SOURCE_EXTENSION_CALENDAR;
	} else if (g_str_equal (type, ETESYNC_COLLECTION_TYPE_TASKS)) {
		extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	} else if (g_str_equal (type, ETESYNC_COLLECTION_TYPE_ADDRESS_BOOK)) {
		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	} else {
		g_object_unref (source);
		g_return_val_if_reached (NULL);
	}

	extension = e_source_get_extension (source, extension_name);
	source_backend = E_SOURCE_BACKEND (extension);
	e_source_backend_set_backend_name (source_backend, "etesync");
	etesync_backend_update_enabled (source, e_backend_get_source (E_BACKEND (backend)));

	/* Set color for calendar and task only */
	if (!g_str_equal (extension_name, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		if (color) {
			gchar *safe_color;

			safe_color = g_strdup_printf ("#%06X", (0xFFFFFF & color));
			e_source_selectable_set_color (E_SOURCE_SELECTABLE (source_backend), safe_color);

			g_free (safe_color);
		}
	}

	extension_name = E_SOURCE_EXTENSION_ETESYNC;
	extension = e_source_get_extension (source, extension_name);
	e_source_etesync_set_journal_id (
		E_SOURCE_ETESYNC (extension), journal_uid);
	e_source_etesync_set_journal_description (
		E_SOURCE_ETESYNC (extension), description);
	e_source_etesync_set_journal_color (
		E_SOURCE_ETESYNC (extension), color);

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	extension = e_source_get_extension (source, extension_name);
	e_source_offline_set_stay_synchronized (
		E_SOURCE_OFFLINE (extension), TRUE);

	e_server_side_source_set_remote_deletable (
		E_SERVER_SIDE_SOURCE (source), TRUE);

	g_free (journal_uid);
	g_free (display_name);
	g_free (type);
	g_free (description);

	return source;
}

/*
 * This function searches for the corresponding journal using its ID
 * which is the same as the ESource, if found in the hashtable
 * it checks if modified, if not then create the corresponding ESource.
 * It also remove the checked (possibly modified) ESources from the hashtable
 * so what remains is not created or old (modified), means it was deleted.
 * so it should be removed from the collection sources.
 */
static void
etesync_check_create_modify (EEteSyncBackend *backend,
			     EteSyncJournal *journal,
			     EteSyncCollectionInfo *info,
			     GHashTable *known_sources,
			     ECollectionBackend *collection_backend,
			     ESourceRegistryServer *server)
{
	gchar *journal_uid;
	ESource *source;

	g_return_if_fail (E_IS_ETESYNC_BACKEND (backend));
	g_return_if_fail (journal != NULL);

	journal_uid = etesync_journal_get_uid (journal);

	source = g_hash_table_lookup (known_sources, journal_uid);

	/* if exists check if needs modification
	   else create the source */
	if (source != NULL) {
		ESourceExtension *extension;
		ESourceBackend *source_backend;
		gchar *display_name, *description;
		const gchar *extension_name = NULL;
		gint32 color;

		display_name = etesync_collection_info_get_display_name (info);
		description = etesync_collection_info_get_description (info);
		color = etesync_collection_info_get_color (info);

		extension_name = E_SOURCE_EXTENSION_ETESYNC;
		extension = e_source_get_extension (source, extension_name);

		/* Set source data */
		e_source_set_display_name (source, display_name);
		e_source_etesync_set_journal_description ( E_SOURCE_ETESYNC (extension), description);
		e_source_etesync_set_journal_color (E_SOURCE_ETESYNC (extension), color);

		extension_name = NULL;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR))
			extension_name = E_SOURCE_EXTENSION_CALENDAR;
		if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
			extension_name = E_SOURCE_EXTENSION_TASK_LIST;

		/* If extention_name has be set then this is a calendar or task which both have colors
		   address-book doesn't have color */
		if (extension_name) {
			source_backend = e_source_get_extension (source, extension_name);

			if (color) {
				gchar *safe_color;

				safe_color = g_strdup_printf ("#%06X", (0xFFFFFF & color));
				e_source_selectable_set_color (E_SOURCE_SELECTABLE (source_backend), safe_color);

				g_free (safe_color);
			}
		}

		e_server_side_source_set_remote_deletable (
			E_SERVER_SIDE_SOURCE (source), TRUE);

		g_free (display_name);
		g_free (description);
	} else {
		source = etesync_backend_new_child (backend, journal, info);
		e_source_registry_server_add_source (server, source);
		g_object_unref (source);
	}

	g_hash_table_remove (known_sources, journal_uid);
	g_free (journal_uid);
}

static void
etesync_backend_delete_source (gpointer data)
{
	ESource *source = data;

	if (source)
		e_source_remove_sync (source, NULL, NULL);
}

static void
etesync_backend_fill_known_sources (EEteSyncBackend *backend,
				    GHashTable *known_sources /* gchar *folder_id ~> ESource * */)
{
	ECollectionBackend *collection_backend;
	GList *sources, *link;

	g_return_if_fail (E_IS_ETESYNC_BACKEND (backend));
	g_return_if_fail (known_sources != NULL);

	collection_backend = E_COLLECTION_BACKEND (backend);

	sources = e_collection_backend_list_contacts_sources (collection_backend);
	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
			ESourceEteSync *extension;
			gchar *journal_id;

			extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
			journal_id = e_source_etesync_dup_journal_id (extension);

			if (journal_id)
				g_hash_table_insert (known_sources, journal_id, g_object_ref (source));
		}
	}
	g_list_free_full (sources, g_object_unref);

	sources = e_collection_backend_list_calendar_sources (collection_backend);
	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
			ESourceEteSync *extension;
			gchar *journal_id;

			extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
			journal_id = e_source_etesync_dup_journal_id (extension);

			if (journal_id)
				g_hash_table_insert (known_sources, journal_id, g_object_ref (source));
		}
	}

	g_list_free_full (sources, g_object_unref);
}

static gboolean
etesync_backend_sync_folders (EEteSyncBackend *backend,
			      GCancellable *cancellable,
			      GError **error)
{
	EteSyncJournal **journals;
	EEteSyncConnection *connection = backend->priv->connection;
	gboolean success = TRUE;
	g_rec_mutex_lock (&backend->priv->etesync_lock);

	journals =  etesync_journal_manager_list (e_etesync_connection_get_journal_manager (connection));

	/*
	   1) Get all sources in a hashtable.
	   2) loop on the journals, check if it is new (create), or old (maybe modified).
	   3) remove these two from the hashtable while looping.
	   4) what is left in the hashtable should be deleted.
	*/

	if (journals) {
		gpointer key = NULL, value = NULL;
		ECollectionBackend *collection_backend;
		EteSyncJournal **journal_iter;
		ESourceRegistryServer *server;
		GHashTable *known_sources; /* Journal ID -> ESource */
		GHashTableIter iter;

		collection_backend = E_COLLECTION_BACKEND (backend);
		server = e_collection_backend_ref_server (collection_backend);
		known_sources =  g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		etesync_backend_fill_known_sources (backend, known_sources);

		for (journal_iter = journals ;*journal_iter ;journal_iter++) {
			EteSyncJournal *journal = *journal_iter;
			EteSyncCryptoManager *crypto_manager;
			EteSyncCollectionInfo *info;

			crypto_manager = etesync_journal_get_crypto_manager (journal,
					 e_etesync_connection_get_derived_key (connection),
					 e_etesync_connection_get_keypair (connection));
			info = etesync_journal_get_info (journal, crypto_manager);

			etesync_check_create_modify (backend, journal, info, known_sources, collection_backend, server);

			etesync_collection_info_destroy (info);
			etesync_crypto_manager_destroy (crypto_manager);
			etesync_journal_destroy (journal);
		}

		/* this is step 4 */
		g_hash_table_iter_init (&iter, known_sources);
		while (g_hash_table_iter_next (&iter, &key, &value))
			etesync_backend_delete_source (value);

		g_hash_table_destroy (known_sources);
		g_free (journals);
		g_object_unref (server);
	} else {
		/* error 500 or 503 */
		success = FALSE;
	}

	g_rec_mutex_unlock (&backend->priv->etesync_lock);

	return success;
}

static gboolean
etesync_backend_create_resource_sync (ECollectionBackend *backend,
				      ESource *source,
				      GCancellable *cancellable,
				      GError **error)
{
	EEteSyncConnection *connection;
	EEteSyncBackend *etesync_backend;
	EteSyncJournal *new_journal = NULL;
	const gchar *extension_name = NULL;
	const gchar *journal_type = NULL;
	gboolean success = TRUE;

	etesync_backend = E_ETESYNC_BACKEND (backend);

	g_return_val_if_fail (etesync_backend->priv->connection != NULL, FALSE);

	g_rec_mutex_lock (&etesync_backend->priv->etesync_lock);

	connection = etesync_backend->priv->connection;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		journal_type = ETESYNC_COLLECTION_TYPE_ADDRESS_BOOK;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR)) {
		extension_name = E_SOURCE_EXTENSION_CALENDAR;
		journal_type = ETESYNC_COLLECTION_TYPE_CALENDAR;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST)) {
		extension_name = E_SOURCE_EXTENSION_TASK_LIST;
		journal_type = ETESYNC_COLLECTION_TYPE_TASKS;
	}

	if (journal_type == NULL) {
		success = FALSE;
	}

	if (success) {
		gchar *display_name = NULL;
		gint32 color = ETESYNC_COLLECTION_DEFAULT_COLOR;

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
								     ETESYNC_SYNC_ENTRY_ACTION_ADD,
								     NULL,
								     journal_type,
								     display_name,
								     NULL,
								     color,
								     &new_journal);

		g_free (display_name);
	}

	if (success) {
		ESourceRegistryServer *server;
		EteSyncCryptoManager *crypto_manager;
		EteSyncCollectionInfo *info;
		ESource *new_source;

		crypto_manager = etesync_journal_get_crypto_manager (new_journal,
				e_etesync_connection_get_derived_key (connection),
				e_etesync_connection_get_keypair (connection));
		info = etesync_journal_get_info (new_journal, crypto_manager);

		new_source = etesync_backend_new_child (etesync_backend, new_journal, info);

		server = e_collection_backend_ref_server (backend);
		e_source_registry_server_add_source (server, new_source);

		etesync_collection_info_destroy (info);
		etesync_crypto_manager_destroy (crypto_manager);
		g_object_unref (new_source);
		g_object_unref (server);
	}

	if (new_journal)
		etesync_journal_destroy (new_journal);

	g_rec_mutex_unlock (&etesync_backend->priv->etesync_lock);

	return success;
}

static gboolean
etesync_backend_delete_resource_sync (ECollectionBackend *backend,
				      ESource *source,
				      GCancellable *cancellable,
				      GError **error)
{
	EEteSyncBackend *etesync_backend;
	ESourceEteSync *extension;
	gchar *journal_uid;
	gboolean success;

	etesync_backend = E_ETESYNC_BACKEND (backend);

	g_return_val_if_fail (etesync_backend->priv->connection != NULL, FALSE);

	g_rec_mutex_lock (&etesync_backend->priv->etesync_lock);

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
	journal_uid = e_source_etesync_dup_journal_id (extension);

	success = e_etesync_connection_write_journal_action (etesync_backend->priv->connection,
							     ETESYNC_SYNC_ENTRY_ACTION_DELETE,
							     journal_uid, NULL, NULL, NULL, 0, NULL);

	if (success)
		etesync_backend_delete_source (source);

	g_free (journal_uid);
	g_rec_mutex_unlock (&etesync_backend->priv->etesync_lock);

	return success;
}

static ESourceAuthenticationResult
etesync_backend_set_credentials_sync (EEteSyncBackend *backend,
				      const ENamedParameters *credentials)
{
	ESource *source;
	ESourceAuthentication *authentication_extension;
	ESourceCollection *collection_extension;
	EEteSyncConnection *connection = backend->priv->connection;

	source = e_backend_get_source (E_BACKEND (backend));
	authentication_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);

	return e_etesync_connection_set_connection_from_sources (connection, credentials, authentication_extension, collection_extension);
}

static void
etesync_backend_setup_connection (EEteSyncBackend *etesync_backend)
{
	EBackend *backend;
	ESource *source;
	const gchar *username = NULL, *server_url = NULL;

	backend = E_BACKEND (etesync_backend);
	source = e_backend_get_source (backend);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION)) {
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
		server_url = e_source_collection_get_calendar_url (collection_extension);
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *authentication_extension;

		authentication_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		username = e_source_authentication_get_user (authentication_extension);
	}

	etesync_backend->priv->connection = e_etesync_connection_new_from_user_url (username, server_url);
}

static ESourceAuthenticationResult
etesync_backend_authenticate_sync (EBackend *backend,
				   const ENamedParameters *credentials,
				   gchar **out_certificate_pem,
				   GTlsCertificateFlags *out_certificate_errors,
				   GCancellable *cancellable,
				   GError **error)
{
	EEteSyncBackend *etesync_backend;
	EEteSyncConnection *connection;
	ESourceAuthenticationResult result;

	g_return_val_if_fail (E_IS_ETESYNC_BACKEND (backend), E_SOURCE_AUTHENTICATION_ERROR);

	etesync_backend = E_ETESYNC_BACKEND (backend);

	g_rec_mutex_lock (&etesync_backend->priv->etesync_lock);

	if (!etesync_backend->priv->connection)
		etesync_backend_setup_connection (etesync_backend);

	if (etesync_backend->priv->connection) {
		gboolean needs_setting;

		connection = etesync_backend->priv->connection;
		/* Get data from credentials if not there already */
		needs_setting = e_etesync_connection_needs_setting (connection, credentials, &result);

		if (needs_setting)
			result = etesync_backend_set_credentials_sync (etesync_backend, credentials);

		if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
			/* Getting the journals here, and updating the Sources */
			if (etesync_backend_sync_folders (etesync_backend, cancellable, error))
				e_collection_backend_authenticate_children (E_COLLECTION_BACKEND (backend), credentials);
			else
				result = E_SOURCE_AUTHENTICATION_ERROR;
		}
	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	g_rec_mutex_unlock (&etesync_backend->priv->etesync_lock);

	return result;
}

static void
etesync_backend_finalize (GObject *object)
{
	EEteSyncBackendPrivate *priv = e_etesync_backend_get_instance_private (E_ETESYNC_BACKEND (object));

	g_rec_mutex_lock (&priv->etesync_lock);
	g_clear_object (&priv->connection);
	g_rec_mutex_unlock (&priv->etesync_lock);

	g_rec_mutex_clear (&priv->etesync_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_etesync_backend_parent_class)->finalize (object);
}

static void
etesync_backend_constructed (GObject *object)
{
	EBackend *backend;
	ESource *source;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_etesync_backend_parent_class)->constructed (object);

	backend = E_BACKEND (object);
	source = e_backend_get_source (backend);

	e_server_side_source_set_remote_creatable (
		E_SERVER_SIDE_SOURCE (source), TRUE);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION)) {
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
		e_source_collection_set_allow_sources_rename (collection_extension, TRUE);
	}
}

static void
e_etesync_backend_class_init (EEteSyncBackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECollectionBackendClass *collection_backend_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = etesync_backend_finalize;
	object_class->constructed = etesync_backend_constructed;

	collection_backend_class = E_COLLECTION_BACKEND_CLASS (class);
	collection_backend_class->populate = etesync_backend_populate;
	collection_backend_class->dup_resource_id = etesync_backend_dup_resource_id;
	collection_backend_class->create_resource_sync = etesync_backend_create_resource_sync;
	collection_backend_class->delete_resource_sync = etesync_backend_delete_resource_sync;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = etesync_backend_authenticate_sync;
}

static void
e_etesync_backend_init (EEteSyncBackend *backend)
{
	backend->priv = e_etesync_backend_get_instance_private (backend);
	g_rec_mutex_init (&backend->priv->etesync_lock);
}
