/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-etesync-backend.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <etebase.h>

#include "e-etesync-backend.h"
#include "common/e-source-etesync.h"
#include "common/e-source-etesync-account.h"
#include "common/e-etesync-connection.h"
#include "common/e-etesync-defines.h"
#include "common/e-etesync-service.h"
#include "common/e-etesync-utils.h"

static gulong source_removed_handler_id = 0;
static gint backend_count = 0;
G_LOCK_DEFINE_STATIC (backend_count);

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

	return e_source_etesync_dup_collection_id (extension);
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
	    e_source_has_extension (data_source, E_SOURCE_EXTENSION_MEMO_LIST) ||
	    e_source_has_extension (data_source, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		part_enabled = !collection_extension ||
				e_source_collection_get_calendar_enabled (collection_extension) ||
				e_source_collection_get_contacts_enabled (collection_extension);
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
			   const EtebaseCollection *col_obj,
			   EtebaseItemMetadata *item_metadata)
{
	ECollectionBackend *collection_backend;
	ESourceExtension *extension;
	ESourceBackend *source_backend;
	ESource *source;
	gchar *col_obj_b64 =  NULL, *type = NULL;
	const gchar *collection_uid, *display_name, *description, *color;
	const gchar *extension_name;

	collection_uid = etebase_collection_get_uid (col_obj);
	type = etebase_collection_get_collection_type (col_obj);
	display_name = etebase_item_metadata_get_name (item_metadata);
	description = etebase_item_metadata_get_description (item_metadata);
	color = etebase_item_metadata_get_color (item_metadata);

	collection_backend = E_COLLECTION_BACKEND (backend);
	source = e_collection_backend_new_child (collection_backend, collection_uid);

	e_source_set_display_name (source, display_name);

	if (g_str_equal (type, E_ETESYNC_COLLECTION_TYPE_CALENDAR)) {
		extension_name = E_SOURCE_EXTENSION_CALENDAR;
	} else if (g_str_equal (type, E_ETESYNC_COLLECTION_TYPE_TASKS)) {
		extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	} else if (g_str_equal (type, E_ETESYNC_COLLECTION_TYPE_ADDRESS_BOOK)) {
		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	} else if (g_str_equal (type, E_ETESYNC_COLLECTION_TYPE_NOTES)) {
		extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
	} else {
		g_object_unref (source);
		return NULL;
	}

	extension = e_source_get_extension (source, extension_name);
	source_backend = E_SOURCE_BACKEND (extension);
	col_obj_b64 = e_etesync_utils_etebase_collection_to_base64 (
					col_obj,
					e_etesync_connection_get_collection_manager (backend->priv->connection));
	e_source_backend_set_backend_name (source_backend, "etesync");
	etesync_backend_update_enabled (source, e_backend_get_source (E_BACKEND (backend)));

	/* Set color for calendar and task only */
	if (!g_str_equal (extension_name, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		if (color && !(color[0] == 0)) {
			gchar *safe_color;

			/* Copying first 7 chars as color is stored in format #RRGGBBAA */
			safe_color = g_strndup (color, 7);
			e_source_selectable_set_color (E_SOURCE_SELECTABLE (source_backend), safe_color);

			g_free (safe_color);
		} else
			e_source_selectable_set_color (E_SOURCE_SELECTABLE (source_backend), E_ETESYNC_COLLECTION_DEFAULT_COLOR);

	}

	extension_name = E_SOURCE_EXTENSION_ETESYNC;
	extension = e_source_get_extension (source, extension_name);
	e_source_etesync_set_collection_id (
		E_SOURCE_ETESYNC (extension), collection_uid);
	e_source_etesync_set_collection_description (
		E_SOURCE_ETESYNC (extension), description);
	e_source_etesync_set_collection_color (
		E_SOURCE_ETESYNC (extension), color);
	e_source_etesync_set_etebase_collection_b64 (
		E_SOURCE_ETESYNC (extension), col_obj_b64);

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	extension = e_source_get_extension (source, extension_name);
	e_source_offline_set_stay_synchronized (
		E_SOURCE_OFFLINE (extension), TRUE);

	e_server_side_source_set_remote_deletable (
		E_SERVER_SIDE_SOURCE (source), TRUE);

	g_free (col_obj_b64);
	g_free (type);

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
			     const EtebaseCollection *col_obj,
			     EtebaseItemMetadata *item_metadata,
			     ESource *source,
			     ECollectionBackend *collection_backend,
			     ESourceRegistryServer *server)
{
	g_return_if_fail (E_IS_ETESYNC_BACKEND (backend));
	g_return_if_fail (col_obj != NULL);

	/* if exists check if needs modification
	   else create the source */
	if (source != NULL) {
		ESourceExtension *extension;
		ESourceBackend *source_backend;
		const gchar *display_name, *description;
		const gchar *extension_name = NULL;
		const gchar *color;

		display_name = etebase_item_metadata_get_name (item_metadata);
		description = etebase_item_metadata_get_description (item_metadata);
		color = etebase_item_metadata_get_color (item_metadata);

		extension_name = E_SOURCE_EXTENSION_ETESYNC;
		extension = e_source_get_extension (source, extension_name);

		/* Set source data */
		e_source_set_display_name (source, display_name);
		e_source_etesync_set_collection_description ( E_SOURCE_ETESYNC (extension), description);
		e_source_etesync_set_collection_color (E_SOURCE_ETESYNC (extension), color);

		extension_name = NULL;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR))
			extension_name = E_SOURCE_EXTENSION_CALENDAR;
		if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
			extension_name = E_SOURCE_EXTENSION_TASK_LIST;
		if (e_source_has_extension (source, E_SOURCE_EXTENSION_MEMO_LIST))
			extension_name = E_SOURCE_EXTENSION_MEMO_LIST;

		/* If extention_name has be set then this is a calendar or task or a memo which the three have colors
		   address-book doesn't have color */
		if (extension_name) {
			source_backend = e_source_get_extension (source, extension_name);

			if (color && *color) {
				gchar *safe_color;

				/* Copying first 7 chars as color is stored in format #RRGGBBAA */
				safe_color = g_strndup (color, 7);
				e_source_selectable_set_color (E_SOURCE_SELECTABLE (source_backend), safe_color);

				g_free (safe_color);
			} else
				e_source_selectable_set_color (E_SOURCE_SELECTABLE (source_backend), E_ETESYNC_COLLECTION_DEFAULT_COLOR);
		}

	} else {
		source = etesync_backend_new_child (backend, col_obj, item_metadata);

		if (source) {
			e_source_registry_server_add_source (server, source);
			g_object_unref (source);
		}
	}
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
			gchar *collection_id;

			extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
			collection_id = e_source_etesync_dup_collection_id (extension);

			if (collection_id)
				g_hash_table_insert (known_sources, collection_id, g_object_ref (source));
		}

		e_server_side_source_set_remote_deletable (
			E_SERVER_SIDE_SOURCE (source), TRUE);
	}
	g_list_free_full (sources, g_object_unref);

	sources = e_collection_backend_list_calendar_sources (collection_backend);
	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
			ESourceEteSync *extension;
			gchar *collection_id;

			extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
			collection_id = e_source_etesync_dup_collection_id (extension);

			if (collection_id)
				g_hash_table_insert (known_sources, collection_id, g_object_ref (source));
		}

		e_server_side_source_set_remote_deletable (
			E_SERVER_SIDE_SOURCE (source), TRUE);
	}

	g_list_free_full (sources, g_object_unref);
}

/* Creates a default EtebaseCollection and upload it to the server, then
   adds it to Evolution EteSync account */
static const gchar*
etesync_backend_create_and_add_collection_sync (EEteSyncBackend *backend,
						ESourceRegistryServer *server,
						const gchar *type,
						const gchar *name,
						GCancellable *cancellable)
{
	EBackend *e_backend = E_BACKEND (backend);
	EtebaseCollection *col_obj;
	const gchar* stoken = NULL;

	if (e_etesync_connection_collection_create_upload_sync (backend->priv->connection, e_backend,
		type, name, NULL, E_ETESYNC_COLLECTION_DEFAULT_COLOR, &col_obj, cancellable, NULL)) {

		ESource *source;
		EtebaseItemMetadata *item_metadata;

		item_metadata = etebase_collection_get_meta (col_obj);
		source = etesync_backend_new_child (backend, col_obj, item_metadata);

		if (source) {
			e_source_registry_server_add_source (server, source);
			g_object_unref (source);
		}

		if (col_obj) {
			etebase_collection_destroy (col_obj);
			stoken = etebase_collection_get_stoken (col_obj);
		}
		if (item_metadata)
			etebase_item_metadata_destroy (item_metadata);
	}

	return stoken;
}

static gboolean
etesync_backend_sync_folders_sync (EEteSyncBackend *backend,
				   gboolean check_rec,
				   GCancellable *cancellable,
				   GError **error)
{
	EEteSyncConnection *connection = backend->priv->connection;
	ECollectionBackend *collection_backend;
	ESourceEteSyncAccount *etesync_account_extention;
	ESourceRegistryServer *server;
	EtebaseFetchOptions *fetch_options;
	GHashTable *known_sources; /* Collection ID -> ESource */
	gboolean success = TRUE, done = FALSE, is_first_time = FALSE;
	gchar *stoken = NULL;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_rec_mutex_lock (&backend->priv->etesync_lock);

	collection_backend = E_COLLECTION_BACKEND (backend);
	server = e_collection_backend_ref_server (collection_backend);
	etesync_account_extention = e_source_get_extension (e_backend_get_source (E_BACKEND (backend)), E_SOURCE_EXTENSION_ETESYNC_ACCOUNT);
	fetch_options = etebase_fetch_options_new();
	known_sources =  g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	/*
		1) Get all known-sources (loaded in evo before) in a hashtable to easily lookup.
		2) Keep getting list of changes until "done" is true
		3) From the list we got, delete removed membership collection sources.
		4) loop on the collections, check if it is deleted
		5) if not deleted then check if it is new (create), or old (maybe modified).
		6) remove what is deleted or has removed member-ship
	*/

	etebase_fetch_options_set_prefetch(fetch_options, ETEBASE_PREFETCH_OPTION_MEDIUM);
	etebase_fetch_options_set_limit (fetch_options, E_ETESYNC_COLLECTION_FETCH_LIMIT);
	etesync_backend_fill_known_sources (backend, known_sources); /* (1) */

	stoken = g_hash_table_size (known_sources) > 0 ? e_source_etesync_account_dup_collection_stoken (etesync_account_extention) : NULL;

	/* if stoken is null, then it is first time to fetch data from server */
	if (!stoken)
		is_first_time = TRUE;

	while (!done) {
		EtebaseCollectionListResponse *col_list;

		etebase_fetch_options_set_stoken (fetch_options, stoken);
		col_list =  etebase_collection_manager_list_multi (e_etesync_connection_get_collection_manager (connection),
								   e_etesync_util_get_collection_supported_types (),
								   EETESYNC_UTILS_SUPPORTED_TYPES_SIZE,
								   fetch_options); /* (2) */

		if (col_list) {
			guintptr col_objs_len = etebase_collection_list_response_get_data_length (col_list);
			const EtebaseCollection *col_objs[col_objs_len];
			guintptr col_iter, col_list_rmv_membership_len;

			g_free (stoken);

			stoken = g_strdup (etebase_collection_list_response_get_stoken (col_list));
			done = etebase_collection_list_response_is_done (col_list);
			col_list_rmv_membership_len = etebase_collection_list_response_get_removed_memberships_length (col_list);

			etebase_collection_list_response_get_data (col_list, col_objs);

			if (col_list_rmv_membership_len > 0) { /* (3) */
				const EtebaseRemovedCollection *col_list_rmv_membership[col_list_rmv_membership_len];

				/* Creating a hashed table to easily look up if a collection has removed membership or not */
				if (etebase_collection_list_response_get_removed_memberships (col_list, col_list_rmv_membership) == 0) {

					for (col_iter = 0; col_iter < col_list_rmv_membership_len; col_iter++) {
						ESource *source = g_hash_table_lookup (known_sources, etebase_removed_collection_get_uid (col_list_rmv_membership[col_iter]));

						if (source)
							etesync_backend_delete_source (source);
					}
				} else {
					success = FALSE;
					etebase_collection_list_response_destroy (col_list);
					break;
				}
			}

			/* Loop each collection to see should we remove or check it, depending if it is deleted or removed membership */
			for (col_iter = 0; col_iter < col_objs_len; col_iter++) { /* (4) */
				ESource *source;
				const EtebaseCollection *col_obj;
				const gchar *collection_uid;

				col_obj = col_objs[col_iter];
				collection_uid = etebase_collection_get_uid (col_obj);
				source = g_hash_table_lookup (known_sources, collection_uid);

				if (!etebase_collection_is_deleted (col_obj)) { /* (5) */
					EtebaseItemMetadata *item_metadata;

					item_metadata = etebase_collection_get_meta (col_obj);
					etesync_check_create_modify (backend, col_obj, item_metadata, source, collection_backend, server);

					etebase_item_metadata_destroy (item_metadata);
				} else /* (6) */
					etesync_backend_delete_source (source);
			}

			etebase_collection_list_response_destroy (col_list);
		} else {
			/* error 500 or 503 */
			success = FALSE;
			break;
		}
	}

	/* If this is the first time to sync, then try fetching supported types, if success then don't create default collections */
	if (is_first_time) {
		EtebaseCollectionListResponse *col_list;
		EtebaseFetchOptions *fetch_options;
		const gchar *const *collection_supported_types;
		const gchar *const *collection_supported_types_default_names;
		gint ii;

		fetch_options = etebase_fetch_options_new();
		etebase_fetch_options_set_prefetch(fetch_options, ETEBASE_PREFETCH_OPTION_MEDIUM);
		etebase_fetch_options_set_stoken (fetch_options, NULL);
		etebase_fetch_options_set_limit (fetch_options, 1);

		collection_supported_types = e_etesync_util_get_collection_supported_types ();
		collection_supported_types_default_names = e_etesync_util_get_collection_supported_types_default_names ();

		/* Check default type (contacts, calendar and tasks)
		   First three types in `collection_supported_types` are contacts, calendar and tasks */
		col_list =  etebase_collection_manager_list_multi (e_etesync_connection_get_collection_manager (connection),
								   collection_supported_types,
								   3,
								   fetch_options);

		if (etebase_collection_list_response_get_data_length (col_list) == 0) {
			const gchar *temp_stoken;

			for (ii = COLLECTION_INDEX_TYPE_ADDRESSBOOK; ii <= COLLECTION_INDEX_TYPE_TASKS; ii++)
				temp_stoken = etesync_backend_create_and_add_collection_sync (backend, server, collection_supported_types[ii],
											      collection_supported_types_default_names[ii], cancellable);

			if (temp_stoken) {
				g_free (stoken);
				stoken = g_strdup (temp_stoken);
			}
		}

		/* Check (Notes) type */
		etebase_collection_list_response_destroy (col_list);
		col_list = etebase_collection_manager_list (e_etesync_connection_get_collection_manager (connection),
							    E_ETESYNC_COLLECTION_TYPE_NOTES,
							    fetch_options);

		if (etebase_collection_list_response_get_data_length (col_list) == 0) {
			const gchar *temp_stoken;

			temp_stoken = etesync_backend_create_and_add_collection_sync (backend, server, collection_supported_types[COLLECTION_INDEX_TYPE_NOTES],
										      collection_supported_types_default_names[COLLECTION_INDEX_TYPE_NOTES], cancellable);

			if (temp_stoken) {
				g_free (stoken);
				stoken = g_strdup (temp_stoken);
			}
		}

		etebase_collection_list_response_destroy (col_list);
		etebase_fetch_options_destroy (fetch_options);
	}

	if (success)
		e_source_etesync_account_set_collection_stoken (etesync_account_extention, stoken);
	else {
		EtebaseErrorCode etebase_error = etebase_error_get_code ();

		e_etesync_utils_set_io_gerror (etebase_error, etebase_error_get_message (), error);
		if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED && check_rec) {
			EBackend *e_backend = E_BACKEND (backend);

			if (e_etesync_connection_maybe_reconnect_sync (connection, e_backend, cancellable, error))
				success = etesync_backend_sync_folders_sync (backend, FALSE, cancellable, error);
		}
	}

	g_object_unref (server);
	g_hash_table_destroy (known_sources);
	etebase_fetch_options_destroy (fetch_options);
	g_free (stoken);

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
	EtebaseCollection *new_col_obj = NULL;
	const gchar *extension_name = NULL;
	const gchar *col_type = NULL;
	gboolean success = TRUE;

	etesync_backend = E_ETESYNC_BACKEND (backend);

	g_return_val_if_fail (etesync_backend->priv->connection != NULL, FALSE);

	g_rec_mutex_lock (&etesync_backend->priv->etesync_lock);

	connection = etesync_backend->priv->connection;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		col_type = E_ETESYNC_COLLECTION_TYPE_ADDRESS_BOOK;
	} else if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR)) {
		extension_name = E_SOURCE_EXTENSION_CALENDAR;
		col_type = E_ETESYNC_COLLECTION_TYPE_CALENDAR;
	} else if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST)) {
		extension_name = E_SOURCE_EXTENSION_TASK_LIST;
		col_type = E_ETESYNC_COLLECTION_TYPE_TASKS;
	} else if (e_source_has_extension (source, E_SOURCE_EXTENSION_MEMO_LIST)) {
		extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
		col_type = E_ETESYNC_COLLECTION_TYPE_NOTES;
	}

	if (col_type == NULL) {
		success = FALSE;
	}

	if (success) {
		gchar *display_name = NULL;
		gchar *color = NULL;
		EBackend *e_backend = E_BACKEND (backend);

		if (!g_str_equal (extension_name, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
			ESourceBackend *source_backend;
			const gchar *source_color = NULL;

			source_backend = e_source_get_extension (source, extension_name);
			source_color = e_source_selectable_get_color (E_SOURCE_SELECTABLE (source_backend));

			if (source_color) {
				g_free (color);
				color = g_strdup (source_color);
			}
		}

		display_name = e_source_dup_display_name (source);

		success = e_etesync_connection_collection_create_upload_sync (connection,
									      e_backend,
									      col_type,
									      display_name,
									      NULL,
									      color ? color : E_ETESYNC_COLLECTION_DEFAULT_COLOR,
									      &new_col_obj,
									      cancellable,
									      error);

		g_free (display_name);
		g_free (color);
	}

	if (success) {
		ESourceRegistryServer *server;
		EtebaseItemMetadata *item_metadata;
		ESource *new_source;

		item_metadata = etebase_collection_get_meta (new_col_obj);

		new_source = etesync_backend_new_child (etesync_backend, new_col_obj, item_metadata);

		server = e_collection_backend_ref_server (backend);
		e_source_registry_server_add_source (server, new_source);

		etebase_item_metadata_destroy (item_metadata);
		g_object_unref (new_source);
		g_object_unref (server);
	}

	if (new_col_obj)
		etebase_collection_destroy (new_col_obj);

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
	EBackend *e_backend;
	EEteSyncConnection *connection;
	ESourceEteSync *extension;
	EtebaseCollection *col_obj;
	gboolean success;

	etesync_backend = E_ETESYNC_BACKEND (backend);
	e_backend = E_BACKEND (backend);

	g_return_val_if_fail (etesync_backend->priv->connection != NULL, FALSE);

	g_rec_mutex_lock (&etesync_backend->priv->etesync_lock);

	connection = etesync_backend->priv->connection;
	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
	col_obj = e_etesync_utils_etebase_collection_from_base64 (
					e_source_etesync_get_etebase_collection_b64 (extension),
					e_etesync_connection_get_collection_manager (connection));

	success = e_etesync_connection_collection_delete_upload_sync (connection, e_backend, col_obj, cancellable, error);

	if (success)
		etesync_backend_delete_source (source);

	if (col_obj)
		etebase_collection_destroy (col_obj);

	g_rec_mutex_unlock (&etesync_backend->priv->etesync_lock);

	return success;
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
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ERROR;

	g_return_val_if_fail (E_IS_ETESYNC_BACKEND (backend), E_SOURCE_AUTHENTICATION_ERROR);

	etesync_backend = E_ETESYNC_BACKEND (backend);

	g_rec_mutex_lock (&etesync_backend->priv->etesync_lock);

	if (e_etesync_connection_is_connected (etesync_backend->priv->connection))
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	else {
		ESource *source = e_backend_get_source (backend);

		if (!etesync_backend->priv->connection)
			etesync_backend->priv->connection = e_etesync_connection_new (source);

		if (e_etesync_connection_reconnect_sync (etesync_backend->priv->connection, &result, cancellable, error))
			result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	}

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		/* Getting the journals here, and updating the Sources */
		if (etesync_backend_sync_folders_sync (etesync_backend, TRUE, cancellable, error))
			e_collection_backend_authenticate_children (E_COLLECTION_BACKEND (backend), credentials);
		else
			result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	g_rec_mutex_unlock (&etesync_backend->priv->etesync_lock);

	return result;
}

/* This function is a call back for "source-removed" signal, it makes sure
   that the account logs-out after being removed */
static void
etesync_backend_source_removed_cb (ESourceRegistryServer *server,
				   ESource *source,
				   gpointer user_data)
{
	/* Checking if it is a collection and is an EteSync collection */
	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION) &&
	    e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC_ACCOUNT)) {

		EEteSyncConnection *connection;
		ENamedParameters *credentials;
		const gchar *collection_uid;

		collection_uid = e_source_get_uid (source);
		connection = e_etesync_connection_new (source);

		/* Get credentials and set connection object, then use the connection object to logout */
		if (e_etesync_service_lookup_credentials_sync (collection_uid, &credentials, NULL, NULL) &&
		    e_etesync_connection_set_connection_from_sources (connection, credentials)) {

			etebase_account_logout (e_etesync_connection_get_etebase_account (connection));
		}

		g_object_unref (connection);
		e_named_parameters_free (credentials);
	}
}

static void
etesync_backend_dispose (GObject *object)
{
	ESourceRegistryServer *server;

	server = e_collection_backend_ref_server (E_COLLECTION_BACKEND (object));

	/* Only disconnect when backend_count is zero */
	G_LOCK (backend_count);
	if (server && !--backend_count) {
		g_signal_handler_disconnect (
			server, source_removed_handler_id);

		backend_count = 0;
	}
	G_UNLOCK (backend_count);

	g_object_unref (server);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_etesync_backend_parent_class)->dispose (object);
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
	EEteSyncBackend *etesync_backend;
	ESourceRegistryServer *server;
	ESource *source;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_etesync_backend_parent_class)->constructed (object);

	backend = E_BACKEND (object);
	etesync_backend = E_ETESYNC_BACKEND (object);
	server = e_collection_backend_ref_server (E_COLLECTION_BACKEND (backend));
	source = e_backend_get_source (backend);
	etesync_backend->priv->connection = e_etesync_connection_new (source);

	e_server_side_source_set_remote_creatable (
		E_SERVER_SIDE_SOURCE (source), TRUE);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION)) {
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
		e_source_collection_set_allow_sources_rename (collection_extension, TRUE);
	}

	G_LOCK (backend_count);
	if (!backend_count++) {
		source_removed_handler_id = g_signal_connect (
					server, "source-removed",
					G_CALLBACK (etesync_backend_source_removed_cb), NULL);
	}
	G_UNLOCK (backend_count);

	g_object_unref (server);
}

static void
e_etesync_backend_class_init (EEteSyncBackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECollectionBackendClass *collection_backend_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = etesync_backend_dispose;
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
