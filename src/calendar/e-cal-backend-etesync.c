/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cal-backend-etesync.c - EteSync calendar backend.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <libedataserver/libedataserver.h>
#include <etesync.h>

#include "common/e-source-etesync.h"
#include "common/e-etesync-connection.h"
#include "e-cal-backend-etesync.h"

#define FETCH_BATCH_LIMIT 50
#define PUSH_BATCH_LIMIT 30

static gulong source_changed_handler_id = 0;
static gint backend_count = 0;
G_LOCK_DEFINE_STATIC (backend_count);

struct _ECalBackendEteSyncPrivate {
	EEteSyncConnection *connection;
	GRecMutex etesync_lock;
	gchar *journal_id;

	gboolean fetch_from_server;

	gchar *preloaded_sync_tag;
	GSList *preloaded_add; /* ECalMetaBackendInfo * */
	GSList *preloaded_modify; /* ECalMetaBackendInfo * */
	GSList *preloaded_delete; /* ECalMetaBackendInfo * */
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendEteSync, e_cal_backend_etesync, E_TYPE_CAL_META_BACKEND)

static void
ecb_etesync_setup_connection (ECalBackendEteSync *cbetesync)
{
	ESource *source, *collection;
	ESourceRegistry *registry;

	source = e_backend_get_source (E_BACKEND (cbetesync));
	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbetesync));
	collection = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
		ESourceEteSync *etesync_extension;

		etesync_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
		g_free (cbetesync->priv->journal_id);
		cbetesync->priv->journal_id = e_source_etesync_dup_journal_id (etesync_extension);
	}

	if (collection) {
		cbetesync->priv->connection = e_etesync_connection_new_connection (collection);
		g_clear_object (&collection);
	}
}

/*
 * Should check if credentials exist, if so should validate them
 * to make sure credentials haven't changed, then copy them to the private
 */
static ESourceAuthenticationResult
ecb_etesync_set_credentials_sync (ECalBackendEteSync *cbetesync,
				  const ENamedParameters *credentials)
{
	ESource *source;
	ESource *collection;
	ESourceRegistry *registry;

	source = e_backend_get_source (E_BACKEND (cbetesync));
	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbetesync));
	collection = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (collection) {
		ESourceAuthentication *authentication_extension;
		ESourceCollection *collection_extension;

		authentication_extension = e_source_get_extension (collection, E_SOURCE_EXTENSION_AUTHENTICATION);
		collection_extension = e_source_get_extension (collection, E_SOURCE_EXTENSION_COLLECTION);

		g_clear_object (&collection);

		g_return_val_if_fail (authentication_extension != NULL, E_SOURCE_AUTHENTICATION_ERROR);
		g_return_val_if_fail (collection_extension != NULL, E_SOURCE_AUTHENTICATION_ERROR);

		return e_etesync_connection_set_connection_from_sources (cbetesync->priv->connection, credentials, authentication_extension, collection_extension);
	}

	return E_SOURCE_AUTHENTICATION_ERROR;
}

static gboolean
ecb_etesync_connect_sync (ECalMetaBackend *meta_backend,
			  const ENamedParameters *credentials,
			  ESourceAuthenticationResult *out_auth_result,
			  gchar **out_certificate_pem,
			  GTlsCertificateFlags *out_certificate_errors,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalBackendEteSync *cbetesync;
	EEteSyncConnection *connection;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	if (!cbetesync->priv->connection)
		ecb_etesync_setup_connection (cbetesync);

	if (cbetesync->priv->connection) {
		gboolean needs_setting;

		connection = cbetesync->priv->connection;
		needs_setting = e_etesync_connection_needs_setting (connection, credentials, out_auth_result);

		if (needs_setting)
			*out_auth_result = ecb_etesync_set_credentials_sync (cbetesync, credentials);

		if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
			gboolean is_read_only;

			success = e_etesync_connection_get_journal_exists (connection, cbetesync->priv->journal_id, &is_read_only);

			if (success)
				e_cal_backend_set_writable (E_CAL_BACKEND (cbetesync), !is_read_only);
		}
	} else {
		*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return success;
}

static gboolean
ecb_etesync_disconnect_sync (ECalMetaBackend *meta_backend,
			     GCancellable *cancellable,
			     GError **error)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);

	return TRUE;
}

static gboolean
ecb_etesync_get_component_uid_revision (const gchar *content,
					gchar **out_component_uid,
					gchar **out_revision)
{
	ICalComponent *vcalendar, *subcomp;
	gboolean success = FALSE;

	vcalendar = i_cal_component_new_from_string (content);

	*out_component_uid = NULL;
	*out_revision = NULL;

	for (subcomp = i_cal_component_get_first_component (vcalendar, I_CAL_ANY_COMPONENT);
	     subcomp;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vcalendar, I_CAL_ANY_COMPONENT)) {

		ICalComponentKind kind = i_cal_component_isa (subcomp);

		if (kind == I_CAL_VEVENT_COMPONENT ||
		    kind == I_CAL_VTODO_COMPONENT) {
			if (!*out_component_uid){
				*out_component_uid = g_strdup (i_cal_component_get_uid (subcomp));
				success = TRUE;
			}

			if (!*out_revision) {
				ICalProperty *prop;

				prop = i_cal_component_get_first_property (vcalendar, I_CAL_LASTMODIFIED_PROPERTY);
				if (prop) {
					ICalTime *itt;

					itt = i_cal_property_get_lastmodified (prop);
					*out_revision = i_cal_time_as_ical_string (itt);
					g_clear_object (&itt);
					g_object_unref (prop);
				} else {
					*out_revision = NULL;
				}
			}
		}
	}

	g_object_unref (vcalendar);

	return success;
}

static void
ecb_etesync_update_hash_tables (GHashTable *main_table,
				GHashTable *table_1,
				GHashTable *table_2,
				const gchar *uid,
				ECalMetaBackendInfo *nfo)
{
	g_hash_table_replace (main_table, g_strdup (uid), nfo);
	g_hash_table_remove (table_1, uid);
	g_hash_table_remove (table_2, uid);
}

static gboolean
ecb_etesync_list_existing_sync (ECalMetaBackend *meta_backend,
				gchar **out_new_sync_tag,
				GSList **out_existing_objects,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendEteSync *cbetesync;
	EteSyncEntry **entries = NULL, **entry_iter;
	EteSyncEntryManager *entry_manager;
	EteSyncCryptoManager *crypto_manager = NULL;
	EEteSyncConnection *connection;
	gchar *prev_entry_uid = NULL;
	GHashTable *existing_entry; /* gchar *uid ~> ECalMetaBackendInfo * */
	GHashTableIter iter;
	gpointer key = NULL, value = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	*out_existing_objects = NULL;

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);
	connection = cbetesync->priv->connection;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* 1) Retrive entry_manager, crypto_manager and entries since "last_sync_tag", here "last_sync_tag" will be NULL
	      Failing means connection error */
	if (!e_etesync_connection_check_journal_changed_sync (connection, cbetesync->priv->journal_id, NULL, FETCH_BATCH_LIMIT, &entries, &crypto_manager, &entry_manager)) {
		g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);
		return FALSE;
	}

	/* 2) Check if there are entries returned from "e_etesync_connection_check_journal_changed_sync",
	      if it is empty then no changes since "last_sync_tag", just free the memory and return, here "last_sync_tag" will be NULL */
	if (!entries || !*entries) {
		g_free (entries);
		if (entry_manager)
			etesync_entry_manager_destroy (entry_manager);
		if (crypto_manager)
			etesync_crypto_manager_destroy (crypto_manager);
		g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);
		return TRUE;
	}

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	/* 3) At this point, entries are not empty, then we should loop on entries and add each
	      one to the hashtable as ECalMetaBackendInfo only keeping last action for each component */
	existing_entry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_cal_meta_backend_info_free);
	prev_entry_uid = NULL;

	while (entries && *entries) {
		gint batch_size = 0;

		for (entry_iter = entries; *entry_iter; entry_iter++, batch_size++) {

			EteSyncEntry *entry;
			EteSyncSyncEntry *sync_entry;
			ECalMetaBackendInfo *nfo;
			gchar *entry_uid, *action, *content, *component_uid, *revision;

			entry = *entry_iter;
			sync_entry = etesync_entry_get_sync_entry (entry, crypto_manager, prev_entry_uid);

			action = etesync_sync_entry_get_action (sync_entry);
			content = etesync_sync_entry_get_content (sync_entry);

			/* create ECalMetaBackendInfo * to be stored in Hash Table */
			ecb_etesync_get_component_uid_revision (content, &component_uid, &revision);
			nfo = e_cal_meta_backend_info_new (component_uid, revision, content, NULL);

			/* check action add, change or delete */
			if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_ADD)
			 || g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_CHANGE))
				g_hash_table_replace (existing_entry, g_strdup (component_uid), nfo);

			else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_DELETE))
				g_hash_table_remove (existing_entry, component_uid);

			else
				e_cal_meta_backend_info_free (nfo);

			entry_uid = etesync_entry_get_uid (entry);
			g_free (prev_entry_uid);
			prev_entry_uid = entry_uid;

			etesync_sync_entry_destroy (sync_entry);
			etesync_entry_destroy (entry);
			g_free (content);
			g_free (action);
			g_free (component_uid);
			g_free (revision);
		}

		if (batch_size < FETCH_BATCH_LIMIT)
			break;

		g_free (entries);
		entries = etesync_entry_manager_list (entry_manager, prev_entry_uid, FETCH_BATCH_LIMIT);
	}

	/* 4) iterate on each hashtable and add to correspoinding GList */
	g_hash_table_iter_init (&iter, existing_entry);
	while (g_hash_table_iter_next (&iter, &key, &value))
		*out_existing_objects = g_slist_prepend (*out_existing_objects, e_cal_meta_backend_info_copy (value));

        g_free (entries);
	g_free (prev_entry_uid);
        etesync_entry_manager_destroy (entry_manager);
        etesync_crypto_manager_destroy (crypto_manager);
	g_hash_table_destroy (existing_entry);

	return TRUE;
}

static gboolean
ecb_etesync_get_changes_sync (ECalMetaBackend *meta_backend,
			      const gchar *last_sync_tag,
			      gboolean is_repeat,
			      gchar **out_new_sync_tag,
			      gboolean *out_repeat,
			      GSList **out_created_objects, /* ECalMetaBackendInfo * */
			      GSList **out_modified_objects, /* ECalMetaBackendInfo * */
			      GSList **out_removed_objects, /* ECalMetaBackendInfo * */
			      GCancellable *cancellable,
			      GError **error)
{
	ECalBackendEteSync *cbetesync;
	EteSyncEntry **entries = NULL, **entry_iter;
	EteSyncCryptoManager *crypto_manager = NULL;
	EteSyncEntryManager *entry_manager;
	EEteSyncConnection *connection;
	gchar *prev_entry_uid = NULL;
	const gchar *last_tag = NULL;
	GHashTable *created_entry, *modified_entry, *removed_entry; /* gchar *uid ~> ECalMetaBackendInfo * */
	GHashTableIter iter;
	gpointer key = NULL, value = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);
	connection = cbetesync->priv->connection;

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* Must add preloaded and updating the out_new_sync_tag */
	if (cbetesync->priv->preloaded_sync_tag) {
		*out_created_objects = cbetesync->priv->preloaded_add;
		*out_modified_objects = cbetesync->priv->preloaded_modify;
		*out_removed_objects = cbetesync->priv->preloaded_delete;
		*out_new_sync_tag = cbetesync->priv->preloaded_sync_tag; /* Made here if no chnages are required to fetch */
		last_tag = cbetesync->priv->preloaded_sync_tag;

		cbetesync->priv->preloaded_sync_tag = NULL;
		cbetesync->priv->preloaded_add = NULL;
		cbetesync->priv->preloaded_modify = NULL;
		cbetesync->priv->preloaded_delete = NULL;
	}

	if (cbetesync->priv->fetch_from_server) {
		if (!last_tag)
			last_tag = last_sync_tag;
		/* 1) Retrive entry_manager, crypto_manager and entries since "last_sync_tag"
		      Failing means connection error */
		if (!e_etesync_connection_check_journal_changed_sync (connection, cbetesync->priv->journal_id, last_sync_tag, FETCH_BATCH_LIMIT, &entries, &crypto_manager, &entry_manager)) {
			g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);
			return FALSE;
		}

		/* 2) Check if there are entries returned from "e_etesync_connection_check_journal_changed_sync",
		      if it is empty then no changes since "last_sync_tag", just free the memory and return */
		if (!entries || !*entries) {
			g_free (entries);
			if (entry_manager)
				etesync_entry_manager_destroy (entry_manager);
			if (crypto_manager)
				etesync_crypto_manager_destroy (crypto_manager);
			g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);
			return TRUE;
		}

		/* 3) At this point, entries are not empty, then we should loop on entries and add each
		      one to its hashtable as ECalMetaBackendInfo depending on the action ("create/modify/delete) */
		created_entry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_cal_meta_backend_info_free);
		modified_entry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_cal_meta_backend_info_free);
		removed_entry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_cal_meta_backend_info_free);
		prev_entry_uid = g_strdup (last_tag);

		while (entries && *entries) {
			gint batch_size = 0;

			for (entry_iter = entries; *entry_iter; entry_iter++, batch_size++) {

				EteSyncEntry *entry;
				EteSyncSyncEntry *sync_entry;
				ECalMetaBackendInfo *nfo;
				gchar *entry_uid, *action, *content, *component_uid, *revision;

				entry = *entry_iter;
				sync_entry = etesync_entry_get_sync_entry (entry, crypto_manager, prev_entry_uid);

				action = etesync_sync_entry_get_action (sync_entry);
				content = etesync_sync_entry_get_content (sync_entry);

				/* create ECalMetaBackendInfo * to be stored in Hash Table */
				ecb_etesync_get_component_uid_revision (content, &component_uid, &revision);
				nfo = e_cal_meta_backend_info_new (component_uid, revision, content, NULL);

				/* check action add, change or delete */
				if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_ADD))
					ecb_etesync_update_hash_tables (created_entry, modified_entry, removed_entry, component_uid, nfo);

				else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_CHANGE))
					ecb_etesync_update_hash_tables (modified_entry, created_entry ,removed_entry, component_uid, nfo);

				else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_DELETE))
					ecb_etesync_update_hash_tables (removed_entry, created_entry, modified_entry, component_uid, nfo);

				else
					e_cal_meta_backend_info_free (nfo);

				entry_uid = etesync_entry_get_uid (entry);
				g_free (prev_entry_uid);
				prev_entry_uid = entry_uid;

				etesync_sync_entry_destroy (sync_entry);
				etesync_entry_destroy (entry);
				g_free (content);
				g_free (action);
				g_free (component_uid);
				g_free (revision);
			}

			if (batch_size < FETCH_BATCH_LIMIT)
				break;

			g_free (entries);
			entries = etesync_entry_manager_list (entry_manager, prev_entry_uid, FETCH_BATCH_LIMIT);
		}

		*out_new_sync_tag = prev_entry_uid;

		/* 4) iterate on each hashtable and add to correspoinding GList */
		g_hash_table_iter_init (&iter, created_entry);
		while (g_hash_table_iter_next (&iter, &key, &value))
			*out_created_objects = g_slist_prepend (*out_created_objects, e_cal_meta_backend_info_copy (value));

		g_hash_table_iter_init (&iter, modified_entry);
		while (g_hash_table_iter_next (&iter, &key, &value))
			*out_modified_objects = g_slist_prepend (*out_modified_objects, e_cal_meta_backend_info_copy (value));

		g_hash_table_iter_init (&iter, removed_entry);
		while (g_hash_table_iter_next (&iter, &key, &value))
			*out_removed_objects = g_slist_prepend (*out_removed_objects, e_cal_meta_backend_info_copy (value));

		g_free (entries);
		etesync_entry_manager_destroy (entry_manager);
		etesync_crypto_manager_destroy (crypto_manager);
		g_hash_table_destroy (created_entry);
		g_hash_table_destroy (modified_entry);
		g_hash_table_destroy (removed_entry);

	}

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return TRUE;
}

static gboolean
ecb_etesync_load_component_sync (ECalMetaBackend *meta_backend,
				 const gchar *uid,
				 const gchar *extra,
				 ICalComponent **out_component,
				 gchar **out_extra,
				 GCancellable *cancellable,
				 GError **error)
{
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	/* 1) Call e_cal_meta_backend_refresh_sync() to get components since last_tag */
	if (e_cal_meta_backend_refresh_sync (meta_backend, cancellable, error)) {
		ECalCache *cal_cache;
		GSList *components = NULL;

		/* 2) Search cache data for the required component */
		cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

		if (cal_cache) {
			if (e_cal_cache_get_components_by_uid (cal_cache, uid, &components, cancellable, NULL)) {
				*out_component = e_cal_meta_backend_merge_instances (meta_backend, components, FALSE);
				success = TRUE;

				if (!e_cal_cache_get_component_extra (cal_cache, uid, NULL, out_extra, cancellable, NULL))
					*out_extra = NULL;
			}
			g_slist_free_full (components, g_object_unref);
			g_object_unref (cal_cache);
		}
	}

	return success;
}

static gboolean
ecb_etesync_save_component_sync (ECalMetaBackend *meta_backend,
				 gboolean overwrite_existing,
				 EConflictResolution conflict_resolution,
				 const GSList *instances, /* ECalComponent * */
				 const gchar *extra,
				 guint32 opflags,
				 gchar **out_new_uid,
				 gchar **out_new_extra,
				 GCancellable *cancellable,
				 GError **error)
{
	ECalBackendEteSync *cbetesync;
	EEteSyncConnection *connection;
	ICalComponent *vcalendar = NULL;
	gboolean success = FALSE, conflict = TRUE;
	gchar *last_sync_tag, *content;
	gchar *new_sync_tag;
	ECalMetaBackendInfo *info;
	EteSyncErrorCode etesync_error;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);
	connection = cbetesync->priv->connection;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	vcalendar = e_cal_meta_backend_merge_instances (meta_backend, instances, TRUE);
	g_return_val_if_fail (vcalendar != NULL, FALSE);

	content = i_cal_component_as_ical_string (vcalendar);

	while (conflict) {
		last_sync_tag = e_cal_meta_backend_dup_sync_tag (meta_backend);

		if (overwrite_existing) {
			success = e_etesync_connection_entry_write_action (connection, cbetesync->priv->journal_id,
				last_sync_tag, content, ETESYNC_SYNC_ENTRY_ACTION_CHANGE, &new_sync_tag, &etesync_error);
		} else {
			success = e_etesync_connection_entry_write_action (connection, cbetesync->priv->journal_id,
				last_sync_tag, content, ETESYNC_SYNC_ENTRY_ACTION_ADD, &new_sync_tag, &etesync_error);
		}

		if (success) {
			gchar *rev, *uid;

			ecb_etesync_get_component_uid_revision (content, &uid, &rev);
			info = e_cal_meta_backend_info_new (uid, rev, content, NULL);

			cbetesync->priv->preloaded_sync_tag = g_strdup (new_sync_tag);
			if (overwrite_existing)
				cbetesync->priv->preloaded_modify = g_slist_prepend (cbetesync->priv->preloaded_modify, info);
			else
				cbetesync->priv->preloaded_add = g_slist_prepend (cbetesync->priv->preloaded_add, info);

			conflict = FALSE;
			g_free (uid);
			g_free (rev);
		} else {
			if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT)
				conflict = FALSE;
			else {
				if (! e_cal_meta_backend_refresh_sync (meta_backend, cancellable, error))
					conflict = FALSE;
			}
		}

		g_free (new_sync_tag);
		g_free (last_sync_tag);
	}

	if (success) {
		cbetesync->priv->fetch_from_server = FALSE;
		e_cal_meta_backend_refresh_sync (meta_backend, cancellable, error);
		cbetesync->priv->fetch_from_server = TRUE;
	}

	g_free (content);
	g_object_unref (vcalendar);

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);
	return success;
}

static gboolean
ecb_etesync_remove_component_sync (ECalMetaBackend *meta_backend,
				   EConflictResolution conflict_resolution,
				   const gchar *uid,
				   const gchar *extra,
				   const gchar *object,
				   guint32 opflags,
				   GCancellable *cancellable,
				   GError **error)
{
	ECalBackendEteSync *cbetesync;
	EEteSyncConnection *connection;
	ICalComponent *vcalendar = NULL;
	gboolean success = FALSE, conflict = TRUE;
	gchar *last_sync_tag, *new_sync_tag, *content;
	GSList *instances = NULL;
	ECalCache *cal_cache;
	ECalMetaBackendInfo *info;
	EteSyncErrorCode etesync_error;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);
	connection = cbetesync->priv->connection;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* Search cache data for the required component */
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

	if (cal_cache) {
		if (e_cal_cache_get_components_by_uid (cal_cache, uid, &instances, cancellable, NULL)) {
			vcalendar = e_cal_meta_backend_merge_instances (meta_backend, instances, TRUE);
			content = i_cal_component_as_ical_string (vcalendar);
			success = TRUE;
		}
		g_object_unref (cal_cache);
	}

	/* Checking if the calendar with that uid has been found */
	if (success) {
		while (conflict) {
			last_sync_tag = e_cal_meta_backend_dup_sync_tag (meta_backend);

			success = e_etesync_connection_entry_write_action (connection, cbetesync->priv->journal_id,
				last_sync_tag, content, ETESYNC_SYNC_ENTRY_ACTION_DELETE, &new_sync_tag, &etesync_error);

			if (success) {
				gchar *rev, *uid;

				ecb_etesync_get_component_uid_revision (content, &uid, &rev);
				info = e_cal_meta_backend_info_new (uid, rev, content, NULL);

				cbetesync->priv->preloaded_sync_tag = g_strdup (new_sync_tag);
				cbetesync->priv->preloaded_delete = g_slist_prepend (cbetesync->priv->preloaded_delete, info);
				conflict = FALSE;
				g_free (uid);
				g_free (rev);
			} else {
				if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT)
					conflict = FALSE;
				else {
					if (! e_cal_meta_backend_refresh_sync (meta_backend, cancellable, error))
						conflict = FALSE;
				}
			}

			g_free (new_sync_tag);
			g_free (last_sync_tag);
		}

		g_free (content);
		g_object_unref (vcalendar);
		g_slist_free_full (instances, g_object_unref);
	}

	if (success) {
		cbetesync->priv->fetch_from_server = FALSE;
		e_cal_meta_backend_refresh_sync (meta_backend, cancellable, error);
		cbetesync->priv->fetch_from_server = TRUE;
	}

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return success;
}

/* --------------------Batch Functions-------------------- */
static void
ecb_etesync_create_objects_sync (ECalBackendSync *backend,
				 EDataCal *cal,
				 GCancellable *cancellable,
				 const GSList *calobjs,
				 guint32 opflags,
				 GSList **uids,
				 GSList **new_components,
				 GError **error)
{
	ECalBackendEteSync *cbetesync;
	EEteSyncConnection *connection;
	EteSyncErrorCode etesync_error;
	gchar *last_sync_tag;
	gboolean success = TRUE;
	const GSList *l;

	g_return_if_fail (E_IS_CAL_BACKEND_ETESYNC (backend));

	if (!calobjs || !calobjs->next) {
		/* Chain up to parent's method. */
		E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_etesync_parent_class)->create_objects_sync (backend, cal, cancellable, calobjs, opflags, uids, new_components, error);
		return;
	}

	cbetesync = E_CAL_BACKEND_ETESYNC (backend);
	connection = cbetesync->priv->connection;
	last_sync_tag = e_cal_meta_backend_dup_sync_tag (E_CAL_META_BACKEND (cbetesync));
	*uids = NULL;
	*new_components = NULL;
	l = calobjs;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* extract the components and mass-add them to the server "batch by batch" */
	while (l && success) {
		gchar **content;
		gboolean conflict = TRUE;
		GSList *batch_uids = NULL; /* gchar* */
		GSList *batch_components= NULL; /* ECalComponent* */
		GSList *batch_info = NULL; /* ECalMetaBackendInfo* */
		guint ii,  batch_length = 0;

		content = g_slice_alloc0 (PUSH_BATCH_LIMIT * sizeof (gchar *));

		/* Data Preproccessing */
		for (ii = 0 ; ii < PUSH_BATCH_LIMIT && l; l = l->next, ii++) {
			ICalComponent *icomp, *vcal;
			ECalComponent *comp;
			ICalTime *current;
			ECalMetaBackendInfo *info;
			gchar *comp_uid, *comp_revision;

			/* Parse the icalendar text */
			icomp = i_cal_parser_parse_string ((gchar *) l->data);
			if (!icomp) {
				success = FALSE;
				break;
			}
			comp = e_cal_component_new_from_icalcomponent (icomp);

			/* Preserve original UID, create a unique UID if needed */
			if (!i_cal_component_get_uid (icomp)) {
				gchar *new_uid = etesync_gen_uid ();
				i_cal_component_set_uid (icomp, new_uid);
				g_free (new_uid);
			}

			/* Set the created and last modified times on the component, if not there already */
			current = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
			if (!e_cal_util_component_has_property (icomp, I_CAL_CREATED_PROPERTY)) {
				/* Update both when CREATED is missing, to make sure the LAST-MODIFIED
				is not before CREATED */
				e_cal_component_set_created (comp, current);
				e_cal_component_set_last_modified (comp, current);
			} else if (!e_cal_util_component_has_property (icomp, I_CAL_LASTMODIFIED_PROPERTY)) {
				e_cal_component_set_last_modified (comp, current);
			}
			g_object_unref (current);

			/* If no vcaledar exist, create a new one */
			if (i_cal_component_isa (icomp) != I_CAL_VCALENDAR_COMPONENT) {
				vcal = e_cal_util_new_top_level ();
				i_cal_component_take_component (vcal, i_cal_component_clone (icomp));
				content[ii] = i_cal_component_as_ical_string (vcal);
				g_object_unref (vcal);
			} else
				content[ii] = i_cal_component_as_ical_string (icomp);

			ecb_etesync_get_component_uid_revision (content[ii], &comp_uid, &comp_revision);
			info = e_cal_meta_backend_info_new (comp_uid, comp_revision, content[ii], NULL);

			batch_components = g_slist_prepend (batch_components, e_cal_component_clone (comp));
			batch_uids = g_slist_prepend (batch_uids, comp_uid);
			batch_info = g_slist_prepend (batch_info, info);

			g_free (comp_revision);
			g_object_unref (comp);
		}

		batch_length = ii;

		if (success) {
			while (conflict) {
				success = e_etesync_connection_multiple_entry_write_action (connection,
										cbetesync->priv->journal_id,
										(const gchar* const*) content,
										batch_length, ETESYNC_SYNC_ENTRY_ACTION_ADD,
										&last_sync_tag, &etesync_error);

				if (success) {
					g_free (cbetesync->priv->preloaded_sync_tag);
					cbetesync->priv->preloaded_sync_tag = g_strdup (last_sync_tag);
					cbetesync->priv->preloaded_add = g_slist_concat (batch_info, cbetesync->priv->preloaded_add);
					*new_components = g_slist_concat (*new_components, batch_components);
					*uids = g_slist_concat (*uids, batch_uids);
					conflict = FALSE;
				} else {
					if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT) {
						g_slist_free_full (batch_components, g_object_unref);
						g_slist_free_full (batch_info, e_cal_meta_backend_info_free);
						g_slist_free_full (batch_uids, g_free);
						conflict = FALSE;
					} else { /* Repeat again if a conflict existed */
						/* This will empty batch_info and update the last_sync_tag to avoid conflict again */
						if (!e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), NULL, NULL))
							conflict = FALSE;

						g_free (last_sync_tag);
						last_sync_tag = e_cal_meta_backend_dup_sync_tag (E_CAL_META_BACKEND (cbetesync));
					}
				}
			}
		}

		for (ii = 0; ii < batch_length; ii++)
			g_free (content[ii]);
		g_slice_free1 (PUSH_BATCH_LIMIT * sizeof (gchar *), content);
	}

	if (success) {
		cbetesync->priv->fetch_from_server = FALSE;
		e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), cancellable, error);
		cbetesync->priv->fetch_from_server = TRUE;
	} else {
		g_slist_free_full (*new_components, g_object_unref);
		g_slist_free_full (*uids, g_free);
		*new_components = NULL;
		*uids = NULL;
	}

	/* free any data related to this bulk operation */
	cbetesync->priv->preloaded_add = NULL;
	g_free (last_sync_tag);

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return;
}

static void
ecb_etesync_modify_objects_sync (ECalBackendSync *backend,
				 EDataCal *cal,
				 GCancellable *cancellable,
				 const GSList *calobjs,
				 ECalObjModType mod,
				 guint32 opflags,
				 GSList **old_components,
				 GSList **new_components,
				 GError **error)
{
	ECalBackendEteSync *cbetesync;
	ECalCache *cal_cache;
	EEteSyncConnection *connection;
	EteSyncErrorCode etesync_error;
	gchar *last_sync_tag;
	gboolean success = TRUE;
	const GSList *l;

	g_return_if_fail (E_IS_CAL_BACKEND_ETESYNC (backend));

	if (!calobjs || !calobjs->next) {
		/* Chain up to parent's method. */
		E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_etesync_parent_class)->modify_objects_sync (backend, cal, cancellable, calobjs, mod, opflags, old_components, new_components, error);
		return;
	}

	cbetesync = E_CAL_BACKEND_ETESYNC (backend);
	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbetesync));
	connection = cbetesync->priv->connection;
	last_sync_tag = e_cal_meta_backend_dup_sync_tag (E_CAL_META_BACKEND (cbetesync));
	*old_components = NULL;
	*new_components = NULL;
	l = calobjs;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* extract the components and mass-add them to the server "batch by batch" */
	while (l && success) {
		gchar **content;
		gboolean conflict = TRUE;
		GSList *batch_old_components = NULL; /* ECalComponent* */
		GSList *batch_new_components= NULL; /* ECalComponent* */
		GSList *batch_info = NULL; /* ECalMetaBackendInfo* */
		guint ii,  batch_length = 0;

		content = g_slice_alloc0 (PUSH_BATCH_LIMIT * sizeof (gchar *));

		/* Data Preproccessing */
		for (ii = 0 ; ii < PUSH_BATCH_LIMIT && l; l = l->next, ii++) {
			ICalComponent *icomp, *vcal;
			ECalComponent *comp;
			ICalTime *current;
			ECalMetaBackendInfo *info;
			gchar *comp_uid, *comp_revision;
			GSList *instances;

			/* Parse the icalendar text */
			icomp = i_cal_parser_parse_string ((gchar *) l->data);
			if (!icomp) {
				success = FALSE;
				break;
			}
			comp = e_cal_component_new_from_icalcomponent (icomp);

			/* Set the created and last modified times on the component, if not there already */
			current = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
			e_cal_component_set_last_modified (comp, current);
			g_object_unref (current);

			/* If no vcaledar exist, create a new one */
			if (i_cal_component_isa (icomp) != I_CAL_VCALENDAR_COMPONENT) {
				vcal = e_cal_util_new_top_level ();
				i_cal_component_take_component (vcal, i_cal_component_clone (icomp));
				content[ii] = i_cal_component_as_ical_string (vcal);
				g_object_unref (vcal);
			} else
				content[ii] = i_cal_component_as_ical_string (icomp);

			ecb_etesync_get_component_uid_revision (content[ii], &comp_uid, &comp_revision);

			info = e_cal_meta_backend_info_new (comp_uid, comp_revision, content[ii], NULL);

			batch_new_components = g_slist_prepend (batch_new_components, e_cal_component_clone (comp));
			batch_info = g_slist_prepend (batch_info, info);

			if (e_cal_cache_get_components_by_uid (cal_cache, comp_uid, &instances, NULL, NULL))
				batch_old_components = g_slist_concat (batch_old_components, instances);

			g_free (comp_uid);
			g_free (comp_revision);
			g_object_unref (comp);
		}

		batch_length = ii;

		if (success) {
			while (conflict) {
				success = e_etesync_connection_multiple_entry_write_action (connection,
										cbetesync->priv->journal_id,
										(const gchar* const*) content,
										batch_length, ETESYNC_SYNC_ENTRY_ACTION_CHANGE,
										&last_sync_tag, &etesync_error);

				if (success) {
					g_free (cbetesync->priv->preloaded_sync_tag);
					cbetesync->priv->preloaded_sync_tag = g_strdup (last_sync_tag);
					cbetesync->priv->preloaded_modify = g_slist_concat (batch_info, cbetesync->priv->preloaded_modify);
					*new_components = g_slist_concat (*new_components, batch_new_components);
					*old_components = g_slist_concat (*old_components, batch_old_components);
					conflict = FALSE;
				} else {
					if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT) {
						g_slist_free_full (batch_new_components, g_object_unref);
						g_slist_free_full (batch_old_components, g_object_unref);
						g_slist_free_full (batch_info, e_cal_meta_backend_info_free);
						conflict = FALSE;
					} else { /* Repeat again if a conflict existed */
						/* This will empty batch_info and update the last_sync_tag to avoid conflict again */
						if (!e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), NULL, NULL))
							conflict = FALSE;

						g_free (last_sync_tag);
						last_sync_tag = e_cal_meta_backend_dup_sync_tag (E_CAL_META_BACKEND (cbetesync));
					}
				}
			}
		}

		for (ii = 0; ii < batch_length; ii++)
			g_free (content[ii]);
		g_slice_free1 (PUSH_BATCH_LIMIT * sizeof (gchar *), content);
	}

	if (success) {
		cbetesync->priv->fetch_from_server = FALSE;
		e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), cancellable, error);
		cbetesync->priv->fetch_from_server = TRUE;
	} else {
		g_slist_free_full (*new_components, g_object_unref);
		g_slist_free_full (*old_components, g_object_unref);
		*new_components = NULL;
		*old_components = NULL;
	}

	/* free any data related to this bulk operation */
	cbetesync->priv->preloaded_modify = NULL;
	g_free (last_sync_tag);
	g_object_unref (cal_cache);

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return;
}

static void
ecb_etesync_remove_objects_sync (ECalBackendSync *backend,
				 EDataCal *cal,
				 GCancellable *cancellable,
				 const GSList *ids,
				 ECalObjModType mod,
				 guint32 opflags,
				 GSList **old_components,
				 GSList **new_components,
				 GError **error)
{
	ECalBackendEteSync *cbetesync;
	ECalCache *cal_cache;
	EEteSyncConnection *connection;
	EteSyncErrorCode etesync_error;
	gchar *last_sync_tag;
	gboolean success = TRUE;
	const GSList *l;

	g_return_if_fail (E_IS_CAL_BACKEND_ETESYNC (backend));

	if (!ids || !ids->next) {
		/* Chain up to parent's method. */
		E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_etesync_parent_class)->remove_objects_sync (backend, cal, cancellable, ids, mod, opflags, old_components, new_components, error);
		return;
	}

	cbetesync = E_CAL_BACKEND_ETESYNC (backend);
	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbetesync));
	connection = cbetesync->priv->connection;
	last_sync_tag = e_cal_meta_backend_dup_sync_tag (E_CAL_META_BACKEND (cbetesync));
	*old_components = NULL;
	*new_components = NULL;
	l = ids;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* extract the components and mass-add them to the server "batch by batch" */
	while (l && success) {
		gchar **content;
		gboolean conflict = TRUE;
		GSList *batch_old_components = NULL; /* ECalComponent* */
		GSList *batch_info = NULL; /* ECalMetaBackendInfo* */
		guint ii,  batch_length = 0;

		content = g_slice_alloc0 (PUSH_BATCH_LIMIT * sizeof (gchar *));

		/* Data Preproccessing */
		for (ii = 0; ii < PUSH_BATCH_LIMIT && l; l = l->next, ii++) {
			ICalComponent *vcal;
			ECalMetaBackendInfo *info;
			gchar *comp_uid, *comp_revision;
			GSList *instances;

			if (e_cal_cache_get_components_by_uid (cal_cache, e_cal_component_id_get_uid (l->data), &instances, cancellable, NULL)) {
				vcal = e_cal_meta_backend_merge_instances (E_CAL_META_BACKEND (cbetesync), instances, TRUE);
				content[ii] = i_cal_component_as_ical_string (vcal);
				g_object_unref (vcal);
			} else {
				success = FALSE;
				break;
			}

			ecb_etesync_get_component_uid_revision (content[ii], &comp_uid, &comp_revision);

			info = e_cal_meta_backend_info_new (comp_uid, comp_revision, content[ii], NULL);

			batch_info = g_slist_prepend (batch_info, info);
			batch_old_components = g_slist_concat (batch_old_components, instances);
			*new_components = g_slist_prepend (*new_components, NULL);

			g_free (comp_uid);
			g_free (comp_revision);
		}

		batch_length = ii;

		if (success) {
			while (conflict) {
				success = e_etesync_connection_multiple_entry_write_action (connection,
										cbetesync->priv->journal_id,
										(const gchar* const*) content,
										batch_length, ETESYNC_SYNC_ENTRY_ACTION_DELETE,
										&last_sync_tag, &etesync_error);

				if (success) {
					g_free (cbetesync->priv->preloaded_sync_tag);
					cbetesync->priv->preloaded_sync_tag = g_strdup (last_sync_tag);
					cbetesync->priv->preloaded_delete = g_slist_concat (batch_info, cbetesync->priv->preloaded_delete);
					*old_components = g_slist_concat (*old_components, batch_old_components);

					conflict = FALSE;
				} else {
					if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT) {
						g_slist_free_full (batch_old_components, g_object_unref);
						g_slist_free_full (batch_info, e_cal_meta_backend_info_free);
						conflict = FALSE;
					} else { /* Repeat again if a conflict existed */
						/* This will empty batch_info and update the last_sync_tag to avoid conflict again */
						if (!e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), NULL, NULL))
							conflict = FALSE;

						g_free (last_sync_tag);
						last_sync_tag = e_cal_meta_backend_dup_sync_tag (E_CAL_META_BACKEND (cbetesync));
					}
				}
			}
		}

		for (ii = 0; ii < batch_length; ii++)
			g_free (content[ii]);
		g_slice_free1 (PUSH_BATCH_LIMIT * sizeof (gchar *), content);
	}

	if (success) {
		cbetesync->priv->fetch_from_server = FALSE;
		e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), cancellable, error);
		cbetesync->priv->fetch_from_server = TRUE;
	} else {
		g_slist_free_full (*old_components, g_object_unref);
		*old_components = NULL;
	}

	/* free any data related to this bulk operation */
	cbetesync->priv->preloaded_delete = NULL;
	g_free (last_sync_tag);
	g_object_unref (cal_cache);

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return;
}
/*------------------------------------------------------------*/

static void
ecb_etesync_source_changed_cb (ESourceRegistry *registry,
			       ESource *source,
			       gpointer user_data)
{
	ESource *collection = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (collection) {
		e_etesync_connection_source_changed_cb (source, collection, FALSE);

		g_clear_object (&collection);
	}
}

static gchar *
ecb_etesync_get_backend_property (ECalBackend *cal_backend,
				  const gchar *prop_name)
{
	ECalBackendEteSync *cbetesync;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (cal_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	cbetesync = E_CAL_BACKEND_ETESYNC (cal_backend);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (
			",",
			E_CAL_STATIC_CAPABILITY_NO_THISANDPRIOR,
			E_CAL_STATIC_CAPABILITY_TASK_CAN_RECUR,
			E_CAL_STATIC_CAPABILITY_COMPONENT_COLOR,
			E_CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS,
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cbetesync)),
			NULL);
	}  else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		return NULL;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_etesync_parent_class)->impl_get_backend_property (cal_backend, prop_name);
}

static void
e_cal_backend_etesync_dispose (GObject *object)
{
	ECalBackendEteSync *cbetesync = E_CAL_BACKEND_ETESYNC (object);
	ESourceRegistry *registry;

	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbetesync));

	/* Only disconnect when backend_count is zero */
	G_LOCK (backend_count);
	if (registry && !--backend_count) {
		g_signal_handler_disconnect (
			registry, source_changed_handler_id);

		backend_count = 0;
	}
	G_UNLOCK (backend_count);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_etesync_parent_class)->dispose (object);
}

static void
e_cal_backend_etesync_finalize (GObject *object)
{
	ECalBackendEteSync *cbetesync = E_CAL_BACKEND_ETESYNC (object);

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);
	g_clear_object (&cbetesync->priv->connection);
	g_clear_pointer (&cbetesync->priv->journal_id, g_free);
	g_clear_pointer (&cbetesync->priv->preloaded_sync_tag, g_free);
	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	g_rec_mutex_clear (&cbetesync->priv->etesync_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_etesync_parent_class)->finalize (object);
}

static void
e_cal_backend_etesync_constructed (GObject *object)
{
	ECalBackendEteSync *cbetesync = E_CAL_BACKEND_ETESYNC (object);
	ESourceRegistry *registry;
	gulong handler_id;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_cal_backend_etesync_parent_class)->constructed (object);

	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbetesync));

	/* Set only once when backend_count is zero to avoid multiple calls on the same file source when changed */
	G_LOCK (backend_count);
	if (!backend_count++) {
		handler_id = g_signal_connect (
			registry, "source-changed",
			G_CALLBACK (ecb_etesync_source_changed_cb), NULL);

		source_changed_handler_id = handler_id;
	}
	G_UNLOCK (backend_count);
}

static void
e_cal_backend_etesync_init (ECalBackendEteSync *cbetesync)
{
	cbetesync->priv = e_cal_backend_etesync_get_instance_private (cbetesync);
	g_rec_mutex_init (&cbetesync->priv->etesync_lock);
	cbetesync->priv->connection = NULL;
	cbetesync->priv->fetch_from_server = TRUE;
	cbetesync->priv->journal_id = NULL;
	cbetesync->priv->preloaded_sync_tag = NULL;
	cbetesync->priv->preloaded_add = NULL;
	cbetesync->priv->preloaded_modify = NULL;
	cbetesync->priv->preloaded_delete = NULL;
}

static void
e_cal_backend_etesync_class_init (ECalBackendEteSyncClass *klass)
{
	GObjectClass *object_class;
	ECalBackendClass *cal_backend_class;
	ECalBackendSyncClass *backend_sync_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_etesync_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_etesync_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_etesync_get_changes_sync;
	cal_meta_backend_class->list_existing_sync = ecb_etesync_list_existing_sync;
	cal_meta_backend_class->load_component_sync = ecb_etesync_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_etesync_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_etesync_remove_component_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->impl_get_backend_property = ecb_etesync_get_backend_property;

	backend_sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);
	backend_sync_class->create_objects_sync = ecb_etesync_create_objects_sync;
	backend_sync_class->modify_objects_sync = ecb_etesync_modify_objects_sync;
	backend_sync_class->remove_objects_sync = ecb_etesync_remove_objects_sync;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = e_cal_backend_etesync_dispose;
	object_class->finalize = e_cal_backend_etesync_finalize;
	object_class->constructed = e_cal_backend_etesync_constructed;
}
