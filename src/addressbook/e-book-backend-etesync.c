/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-backend-etesync.c - EteSync contact backend.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <libedataserver/libedataserver.h>
#include <etesync.h>

#include "common/e-source-etesync.h"
#include "common/e-etesync-connection.h"
#include "e-book-backend-etesync.h"

#define FETCH_BATCH_LIMIT 50
#define PUSH_BATCH_LIMIT 30

static gulong source_changed_handler_id = 0;
static gint backend_count = 0;
G_LOCK_DEFINE_STATIC (backend_count);

struct _EBookBackendEteSyncPrivate {
	EEteSyncConnection *connection;
	GRecMutex etesync_lock;
	gchar *journal_id;

	gboolean fetch_from_server;

	gchar *preloaded_sync_tag;
	GSList *preloaded_add; /* EBookMetaBackendInfo * */
	GSList *preloaded_modify; /* EBookMetaBackendInfo * */
	GSList *preloaded_delete; /* EBookMetaBackendInfo * */
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendEteSync, e_book_backend_etesync, E_TYPE_BOOK_META_BACKEND)

static void
ebb_etesync_setup_connection (EBookBackendEteSync *bbetesync)
{
	ESource *source, *collection;
	ESourceRegistry *registry;

	source = e_backend_get_source (E_BACKEND (bbetesync));
	registry = e_book_backend_get_registry (E_BOOK_BACKEND (bbetesync));
	collection = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
		ESourceEteSync *etesync_extension;

		etesync_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
		g_free (bbetesync->priv->journal_id);
		bbetesync->priv->journal_id = e_source_etesync_dup_journal_id (etesync_extension);
	}

	if (collection) {
		bbetesync->priv->connection = e_etesync_connection_new_connection (collection);
		g_clear_object (&collection);
	}
}

/*
 * Should check if credentials exist, if so should validate them
 * to make sure credentials haven't changed, then copy them to the private
 */
static ESourceAuthenticationResult
ebb_etesync_set_credentials_sync (EBookBackendEteSync *bbetesync,
				  const ENamedParameters *credentials)
{
	ESource *source;
	ESource *collection;
	ESourceRegistry *registry;

	source = e_backend_get_source (E_BACKEND (bbetesync));
	registry = e_book_backend_get_registry (E_BOOK_BACKEND (bbetesync));
	collection = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (collection) {
		ESourceAuthentication *authentication_extension;
		ESourceCollection *collection_extension;

		authentication_extension = e_source_get_extension (collection, E_SOURCE_EXTENSION_AUTHENTICATION);
		collection_extension = e_source_get_extension (collection, E_SOURCE_EXTENSION_COLLECTION);

		g_clear_object (&collection);

		g_return_val_if_fail (authentication_extension != NULL, E_SOURCE_AUTHENTICATION_ERROR);
		g_return_val_if_fail (collection_extension != NULL, E_SOURCE_AUTHENTICATION_ERROR);

		return e_etesync_connection_set_connection_from_sources (bbetesync->priv->connection, credentials, authentication_extension, collection_extension);
	}

	return E_SOURCE_AUTHENTICATION_ERROR;
}

static gboolean
ebb_etesync_connect_sync (EBookMetaBackend *meta_backend,
			  const ENamedParameters *credentials,
			  ESourceAuthenticationResult *out_auth_result,
			  gchar **out_certificate_pem,
			  GTlsCertificateFlags *out_certificate_errors,
			  GCancellable *cancellable,
			  GError **error)
{
	EBookBackendEteSync *bbetesync;
	EEteSyncConnection *connection;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	if (!bbetesync->priv->connection)
		ebb_etesync_setup_connection (bbetesync);

	if (bbetesync->priv->connection) {
		gboolean needs_setting;

		connection = bbetesync->priv->connection;
		needs_setting = e_etesync_connection_needs_setting (connection, credentials, out_auth_result);

		if (needs_setting)
			*out_auth_result = ebb_etesync_set_credentials_sync (bbetesync, credentials);

		if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
			gboolean is_read_only;

			success = e_etesync_connection_get_journal_exists (connection, bbetesync->priv->journal_id, &is_read_only);

			if (success)
				e_book_backend_set_writable (E_BOOK_BACKEND (bbetesync), !is_read_only);
		}
	} else {
		*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
}

static gboolean
ebb_etesync_disconnect_sync (EBookMetaBackend *meta_backend,
			     GCancellable *cancellable,
			     GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);

	return TRUE;
}

static void
ebb_etesync_get_contact_uid_revision (const gchar *content,
				      gchar **out_contact_uid,
				      gchar **out_revision)
{
	EContact *contact;

	contact = e_contact_new_from_vcard (content);

	if (contact) {
		*out_contact_uid = e_contact_get (contact, E_CONTACT_UID);
		*out_revision = e_contact_get (contact, E_CONTACT_REV);

		g_object_unref (contact);
	}
}

static void
ebb_etesync_update_hash_tables (GHashTable *main_table,
				GHashTable *table_1,
				GHashTable *table_2,
				const gchar *uid,
				EBookMetaBackendInfo *nfo)
{
	g_hash_table_replace (main_table, g_strdup (uid), nfo);
	g_hash_table_remove (table_1, uid);
	g_hash_table_remove (table_2, uid);
}

static gboolean
ebb_etesync_list_existing_sync (EBookMetaBackend *meta_backend,
				gchar **out_new_sync_tag,
				GSList **out_existing_objects, /* EBookMetaBackendInfo * */
				GCancellable *cancellable,
				GError **error)
{
	EBookBackendEteSync *bbetesync;
	EteSyncEntry **entries = NULL, **entry_iter;
	EteSyncEntryManager *entry_manager;
	EteSyncCryptoManager *crypto_manager = NULL;
	EEteSyncConnection *connection;
	gchar *prev_entry_uid = NULL;
	GHashTable *existing_entry; /* gchar *uid ~> EBookMetaBackendInfo * */
	GHashTableIter iter;
	gpointer key = NULL, value = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	*out_existing_objects = NULL;

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);
	connection = bbetesync->priv->connection;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	/* 1) Retrive entry_manager, crypto_manager and entries since "last_sync_tag", here "last_sync_tag" will be NULL
	      Failing means connection error */
	if (!e_etesync_connection_check_journal_changed_sync (connection, bbetesync->priv->journal_id, NULL, FETCH_BATCH_LIMIT, &entries, &crypto_manager, &entry_manager)) {
		g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);
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
		g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);
		return TRUE;
	}

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	/* 3) At this point, entries are not empty, then we should loop on entries and add each
	      one to the hashtable as EBookMetaBackendInfo only keeping last action for each contact */
	existing_entry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_book_meta_backend_info_free);
	prev_entry_uid = NULL;

	while (entries && *entries) {
		gint batch_size = 0;

		for (entry_iter = entries; *entry_iter; entry_iter++, batch_size++) {

			EteSyncEntry *entry;
			EteSyncSyncEntry *sync_entry;
			EBookMetaBackendInfo *nfo;
			gchar *entry_uid, *action, *content, *contact_uid, *revision;

			entry = *entry_iter;
			sync_entry = etesync_entry_get_sync_entry (entry, crypto_manager, prev_entry_uid);

			action = etesync_sync_entry_get_action (sync_entry);
			content = etesync_sync_entry_get_content (sync_entry);

			/* create EBookMetaBackendInfo * to be stored in Hash Table */
			ebb_etesync_get_contact_uid_revision (content, &contact_uid, &revision);
			nfo = e_book_meta_backend_info_new (contact_uid, revision, content, NULL);

			/* check action add, change or delete */
			if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_ADD) ||
			    g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_CHANGE))
				g_hash_table_replace (existing_entry, g_strdup (contact_uid), nfo);

			else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_DELETE))
				g_hash_table_remove (existing_entry, contact_uid);

			else
				e_book_meta_backend_info_free (nfo);

			entry_uid = etesync_entry_get_uid (entry);
			g_free (prev_entry_uid);
			prev_entry_uid = entry_uid;

			etesync_sync_entry_destroy (sync_entry);
			etesync_entry_destroy (entry);
			g_free (content);
			g_free (action);
			g_free (contact_uid);
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
		*out_existing_objects = g_slist_prepend (*out_existing_objects, e_book_meta_backend_info_copy (value));

        g_free (entries);
	g_free (prev_entry_uid);
        etesync_entry_manager_destroy (entry_manager);
        etesync_crypto_manager_destroy (crypto_manager);
	g_hash_table_destroy (existing_entry);

	return TRUE;
}

static gboolean
ebb_etesync_get_changes_sync (EBookMetaBackend *meta_backend,
			      const gchar *last_sync_tag,
			      gboolean is_repeat,
			      gchar **out_new_sync_tag,
			      gboolean *out_repeat,
			      GSList **out_created_objects, /* EBookMetaBackendInfo * */
			      GSList **out_modified_objects, /* EBookMetaBackendInfo * */
			      GSList **out_removed_objects, /* EBookMetaBackendInfo * */
			      GCancellable *cancellable,
			      GError **error)
{
	EBookBackendEteSync *bbetesync;
	EteSyncEntry **entries = NULL, **entry_iter;
	EteSyncCryptoManager *crypto_manager = NULL;
	EteSyncEntryManager *entry_manager;
	EEteSyncConnection *connection;
	gchar *prev_entry_uid;
	const gchar *last_tag = NULL;
	GHashTable *created_entry, *modified_entry, *removed_entry; /* gchar *uid ~> EBookMetaBackendInfo * */
	GHashTableIter iter;
	gpointer key = NULL, value = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);
	connection = bbetesync->priv->connection;

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	/* Must add preloaded and updating the out_new_sync_tag */
	if (bbetesync->priv->preloaded_sync_tag) {
		*out_created_objects = bbetesync->priv->preloaded_add;
		*out_modified_objects = bbetesync->priv->preloaded_modify;
		*out_removed_objects = bbetesync->priv->preloaded_delete;
		*out_new_sync_tag = bbetesync->priv->preloaded_sync_tag; /* Made here if no chnages are required to fetch */
		last_tag = bbetesync->priv->preloaded_sync_tag;

		bbetesync->priv->preloaded_sync_tag = NULL;
		bbetesync->priv->preloaded_add = NULL;
		bbetesync->priv->preloaded_modify = NULL;
		bbetesync->priv->preloaded_delete = NULL;
	}

	if (bbetesync->priv->fetch_from_server) {
		if (!last_tag)
			last_tag = last_sync_tag;

		/* 1) Retrive entry_manager, crypto_manager and entries since "last_sync_tag", here "last tag" will be "last_sync_tag"
		Failing means connection error */
		if (!e_etesync_connection_check_journal_changed_sync (connection, bbetesync->priv->journal_id, last_tag, FETCH_BATCH_LIMIT, &entries, &crypto_manager, &entry_manager)) {
			g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);
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
			g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);
			return TRUE;
		}

		/* 3) At this point, entries are not empty, then we should loop on entries and add each
		      one to its hashtable as EBookMetaBackendInfo depending on the action ("create/modify/delete) */
		created_entry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_book_meta_backend_info_free);
		modified_entry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_book_meta_backend_info_free);
		removed_entry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_book_meta_backend_info_free);
		prev_entry_uid = g_strdup (last_tag);

		while (entries && *entries) {
			gint batch_size = 0;

			for (entry_iter = entries; *entry_iter; entry_iter++, batch_size++) {

				EteSyncEntry *entry;
				EteSyncSyncEntry *sync_entry;
				EBookMetaBackendInfo *nfo;
				gchar *entry_uid, *action, *content, *contact_uid, *revision;

				entry = *entry_iter;
				sync_entry = etesync_entry_get_sync_entry (entry, crypto_manager, prev_entry_uid);

				action = etesync_sync_entry_get_action (sync_entry);
				content = etesync_sync_entry_get_content (sync_entry);

				/* create EBookMetaBackendInfo * to be stored in Hash Table */
				ebb_etesync_get_contact_uid_revision (content, &contact_uid, &revision);
				nfo = e_book_meta_backend_info_new (contact_uid, revision, content, NULL);

				/* check action add, change or delete */
				if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_ADD))
					ebb_etesync_update_hash_tables (created_entry, modified_entry, removed_entry, contact_uid, nfo);

				else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_CHANGE))
					ebb_etesync_update_hash_tables (modified_entry, created_entry ,removed_entry, contact_uid, nfo);

				else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_DELETE))
					ebb_etesync_update_hash_tables (removed_entry, created_entry, modified_entry, contact_uid, nfo);

				else
					e_book_meta_backend_info_free (nfo);

				entry_uid = etesync_entry_get_uid (entry);
				g_free (prev_entry_uid);
				prev_entry_uid = entry_uid;

				etesync_sync_entry_destroy (sync_entry);
				etesync_entry_destroy (entry);
				g_free (content);
				g_free (action);
				g_free (contact_uid);
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
			*out_created_objects = g_slist_prepend (*out_created_objects, e_book_meta_backend_info_copy (value));

		g_hash_table_iter_init (&iter, modified_entry);
		while (g_hash_table_iter_next (&iter, &key, &value))
			*out_modified_objects = g_slist_prepend (*out_modified_objects, e_book_meta_backend_info_copy (value));

		g_hash_table_iter_init (&iter, removed_entry);
		while (g_hash_table_iter_next (&iter, &key, &value))
			*out_removed_objects = g_slist_prepend (*out_removed_objects, e_book_meta_backend_info_copy (value));

		g_free (entries);
		etesync_entry_manager_destroy (entry_manager);
		etesync_crypto_manager_destroy (crypto_manager);
		g_hash_table_destroy (created_entry);
		g_hash_table_destroy (modified_entry);
		g_hash_table_destroy (removed_entry);
	}

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return TRUE;
}

static gboolean
ebb_etesync_load_contact_sync (EBookMetaBackend *meta_backend,
			       const gchar *uid,
			       const gchar *extra,
			       EContact **out_contact,
			       gchar **out_extra,
			       GCancellable *cancellable,
			       GError **error)
{
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	/* 1) Call e_book_meta_backend_refresh_sync() to get contacts since last_tag */
	if (e_book_meta_backend_refresh_sync (meta_backend, cancellable, error)) {
		EBookCache *book_cache;

		/* 2) Search cache data for the required contact */
		book_cache = e_book_meta_backend_ref_cache (meta_backend);

		if (book_cache) {
			if (e_book_cache_get_contact (book_cache, uid, FALSE, out_contact, cancellable, NULL)) {
				success =  TRUE;

				if (!e_book_cache_get_contact_extra (book_cache, uid, out_extra, cancellable, NULL))
					*out_extra = NULL;
			}
			g_object_unref (book_cache);
		}
	}
	return success;
}

static gboolean
ebb_etesync_save_contact_sync (EBookMetaBackend *meta_backend,
			       gboolean overwrite_existing,
			       EConflictResolution conflict_resolution,
			       /* const */ EContact *contact,
			       const gchar *extra,
			       guint32 opflags,
			       gchar **out_new_uid,
			       gchar **out_new_extra,
			       GCancellable *cancellable,
			       GError **error)
{
	EBookBackendEteSync *bbetesync;
	EEteSyncConnection *connection;
	gboolean success = FALSE;
	gboolean conflict = TRUE;
	gchar *last_sync_tag, *content;
	gchar *new_sync_tag;
	EBookMetaBackendInfo *info;
	EteSyncErrorCode etesync_error;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);
	g_return_val_if_fail (out_new_extra != NULL, FALSE);

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);
	connection = bbetesync->priv->connection;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	content = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	while (conflict) {
		last_sync_tag = e_book_meta_backend_dup_sync_tag (meta_backend);

		if (overwrite_existing) {
			success = e_etesync_connection_entry_write_action (connection, bbetesync->priv->journal_id,
					last_sync_tag, content, ETESYNC_SYNC_ENTRY_ACTION_CHANGE, &new_sync_tag, &etesync_error);
		} else {
			success = e_etesync_connection_entry_write_action (connection, bbetesync->priv->journal_id,
					last_sync_tag, content, ETESYNC_SYNC_ENTRY_ACTION_ADD, &new_sync_tag, &etesync_error);
		}

		if (success) {
			const gchar *rev, *uid;

			uid = e_contact_get_const (contact, E_CONTACT_UID);
			rev = e_contact_get_const (contact, E_CONTACT_REV);
			info = e_book_meta_backend_info_new (uid, rev, content, NULL);

			bbetesync->priv->preloaded_sync_tag = g_strdup (new_sync_tag);
			if (overwrite_existing)
				bbetesync->priv->preloaded_modify = g_slist_prepend (bbetesync->priv->preloaded_modify, info);
			else
				bbetesync->priv->preloaded_add = g_slist_prepend (bbetesync->priv->preloaded_add, info);
			conflict = FALSE;
		} else {
			if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT)
				conflict = FALSE;
			else
				if (!e_book_meta_backend_refresh_sync (meta_backend, cancellable, error))
					conflict = FALSE;
		}

		g_free (new_sync_tag);
		g_free (last_sync_tag);
	}

	if (success) {
		bbetesync->priv->fetch_from_server = FALSE;
		e_book_meta_backend_refresh_sync (meta_backend, cancellable, error);
		bbetesync->priv->fetch_from_server = TRUE;
	}

	g_free (content);
	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
}

static gboolean
ebb_etesync_remove_contact_sync (EBookMetaBackend *meta_backend,
				 EConflictResolution conflict_resolution,
				 const gchar *uid,
				 const gchar *extra,
				 const gchar *object,
				 guint32 opflags,
				 GCancellable *cancellable,
				 GError **error)
{
	EBookBackendEteSync *bbetesync;
	EEteSyncConnection *connection;
	gboolean success = FALSE;
	gboolean conflict = TRUE;
	gchar *last_sync_tag, *new_sync_tag, *content = NULL;
	EBookCache *book_cache;
	EContact *contact = NULL;
	EBookMetaBackendInfo *info;
	EteSyncErrorCode etesync_error;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);
	connection = bbetesync->priv->connection;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	if (book_cache) {
		if (e_book_cache_get_contact (book_cache, uid, FALSE, &contact, cancellable, NULL)) {
			content = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			success =  TRUE;
		}
		g_object_unref (book_cache);
	}

	/* Checking if the contact with that uid has been found */
	if (success) {
		while (conflict) {
			last_sync_tag = e_book_meta_backend_dup_sync_tag (meta_backend);

			success = e_etesync_connection_entry_write_action (connection, bbetesync->priv->journal_id,
				last_sync_tag, content, ETESYNC_SYNC_ENTRY_ACTION_DELETE, &new_sync_tag, &etesync_error);

			if (success) {
				const gchar *rev;

				rev = e_contact_get_const (contact, E_CONTACT_REV);
				info = e_book_meta_backend_info_new (uid, rev, content, NULL);

				bbetesync->priv->preloaded_sync_tag = g_strdup (new_sync_tag);
				bbetesync->priv->preloaded_delete = g_slist_prepend (bbetesync->priv->preloaded_delete, info);
				conflict = FALSE;
			} else {
				if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT)
					conflict = FALSE;
				else
					if (!e_book_meta_backend_refresh_sync (meta_backend, cancellable, error))
						conflict = FALSE;
			}

			g_free (new_sync_tag);
			g_free (last_sync_tag);
		}

		g_free (content);
		g_object_unref (contact);
	}

	if (success) {
		bbetesync->priv->fetch_from_server = FALSE;
		e_book_meta_backend_refresh_sync (meta_backend, cancellable, error);
		bbetesync->priv->fetch_from_server = TRUE;
	}

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
}

/* --------------------Batch Functions-------------------- */
static gboolean
ebb_etesync_create_modify_contacts_sync (EBookBackendSync *backend,
					 const gchar * const *vcards,
					 GSList **out_contacts,
					 const gchar *action)
{
	EBookBackendEteSync *bbetesync;
	EEteSyncConnection *connection;
	EteSyncErrorCode etesync_error;
	guint length, batch_length, batch_number = 0;
	gchar *last_sync_tag;
	gboolean success = TRUE;

	length = g_strv_length ((gchar **) vcards);

	bbetesync = E_BOOK_BACKEND_ETESYNC (backend);
	connection = bbetesync->priv->connection;
	last_sync_tag = e_book_meta_backend_dup_sync_tag (E_BOOK_META_BACKEND (bbetesync));
	*out_contacts = NULL;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	/* extract the components and mass-add them to the server */
	while (length > 0 && success){
		gboolean conflict = TRUE;
		GSList *batch_contacts = NULL; /* EContact */
		GSList *batch_info = NULL; /* EBookMetaBackendInfo */
		gchar **content;
		guint ii;

		batch_length = length > PUSH_BATCH_LIMIT ? PUSH_BATCH_LIMIT : length;
		length -= batch_length;

		content = g_slice_alloc0 (batch_length * sizeof (gchar *));

		for (ii = 0; ii < batch_length; ii++) {
			const gchar		*id;
			const gchar		*rev;
			EContact		*contact;
			EBookMetaBackendInfo 	*info;

			contact = e_contact_new_from_vcard (vcards[ii + (batch_number * PUSH_BATCH_LIMIT)]);

			/* Preserve original UID, create a unique UID if needed */
			if (e_contact_get_const (contact, E_CONTACT_UID) == NULL) {
				gchar *uid = etesync_gen_uid ();
				e_contact_set (contact, E_CONTACT_UID, uid);
				g_free (uid);
			}

			id = e_contact_get_const (contact, E_CONTACT_UID);
			rev = e_contact_get_const (contact, E_CONTACT_REV);

			content[ii] = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			info = e_book_meta_backend_info_new (id, rev, content[ii], NULL);

			batch_contacts = g_slist_prepend (batch_contacts, contact);
			batch_info = g_slist_prepend (batch_info, info);
		}

		while (conflict) {
			success = e_etesync_connection_multiple_entry_write_action (connection,
									     bbetesync->priv->journal_id,
									     (const gchar* const*) content,
									     batch_length, action,
									     &last_sync_tag, &etesync_error);

			if (success) {
				g_free (bbetesync->priv->preloaded_sync_tag);
				bbetesync->priv->preloaded_sync_tag = g_strdup (last_sync_tag);
				if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_ADD))
					bbetesync->priv->preloaded_add = g_slist_concat (batch_info, bbetesync->priv->preloaded_add);
				else if (g_str_equal (action, ETESYNC_SYNC_ENTRY_ACTION_CHANGE))
					bbetesync->priv->preloaded_modify = g_slist_concat (batch_info, bbetesync->priv->preloaded_modify);
				*out_contacts = g_slist_concat (batch_contacts, *out_contacts);
				conflict = FALSE;
			} else {
				g_slist_free_full (batch_contacts, g_object_unref);
				g_slist_free_full (batch_info, e_book_meta_backend_info_free);

				/* Repeat again if a conflict existed */
				if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT)
					conflict = FALSE;
				else {
					/* This will empty batch_info and update the last_sync_tag to avoid conflict again */
					if (!e_book_meta_backend_refresh_sync (E_BOOK_META_BACKEND (bbetesync), NULL, NULL))
						conflict = FALSE;

					g_free (last_sync_tag);
					last_sync_tag = e_book_meta_backend_dup_sync_tag (E_BOOK_META_BACKEND (bbetesync));
					batch_contacts = NULL;
					batch_info = NULL;
				}
			}
		}
		for (ii = 0; ii < batch_length; ii++)
			g_free (content[ii]);
		g_slice_free1 (batch_length * sizeof (gchar *), content);
		batch_number++;
	}

	if (success) {
		bbetesync->priv->fetch_from_server = FALSE;
		e_book_meta_backend_refresh_sync (E_BOOK_META_BACKEND (bbetesync), NULL, NULL);
		bbetesync->priv->fetch_from_server = TRUE;
	} else {
		g_slist_free_full (*out_contacts, g_object_unref);
		*out_contacts = NULL;
	}

	/* free any data related to this bulk operation */
	bbetesync->priv->preloaded_add = NULL;
	bbetesync->priv->preloaded_modify = NULL;
	g_free (last_sync_tag);

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
}

static gboolean
ebb_etesync_create_contacts_sync (EBookBackendSync *backend,
				  const gchar * const *vcards,
				  guint32 opflags,
				  GSList **out_contacts,
				  GCancellable *cancellable,
				  GError **error)
{
	g_return_val_if_fail (out_contacts != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (backend), FALSE);

	/* Has less than or equal one item */
	if (!vcards || !*vcards || (vcards[0] && !vcards[1])) {
		return E_BOOK_BACKEND_SYNC_CLASS (e_book_backend_etesync_parent_class)->create_contacts_sync (backend, vcards, opflags, out_contacts, cancellable, error);
	}

	return ebb_etesync_create_modify_contacts_sync (backend, vcards,out_contacts, ETESYNC_SYNC_ENTRY_ACTION_ADD);
}

static gboolean
ebb_etesync_modify_contacts_sync (EBookBackendSync *backend,
				  const gchar * const *vcards,
				  guint32 opflags,
				  GSList **out_contacts,
				  GCancellable *cancellable,
				  GError **error)
{
	g_return_val_if_fail (out_contacts != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (backend), FALSE);

	/* Has less than or equal one item */
	if (!vcards || !*vcards || (vcards[0] && !vcards[1])) {
		return E_BOOK_BACKEND_SYNC_CLASS (e_book_backend_etesync_parent_class)->modify_contacts_sync (backend, vcards, opflags, out_contacts, cancellable, error);
	}

	return ebb_etesync_create_modify_contacts_sync (backend, vcards, out_contacts, ETESYNC_SYNC_ENTRY_ACTION_CHANGE);
}

static gboolean
ebb_etesync_remove_contacts_sync (EBookBackendSync *backend,
				  const gchar * const *uids,
				  guint32 opflags,
				  GSList **out_removed_uids,
				  GCancellable *cancellable,
				  GError **error)
{
	EBookBackendEteSync *bbetesync;
	EBookCache *book_cache;
	EEteSyncConnection *connection;
	EteSyncErrorCode etesync_error;
	guint length, batch_length, batch_number = 0;
	gchar *last_sync_tag;
	gboolean success = TRUE;

	g_return_val_if_fail (out_removed_uids != NULL, FALSE);
	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (backend), FALSE);

	if (!uids || !*uids) {
		/* Chain up to parent's method. */
		return E_BOOK_BACKEND_SYNC_CLASS (e_book_backend_etesync_parent_class)->remove_contacts_sync (backend, uids, opflags, out_removed_uids, cancellable, error);
	}

	length = g_strv_length ((gchar **) uids);

	if (length <= 1) {
		/* Chain up to parent's method. */
		return E_BOOK_BACKEND_SYNC_CLASS (e_book_backend_etesync_parent_class)->remove_contacts_sync (backend, uids, opflags, out_removed_uids, cancellable, error);
	}

	bbetesync = E_BOOK_BACKEND_ETESYNC (backend);
	book_cache = e_book_meta_backend_ref_cache (E_BOOK_META_BACKEND (bbetesync));
	connection = bbetesync->priv->connection;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	last_sync_tag = e_book_meta_backend_dup_sync_tag (E_BOOK_META_BACKEND (bbetesync));

	/* extract the components and mass-add them to the server;
	   eventually remember them in the cbetesync->priv and use them
	   in the refresh/get_changes_sync(), instead of re-download them */
	while (length > 0 && success){
		gboolean conflict = TRUE;
		GSList *batch_contacts_id = NULL; /* gchar */
		GSList *batch_info = NULL; /* EBookMetaBackendInfo */
		gchar **content;
		guint ii;

		batch_length = length > PUSH_BATCH_LIMIT ? PUSH_BATCH_LIMIT : length;
		length -= batch_length;

		*out_removed_uids = NULL;
		content = g_slice_alloc0 (batch_length * sizeof (gchar *));

		for (ii = 0; ii < batch_length; ii++) {
			const gchar 		*id;
			const gchar		*rev;
			EContact		*contact;
			EBookMetaBackendInfo 	*info;

			id = uids [ii + (batch_number * PUSH_BATCH_LIMIT)];

			e_book_cache_get_contact (book_cache, id, FALSE, &contact, cancellable, NULL);

			rev = e_contact_get_const (contact, E_CONTACT_REV);
			content[ii] = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			info = e_book_meta_backend_info_new (id, rev, content[ii], NULL);

			batch_contacts_id = g_slist_prepend (batch_contacts_id, g_strdup (id));
			batch_info = g_slist_prepend (batch_info, info);

			g_object_unref (contact);
		}

		while (conflict) {
			success = e_etesync_connection_multiple_entry_write_action (connection,
									     bbetesync->priv->journal_id,
									     (const gchar* const*) content,
									     batch_length, ETESYNC_SYNC_ENTRY_ACTION_ADD,
									     &last_sync_tag, &etesync_error);

			if (success) {
				g_free (bbetesync->priv->preloaded_sync_tag);
				bbetesync->priv->preloaded_sync_tag = g_strdup (last_sync_tag);
				bbetesync->priv->preloaded_delete = g_slist_concat (batch_info, bbetesync->priv->preloaded_delete);
				*out_removed_uids = g_slist_concat (batch_contacts_id, *out_removed_uids);
				conflict = FALSE;
			} else {
				g_slist_free_full (batch_contacts_id, g_object_unref);
				g_slist_free_full (batch_info, e_book_meta_backend_info_free);

				/* Repeat again if a conflict existed */
				if (etesync_error != ETESYNC_ERROR_CODE_CONFLICT)
					conflict = FALSE;
				else {
					/* This will empty batch_info and update the last_sync_tag to avoid conflict again */
					if (!e_book_meta_backend_refresh_sync (E_BOOK_META_BACKEND (bbetesync), NULL, NULL))
						conflict = FALSE;

					g_free (last_sync_tag);
					last_sync_tag = e_book_meta_backend_dup_sync_tag (E_BOOK_META_BACKEND (bbetesync));
					batch_contacts_id = NULL;
					batch_info = NULL;
				}
			}
		}
		for (ii = 0; ii < batch_length; ii++)
			g_free (content[ii]);
		g_slice_free1 (batch_length * sizeof (gchar *), content);
		batch_number++;
	}

	if (success) {
		bbetesync->priv->fetch_from_server = FALSE;
		e_book_meta_backend_refresh_sync (E_BOOK_META_BACKEND (bbetesync), cancellable, error);
		bbetesync->priv->fetch_from_server = TRUE;
	} else {
		g_slist_free_full (*out_removed_uids, g_free);
		*out_removed_uids = NULL;
	}

	/* free any data related to this bulk operation */
	bbetesync->priv->preloaded_delete = NULL;
	g_free (last_sync_tag);
	g_object_unref (book_cache);

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
}
/*------------------------------------------------------------*/

static void
ebb_etesync_source_changed_cb (ESourceRegistry *registry,
			       ESource *source,
			       gpointer user_data)
{
	ESource *collection = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (collection) {
		e_etesync_connection_source_changed_cb (source, collection, TRUE);

		g_clear_object (&collection);
	}
}

static gchar *
ebb_etesync_get_backend_property (EBookBackend *book_backend,
				  const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (book_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			"net",
			"do-initial-query",
			"contact-lists",
			e_book_meta_backend_get_capabilities (E_BOOK_META_BACKEND (book_backend)),
			NULL);
	}

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_etesync_parent_class)->impl_get_backend_property (book_backend, prop_name);
}

static void
e_book_backend_etesync_dispose (GObject *object)
{
	EBookBackendEteSync *bbetesync = E_BOOK_BACKEND_ETESYNC (object);
	ESourceRegistry *registry;

	registry = e_book_backend_get_registry (E_BOOK_BACKEND (bbetesync));

	/* Only disconnect when backend_count is zero */
	G_LOCK (backend_count);
	if (registry && !--backend_count) {
		g_signal_handler_disconnect (
			registry, source_changed_handler_id);

		backend_count = 0;
	}
	G_UNLOCK (backend_count);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_etesync_parent_class)->dispose (object);
}

static void
e_book_backend_etesync_finalize (GObject *object)
{
	EBookBackendEteSync *bbetesync = E_BOOK_BACKEND_ETESYNC (object);

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);
	g_clear_object (&bbetesync->priv->connection);
	g_clear_pointer (&bbetesync->priv->journal_id, g_free);
	g_clear_pointer (&bbetesync->priv->preloaded_sync_tag, g_free);
	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	g_rec_mutex_clear (&bbetesync->priv->etesync_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_etesync_parent_class)->finalize (object);
}

static void
e_book_backend_etesync_constructed (GObject *object)
{
	EBookBackendEteSync *bbetesync = E_BOOK_BACKEND_ETESYNC (object);
	ESourceRegistry *registry;
	gulong handler_id;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_book_backend_etesync_parent_class)->constructed (object);

	registry = e_book_backend_get_registry (E_BOOK_BACKEND (bbetesync));

	/* Set only once when backend_count is zero to avoid multiple calls on the same file source when changed */
	G_LOCK (backend_count);
	if (!backend_count++) {
		handler_id = g_signal_connect (
			registry, "source-changed",
			G_CALLBACK (ebb_etesync_source_changed_cb), NULL);

		source_changed_handler_id = handler_id;
	}
	G_UNLOCK (backend_count);
}

static void
e_book_backend_etesync_init (EBookBackendEteSync *bbetesync)
{
	bbetesync->priv = e_book_backend_etesync_get_instance_private (bbetesync);
	g_rec_mutex_init (&bbetesync->priv->etesync_lock);
	bbetesync->priv->connection = NULL;
	bbetesync->priv->fetch_from_server = TRUE;
	bbetesync->priv->journal_id = NULL;
	bbetesync->priv->preloaded_sync_tag = NULL;
	bbetesync->priv->preloaded_add = NULL;
	bbetesync->priv->preloaded_modify = NULL;
	bbetesync->priv->preloaded_delete = NULL;
}

static void
e_book_backend_etesync_class_init (EBookBackendEteSyncClass *klass)
{

	GObjectClass *object_class;
	EBookBackendClass *book_backend_class;
	EBookBackendSyncClass *backend_sync_class;
	EBookMetaBackendClass *book_meta_backend_class;

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->backend_module_filename = "libebookbackendetesync.so";
	book_meta_backend_class->backend_factory_type_name = "EBookBackendEteSyncFactory";
	book_meta_backend_class->connect_sync = ebb_etesync_connect_sync;
	book_meta_backend_class->disconnect_sync = ebb_etesync_disconnect_sync;
	book_meta_backend_class->get_changes_sync = ebb_etesync_get_changes_sync;
	book_meta_backend_class->list_existing_sync = ebb_etesync_list_existing_sync;
	book_meta_backend_class->load_contact_sync = ebb_etesync_load_contact_sync;
	book_meta_backend_class->save_contact_sync = ebb_etesync_save_contact_sync;
	book_meta_backend_class->remove_contact_sync = ebb_etesync_remove_contact_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->impl_get_backend_property = ebb_etesync_get_backend_property;

	backend_sync_class = E_BOOK_BACKEND_SYNC_CLASS (klass);
	backend_sync_class->create_contacts_sync = ebb_etesync_create_contacts_sync;
	backend_sync_class->modify_contacts_sync = ebb_etesync_modify_contacts_sync;
	backend_sync_class->remove_contacts_sync = ebb_etesync_remove_contacts_sync;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = e_book_backend_etesync_dispose;
	object_class->finalize = e_book_backend_etesync_finalize;
	object_class->constructed = e_book_backend_etesync_constructed;
}
