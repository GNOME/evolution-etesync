/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-backend-etesync.c - EteSync contact backend.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <libedataserver/libedataserver.h>
#include <etebase.h>

#include "common/e-source-etesync.h"
#include "common/e-etesync-connection.h"
#include "common/e-etesync-utils.h"
#include "common/e-etesync-defines.h"
#include "e-book-backend-etesync.h"

struct _EBookBackendEteSyncPrivate {
	EEteSyncConnection *connection;
	EtebaseCollection *col_obj;
	GRecMutex etesync_lock;

	gboolean fetch_from_server;

	GSList *preloaded_add; /* EBookMetaBackendInfo * */
	GSList *preloaded_modify; /* EBookMetaBackendInfo * */
	GSList *preloaded_delete; /* EBookMetaBackendInfo * */
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendEteSync, e_book_backend_etesync, E_TYPE_BOOK_META_BACKEND)

/* must be freed with g_object_unref */
static ESource *
ebb_etesync_ref_collection_source (EBookBackendEteSync *bbetesync)
{
	ESource *source;
	ESourceRegistry *registry;

	source = e_backend_get_source (E_BACKEND (bbetesync));
	registry = e_book_backend_get_registry (E_BOOK_BACKEND (bbetesync));
	return e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);
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
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	if (e_etesync_connection_is_connected (bbetesync->priv->connection)) {
		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
		success = TRUE;
	} else {
		if (!bbetesync->priv->connection) {
			ESource *collection = ebb_etesync_ref_collection_source (bbetesync);
			bbetesync->priv->connection = e_etesync_connection_new (collection);
			g_object_unref (collection);
		}

		if (e_etesync_connection_reconnect_sync (bbetesync->priv->connection, out_auth_result, cancellable, error))
			*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	}

	if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED && !bbetesync->priv->col_obj) {
		gboolean is_read_only;
		ESource *source = e_backend_get_source (E_BACKEND (bbetesync));

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
			ESourceEteSync *etesync_extension;

			etesync_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
			bbetesync->priv->col_obj = e_etesync_utils_etebase_collection_from_base64 (
								e_source_etesync_get_etebase_collection_b64 (etesync_extension),
								e_etesync_connection_get_collection_manager (bbetesync->priv->connection));
		}

		success = bbetesync->priv->col_obj? TRUE : FALSE;

		if (success) {
			is_read_only = etebase_collection_get_access_level (bbetesync->priv->col_obj) == ETEBASE_COLLECTION_ACCESS_LEVEL_READ_ONLY;
			e_book_backend_set_writable (E_BOOK_BACKEND (bbetesync), !is_read_only);
		}
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

static gboolean
ebb_etesync_list_existing_sync (EBookMetaBackend *meta_backend,
				gchar **out_new_sync_tag,
				GSList **out_existing_objects, /* EBookMetaBackendInfo * */
				GCancellable *cancellable,
				GError **error)
{
	EBookBackendEteSync *bbetesync;
	EEteSyncConnection *connection;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);
	connection = bbetesync->priv->connection;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	success = e_etesync_connection_list_existing_sync (connection,
							   E_BACKEND (meta_backend),
							   E_ETESYNC_ADDRESSBOOK,
							   bbetesync->priv->col_obj,
							   out_new_sync_tag,
							   out_existing_objects,
							   cancellable,
							   error);

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
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
	EEteSyncConnection *connection;
	EBookCache *book_cache;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;
	*out_new_sync_tag = NULL;

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);
	connection = bbetesync->priv->connection;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	/* Must add preloaded */
	*out_created_objects = bbetesync->priv->preloaded_add;
	*out_modified_objects = bbetesync->priv->preloaded_modify;
	*out_removed_objects = bbetesync->priv->preloaded_delete;

	bbetesync->priv->preloaded_add = NULL;
	bbetesync->priv->preloaded_modify = NULL;
	bbetesync->priv->preloaded_delete = NULL;

	if (bbetesync->priv->fetch_from_server) {
		book_cache = e_book_meta_backend_ref_cache (meta_backend);

		if (book_cache) {
			success = e_etesync_connection_get_changes_sync (connection,
									 E_BACKEND (meta_backend),
									 E_ETESYNC_ADDRESSBOOK,
									 last_sync_tag,
									 bbetesync->priv->col_obj,
									 E_CACHE (book_cache),
									 out_new_sync_tag,
									 out_created_objects,
									 out_modified_objects,
									 out_removed_objects,
									 cancellable,
									 error);
			g_object_unref (book_cache);
		}
	}

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
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
	gchar *content;
	const gchar *uid;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);
	g_return_val_if_fail (out_new_extra != NULL, FALSE);

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);
	connection = bbetesync->priv->connection;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	content = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	uid = e_contact_get_const (contact, E_CONTACT_UID);

	if (overwrite_existing) {
		success = e_etesync_connection_item_upload_sync (connection, E_BACKEND (meta_backend), bbetesync->priv->col_obj,
				E_ETESYNC_ITEM_ACTION_MODIFY, content, uid, extra, NULL, out_new_extra, cancellable, error);
	} else {
		success = e_etesync_connection_item_upload_sync (connection, E_BACKEND (meta_backend), bbetesync->priv->col_obj,
				E_ETESYNC_ITEM_ACTION_CREATE, content, uid, NULL, NULL, out_new_extra, cancellable, error);
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

	g_return_val_if_fail (E_IS_BOOK_BACKEND_ETESYNC (meta_backend), FALSE);

	bbetesync = E_BOOK_BACKEND_ETESYNC (meta_backend);
	connection = bbetesync->priv->connection;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	success = e_etesync_connection_item_upload_sync (connection, E_BACKEND (meta_backend), bbetesync->priv->col_obj,
				E_ETESYNC_ITEM_ACTION_DELETE, NULL, uid, extra, NULL, NULL, cancellable, error);

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
}

/* --------------------Batch Functions-------------------- */
static gboolean
ebb_etesync_create_modify_contacts_sync (EBookBackendSync *backend,
					 const gchar * const *vcards,
					 GSList **out_contacts,
					 const EteSyncAction action,
					 GCancellable *cancellable,
					 GError **error)
{
	EBookBackendEteSync *bbetesync;
	EEteSyncConnection *connection;
	guint length, batch_length, batch_number = 0;
	gboolean success = TRUE;

	length = g_strv_length ((gchar **) vcards);

	bbetesync = E_BOOK_BACKEND_ETESYNC (backend);
	connection = bbetesync->priv->connection;
	*out_contacts = NULL;

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);

	/* extract the components and mass-add them to the server */
	while (length > 0 && success) {
		GSList *batch_contacts = NULL; /* EContact */
		GSList *batch_info = NULL; /* EBookMetaBackendInfo */
		gchar *content[E_ETESYNC_ITEM_PUSH_LIMIT];
		guint ii;

		batch_length = length > E_ETESYNC_ITEM_PUSH_LIMIT ? E_ETESYNC_ITEM_PUSH_LIMIT : length;
		length -= batch_length;

		for (ii = 0; ii < batch_length; ii++) {
			EContact *contact;

			contact = e_contact_new_from_vcard (vcards[ii + (batch_number * E_ETESYNC_ITEM_PUSH_LIMIT)]);

			/* Preserve original UID, create a unique UID if needed */
			if (e_contact_get_const (contact, E_CONTACT_UID) == NULL) {
				gchar *uid = e_util_generate_uid ();
				e_contact_set (contact, E_CONTACT_UID, uid);
				g_free (uid);
			}

			content[ii] = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			batch_contacts = g_slist_prepend (batch_contacts, contact);
		}

		if (action == E_ETESYNC_ITEM_ACTION_CREATE) {
			success = e_etesync_connection_batch_create_sync (connection,
									  E_BACKEND (E_BOOK_META_BACKEND (bbetesync)),
									  bbetesync->priv->col_obj,
									  E_ETESYNC_ADDRESSBOOK,
									  (const gchar *const*) content,
									  batch_length, /* length of content */
									  &batch_info,
									  cancellable,
									  error);

		} else if (action == E_ETESYNC_ITEM_ACTION_MODIFY) {
			EBookCache *book_cache;

			EBookMetaBackend *meta_backend = E_BOOK_META_BACKEND (bbetesync);
			book_cache = e_book_meta_backend_ref_cache (meta_backend);

			if (book_cache) {
				success = e_etesync_connection_batch_modify_sync (connection,
										  E_BACKEND (E_BOOK_META_BACKEND (bbetesync)),
										  bbetesync->priv->col_obj,
										  E_ETESYNC_ADDRESSBOOK,
										  (const gchar *const*) content,
										  NULL,
										  batch_length, /* length of content */
										  E_CACHE (book_cache), /* uses book_cache if type is addressbook */
										  &batch_info,
										  cancellable,
										  error);
				g_object_unref (book_cache);
			} else
				success = FALSE;
		}

		if (success) {
			if (action == E_ETESYNC_ITEM_ACTION_CREATE)
				bbetesync->priv->preloaded_add = g_slist_concat (batch_info, bbetesync->priv->preloaded_add);
			else if (action == E_ETESYNC_ITEM_ACTION_MODIFY)
				bbetesync->priv->preloaded_modify = g_slist_concat (batch_info, bbetesync->priv->preloaded_modify);
			*out_contacts = g_slist_concat (batch_contacts, *out_contacts);
		} else {
			g_slist_free_full (batch_contacts, g_object_unref);
			g_slist_free_full (batch_info, e_book_meta_backend_info_free);
		}

		for (ii = 0; ii < batch_length; ii++)
			g_free (content[ii]);
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

	return ebb_etesync_create_modify_contacts_sync (backend, vcards, out_contacts, E_ETESYNC_ITEM_ACTION_CREATE, cancellable, error);
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

	return ebb_etesync_create_modify_contacts_sync (backend, vcards, out_contacts, E_ETESYNC_ITEM_ACTION_MODIFY, cancellable, error);
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
	guint length, batch_length, batch_number = 0;
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

	/* extract the components and mass-add them to the server;
	   eventually remember them in the cbetesync->priv and use them
	   in the refresh/get_changes_sync(), instead of re-download them */
	while (length > 0 && success) {
		GSList *batch_contacts_id = NULL; /* gchar */
		GSList *batch_info = NULL; /* EBookMetaBackendInfo */
		gchar *content[E_ETESYNC_ITEM_PUSH_LIMIT];
		guint ii;

		batch_length = length > E_ETESYNC_ITEM_PUSH_LIMIT ? E_ETESYNC_ITEM_PUSH_LIMIT : length;
		length -= batch_length;
		*out_removed_uids = NULL;

		for (ii = 0; ii < batch_length; ii++) {
			const gchar 		*id;
			EContact		*contact;

			id = uids [ii + (batch_number * E_ETESYNC_ITEM_PUSH_LIMIT)];

			e_book_cache_get_contact (book_cache, id, FALSE, &contact, cancellable, NULL);

			content[ii] = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			batch_contacts_id = g_slist_prepend (batch_contacts_id, g_strdup (id));

			g_object_unref (contact);
		}

		success = e_etesync_connection_batch_delete_sync (connection,
								  E_BACKEND (E_BOOK_META_BACKEND (bbetesync)),
								  bbetesync->priv->col_obj,
								  E_ETESYNC_ADDRESSBOOK,
								  (const gchar *const*) content,
								  NULL,
								  batch_length, /* length of content */
								  E_CACHE (book_cache), /* uses book_cache if type is addressbook */
								  &batch_info,
								  cancellable,
								  error);

		if (success) {
			bbetesync->priv->preloaded_delete = g_slist_concat (batch_info, bbetesync->priv->preloaded_delete);
			*out_removed_uids = g_slist_concat (batch_contacts_id, *out_removed_uids);
		} else {
			g_slist_free_full (batch_contacts_id, g_object_unref);
			g_slist_free_full (batch_info, e_book_meta_backend_info_free);
		}

		for (ii = 0; ii < batch_length; ii++)
			g_free (content[ii]);
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
	g_object_unref (book_cache);

	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	return success;
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
e_book_backend_etesync_finalize (GObject *object)
{
	EBookBackendEteSync *bbetesync = E_BOOK_BACKEND_ETESYNC (object);

	g_rec_mutex_lock (&bbetesync->priv->etesync_lock);
	g_clear_object (&bbetesync->priv->connection);
	g_clear_pointer (&bbetesync->priv->col_obj, etebase_collection_destroy);
	g_rec_mutex_unlock (&bbetesync->priv->etesync_lock);

	g_rec_mutex_clear (&bbetesync->priv->etesync_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_etesync_parent_class)->finalize (object);
}

static void
e_book_backend_etesync_constructed (GObject *object)
{
	ESource *collection;
	EBookBackendEteSync *bbetesync = E_BOOK_BACKEND_ETESYNC (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_book_backend_etesync_parent_class)->constructed (object);

	collection = ebb_etesync_ref_collection_source (bbetesync);
	bbetesync->priv->connection = e_etesync_connection_new (collection);

	g_object_unref (collection);
}

static void
e_book_backend_etesync_init (EBookBackendEteSync *bbetesync)
{
	bbetesync->priv = e_book_backend_etesync_get_instance_private (bbetesync);
	g_rec_mutex_init (&bbetesync->priv->etesync_lock);
	bbetesync->priv->connection = NULL;
	bbetesync->priv->col_obj = NULL;
	/* coverity[missing_lock] */
	bbetesync->priv->fetch_from_server = TRUE;
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
#ifdef HAVE_BACKEND_MODULE_DIRECTORY
	book_meta_backend_class->backend_module_directory = BACKENDDIR;
#endif
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
	object_class->constructed = e_book_backend_etesync_constructed;
	object_class->finalize = e_book_backend_etesync_finalize;
}
