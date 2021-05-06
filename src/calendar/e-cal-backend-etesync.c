/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cal-backend-etesync.c - EteSync calendar backend.
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
#include "e-cal-backend-etesync.h"

struct _ECalBackendEteSyncPrivate {
	EEteSyncConnection *connection;
	EtebaseCollection *col_obj;
	GRecMutex etesync_lock;

	gboolean fetch_from_server;

	GSList *preloaded_add; /* ECalMetaBackendInfo * */
	GSList *preloaded_modify; /* ECalMetaBackendInfo * */
	GSList *preloaded_delete; /* ECalMetaBackendInfo * */
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendEteSync, e_cal_backend_etesync, E_TYPE_CAL_META_BACKEND)

/* must be freed with g_object_unref */
static ESource *
ecb_etesync_ref_collection_source (ECalBackendEteSync *cbetesync)
{
	ESource *source;
	ESourceRegistry *registry;

	source = e_backend_get_source (E_BACKEND (cbetesync));
	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbetesync));
	return e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);
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
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	if (e_etesync_connection_is_connected (cbetesync->priv->connection)) {
		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
		success = TRUE;
	} else {

		if (!cbetesync->priv->connection) {
			ESource *collection = ecb_etesync_ref_collection_source (cbetesync);
			cbetesync->priv->connection = e_etesync_connection_new (collection);
			g_object_unref (collection);
		}

		if (e_etesync_connection_reconnect_sync (cbetesync->priv->connection, out_auth_result, cancellable, error))
			*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	}

	if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED && !cbetesync->priv->col_obj) {
		gboolean is_read_only;
		ESource *source = e_backend_get_source (E_BACKEND (cbetesync));

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_ETESYNC)) {
			ESourceEteSync *etesync_extension;

			etesync_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ETESYNC);
			cbetesync->priv->col_obj = e_etesync_utils_etebase_collection_from_base64 (
								e_source_etesync_get_etebase_collection_b64 (etesync_extension),
								e_etesync_connection_get_collection_manager (cbetesync->priv->connection));
		}

		success = cbetesync->priv->col_obj? TRUE : FALSE;

		if (success) {
			is_read_only = etebase_collection_get_access_level (cbetesync->priv->col_obj) == ETEBASE_COLLECTION_ACCESS_LEVEL_READ_ONLY;
			e_cal_backend_set_writable (E_CAL_BACKEND (cbetesync), !is_read_only);
		}
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
ecb_etesync_list_existing_sync (ECalMetaBackend *meta_backend,
				gchar **out_new_sync_tag,
				GSList **out_existing_objects,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendEteSync *cbetesync;
	EEteSyncConnection *connection;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);
	connection = cbetesync->priv->connection;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	success = e_etesync_connection_list_existing_sync (connection,
							   E_BACKEND (meta_backend),
							   E_ETESYNC_CALENDAR,
							   cbetesync->priv->col_obj,
							   out_new_sync_tag,
							   out_existing_objects,
							   cancellable,
							   error);

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return success;
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
	EEteSyncConnection *connection;
	ECalCache *cal_cache;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;
	*out_new_sync_tag = NULL;

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);
	connection = cbetesync->priv->connection;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* Must add preloaded */
	*out_created_objects = cbetesync->priv->preloaded_add;
	*out_modified_objects = cbetesync->priv->preloaded_modify;
	*out_removed_objects = cbetesync->priv->preloaded_delete;

	cbetesync->priv->preloaded_add = NULL;
	cbetesync->priv->preloaded_modify = NULL;
	cbetesync->priv->preloaded_delete = NULL;

	if (cbetesync->priv->fetch_from_server) {
		cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

		if (cal_cache) {
			success = e_etesync_connection_get_changes_sync (connection,
									 E_BACKEND (meta_backend),
									 E_ETESYNC_CALENDAR,
									 last_sync_tag,
									 cbetesync->priv->col_obj,
									 E_CACHE (cal_cache),
									 out_new_sync_tag,
									 out_created_objects,
									 out_modified_objects,
									 out_removed_objects,
									 cancellable,
									 error);
			g_object_unref (cal_cache);
		}
	}

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return success;
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
				 const gchar *extra, /* item_uid */
				 guint32 opflags,
				 gchar **out_new_uid,
				 gchar **out_new_extra,
				 GCancellable *cancellable,
				 GError **error)
{
	ECalBackendEteSync *cbetesync;
	EEteSyncConnection *connection;
	gboolean success = FALSE;
	ICalComponent *vcalendar = NULL;
	gchar *content;
	const gchar *uid;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);
	g_return_val_if_fail (out_new_extra != NULL, FALSE);

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);
	connection = cbetesync->priv->connection;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	vcalendar = e_cal_meta_backend_merge_instances (meta_backend, instances, TRUE);

	if (!vcalendar) {
		g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);
		return FALSE;
	}

	content = i_cal_component_as_ical_string (vcalendar);
	uid = i_cal_component_get_uid (vcalendar);

	if (overwrite_existing) {
		success = e_etesync_connection_item_upload_sync (connection, E_BACKEND (meta_backend), cbetesync->priv->col_obj,
			E_ETESYNC_ITEM_ACTION_MODIFY, content, uid, extra, NULL, out_new_extra, cancellable, error);
	} else {
		success = e_etesync_connection_item_upload_sync (connection, E_BACKEND (meta_backend), cbetesync->priv->col_obj,
			E_ETESYNC_ITEM_ACTION_CREATE, content, uid, NULL, out_new_uid, out_new_extra, cancellable, error);
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
				   const gchar *extra, /* item_uid */
				   const gchar *object,
				   guint32 opflags,
				   GCancellable *cancellable,
				   GError **error)
{
	ECalBackendEteSync *cbetesync;
	EEteSyncConnection *connection;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_ETESYNC (meta_backend), FALSE);

	cbetesync = E_CAL_BACKEND_ETESYNC (meta_backend);
	connection = cbetesync->priv->connection;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	success = e_etesync_connection_item_upload_sync (connection, E_BACKEND (meta_backend), cbetesync->priv->col_obj,
				E_ETESYNC_ITEM_ACTION_DELETE, NULL, uid, extra, NULL, NULL, cancellable, error);

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
				 GSList **out_uids,
				 GSList **out_new_components,
				 GError **error)
{
	ECalBackendEteSync *cbetesync;
	EEteSyncConnection *connection;
	gboolean success = TRUE;
	const GSList *l;

	g_return_if_fail (E_IS_CAL_BACKEND_ETESYNC (backend));

	if (!calobjs || !calobjs->next) {
		/* Chain up to parent's method. */
		E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_etesync_parent_class)->create_objects_sync (backend, cal, cancellable, calobjs, opflags, out_uids, out_new_components, error);
		return;
	}

	cbetesync = E_CAL_BACKEND_ETESYNC (backend);
	connection = cbetesync->priv->connection;
	*out_uids = NULL;
	*out_new_components = NULL;
	l = calobjs;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* extract the components and mass-add them to the server "batch by batch" */
	while (l && success) {
		gchar *content[E_ETESYNC_ITEM_PUSH_LIMIT];
		GSList *batch_uids = NULL; /* gchar* */
		GSList *batch_components= NULL; /* ECalComponent* */
		GSList *batch_info = NULL; /* ECalMetaBackendInfo* */
		guint ii,  batch_length = 0;

		/* Data Preproccessing */
		for (ii = 0 ; ii < E_ETESYNC_ITEM_PUSH_LIMIT && l; l = l->next, ii++) {
			ICalComponent *icomp, *vcal;
			ECalComponent *comp;
			ICalTime *current;
			gchar *comp_uid;

			/* Parse the icalendar text */
			icomp = i_cal_parser_parse_string ((gchar *) l->data);
			if (!icomp) {
				success = FALSE;
				break;
			}
			comp = e_cal_component_new_from_icalcomponent (icomp);

			/* Preserve original UID, create a unique UID if needed */
			if (!i_cal_component_get_uid (icomp)) {
				gchar *new_uid = e_util_generate_uid ();
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

			comp_uid = g_strdup (i_cal_component_get_uid (icomp));
			batch_components = g_slist_prepend (batch_components, e_cal_component_clone (comp));
			batch_uids = g_slist_prepend (batch_uids, comp_uid);

			g_object_unref (comp);
		}

		batch_length = ii;

		if (success) {
			success = e_etesync_connection_batch_create_sync (connection,
									  E_BACKEND (cbetesync),
									  cbetesync->priv->col_obj,
									  E_ETESYNC_CALENDAR,
									  (const gchar *const*) content,
									  batch_length, /* length of content */
									  &batch_info,
									  cancellable,
									  error);

			if (success) {
				cbetesync->priv->preloaded_add = g_slist_concat (batch_info, cbetesync->priv->preloaded_add);
				*out_new_components = g_slist_concat (*out_new_components, batch_components);
				*out_uids = g_slist_concat (*out_uids, batch_uids);
			} else {
				g_slist_free_full (batch_components, g_object_unref);
				g_slist_free_full (batch_uids, g_free);
				g_slist_free_full (batch_info, e_cal_meta_backend_info_free);
			}

		}

		for (ii = 0; ii < batch_length; ii++)
			g_free (content[ii]);
	}

	if (success) {
		cbetesync->priv->fetch_from_server = FALSE;
		e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), cancellable, error);
		cbetesync->priv->fetch_from_server = TRUE;
	} else {
		g_slist_free_full (*out_new_components, g_object_unref);
		g_slist_free_full (*out_uids, g_free);
		*out_new_components = NULL;
		*out_uids = NULL;
	}

	/* free any data related to this bulk operation */
	cbetesync->priv->preloaded_add = NULL;

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
				 GSList **out_old_components,
				 GSList **out_new_components,
				 GError **error)
{
	ECalBackendEteSync *cbetesync;
	ECalCache *cal_cache;
	EEteSyncConnection *connection;
	gboolean success = TRUE;
	const GSList *l;

	g_return_if_fail (E_IS_CAL_BACKEND_ETESYNC (backend));

	if (!calobjs || !calobjs->next) {
		/* Chain up to parent's method. */
		E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_etesync_parent_class)->modify_objects_sync (backend, cal, cancellable, calobjs, mod, opflags, out_old_components, out_new_components, error);
		return;
	}

	cbetesync = E_CAL_BACKEND_ETESYNC (backend);
	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbetesync));
	connection = cbetesync->priv->connection;
	*out_old_components = NULL;
	*out_new_components = NULL;
	l = calobjs;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* extract the components and mass-add them to the server "batch by batch" */
	while (l && success) {
		gchar *data_uids[E_ETESYNC_ITEM_PUSH_LIMIT];
		gchar *content[E_ETESYNC_ITEM_PUSH_LIMIT];
		GSList *batch_out_old_components = NULL; /* ECalComponent* */
		GSList *batch_out_new_components= NULL; /* ECalComponent* */
		GSList *batch_info = NULL; /* ECalMetaBackendInfo* */
		guint ii,  batch_length = 0;

		/* Data Preproccessing */
		for (ii = 0 ; ii < E_ETESYNC_ITEM_PUSH_LIMIT && l; l = l->next, ii++) {
			ICalComponent *icomp, *vcal;
			ECalComponent *comp;
			ICalTime *current;
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

			data_uids[ii] = g_strdup (i_cal_component_get_uid (icomp));
			batch_out_new_components = g_slist_prepend (batch_out_new_components, e_cal_component_clone (comp));

			if (e_cal_cache_get_components_by_uid (cal_cache, data_uids[ii], &instances, NULL, NULL))
				batch_out_old_components = g_slist_concat (batch_out_old_components, instances);

			g_object_unref (comp);
		}

		batch_length = ii;

		if (success) {
			success = e_etesync_connection_batch_modify_sync (connection,
									  E_BACKEND (cbetesync),
									  cbetesync->priv->col_obj,
									  E_ETESYNC_CALENDAR,
									  (const gchar *const*) content,
									  (const gchar *const*) data_uids,
									  batch_length, /* length of batch */
									  E_CACHE (cal_cache), /* uses cal_cache if type is calendar */
									  &batch_info,
									  cancellable,
									  error);

			if (success) {
				cbetesync->priv->preloaded_modify = g_slist_concat (batch_info, cbetesync->priv->preloaded_modify);
				*out_new_components = g_slist_concat (*out_new_components, batch_out_new_components);
				*out_old_components = g_slist_concat (*out_old_components, batch_out_old_components);
			} else {
				g_slist_free_full (batch_out_new_components, g_object_unref);
				g_slist_free_full (batch_out_old_components, g_object_unref);
				g_slist_free_full (batch_info, e_cal_meta_backend_info_free);
			}
		}

		for (ii = 0; ii < batch_length; ii++) {
			g_free (content[ii]);
			g_free (data_uids[ii]);
		}
	}

	if (success) {
		cbetesync->priv->fetch_from_server = FALSE;
		e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), cancellable, error);
		cbetesync->priv->fetch_from_server = TRUE;
	} else {
		g_slist_free_full (*out_new_components, g_object_unref);
		g_slist_free_full (*out_old_components, g_object_unref);
		*out_new_components = NULL;
		*out_old_components = NULL;
	}

	/* free any data related to this bulk operation */
	cbetesync->priv->preloaded_modify = NULL;
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
				 GSList **out_old_components,
				 GSList **out_new_components,
				 GError **error)
{
	ECalBackendEteSync *cbetesync;
	ECalCache *cal_cache;
	EEteSyncConnection *connection;
	gboolean success = TRUE;
	const GSList *l;

	g_return_if_fail (E_IS_CAL_BACKEND_ETESYNC (backend));

	if (!ids || !ids->next) {
		/* Chain up to parent's method. */
		E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_etesync_parent_class)->remove_objects_sync (backend, cal, cancellable, ids, mod, opflags, out_old_components, out_new_components, error);
		return;
	}

	cbetesync = E_CAL_BACKEND_ETESYNC (backend);
	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbetesync));
	connection = cbetesync->priv->connection;
	*out_old_components = NULL;
	*out_new_components = NULL;
	l = ids;

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);

	/* extract the components and mass-add them to the server "batch by batch" */
	while (l && success) {
		gchar *data_uids[E_ETESYNC_ITEM_PUSH_LIMIT];
		gchar *content[E_ETESYNC_ITEM_PUSH_LIMIT];
		GSList *batch_out_old_components = NULL; /* ECalComponent* */
		GSList *batch_info = NULL; /* ECalMetaBackendInfo* */
		guint ii,  batch_length = 0;

		/* Data Preproccessing */
		for (ii = 0; ii < E_ETESYNC_ITEM_PUSH_LIMIT && l; l = l->next, ii++) {
			ICalComponent *vcal;
			GSList *instances;

			if (e_cal_cache_get_components_by_uid (cal_cache, e_cal_component_id_get_uid (l->data), &instances, cancellable, NULL)) {
				vcal = e_cal_meta_backend_merge_instances (E_CAL_META_BACKEND (cbetesync), instances, TRUE);
				content[ii] = i_cal_component_as_ical_string (vcal);
				data_uids[ii] = g_strdup (e_cal_component_id_get_uid (l->data));
				g_object_unref (vcal);
			} else {
				success = FALSE;
				break;
			}

			batch_out_old_components = g_slist_concat (batch_out_old_components, instances);
			*out_new_components = g_slist_prepend (*out_new_components, NULL);
		}

		batch_length = ii;

		if (success) {
			success = e_etesync_connection_batch_delete_sync (connection,
									  E_BACKEND (cbetesync),
									  cbetesync->priv->col_obj,
									  E_ETESYNC_CALENDAR,
									  (const gchar *const*) content,
									  (const gchar *const*) data_uids,
									  batch_length, /* length of batch */
									  E_CACHE (cal_cache), /* uses cal_cache if type is calendar */
									  &batch_info,
									  cancellable,
									  error);

			if (success) {
				cbetesync->priv->preloaded_delete = g_slist_concat (batch_info, cbetesync->priv->preloaded_delete);
				*out_old_components = g_slist_concat (*out_old_components, batch_out_old_components);
			} else {
				g_slist_free_full (batch_out_old_components, g_object_unref);
				g_slist_free_full (batch_info, e_cal_meta_backend_info_free);
			}
		}

		for (ii = 0; ii < batch_length; ii++) {
			g_free (content[ii]);
			g_free (data_uids[ii]);
		}
	}

	if (success) {
		cbetesync->priv->fetch_from_server = FALSE;
		e_cal_meta_backend_refresh_sync (E_CAL_META_BACKEND (cbetesync), cancellable, error);
		cbetesync->priv->fetch_from_server = TRUE;
	} else {
		g_slist_free_full (*out_old_components, g_object_unref);
		*out_old_components = NULL;
	}

	/* free any data related to this bulk operation */
	cbetesync->priv->preloaded_delete = NULL;
	g_object_unref (cal_cache);

	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	return;
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
			#ifdef HAVE_SIMPLE_MEMO_WITH_SUMMARY_CAPABILITY
				E_CAL_STATIC_CAPABILITY_SIMPLE_MEMO_WITH_SUMMARY,
			#else
				E_CAL_STATIC_CAPABILITY_SIMPLE_MEMO,
			#endif
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cbetesync)),
			NULL);
	}  else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		return NULL;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_etesync_parent_class)->impl_get_backend_property (cal_backend, prop_name);
}

static void
e_cal_backend_etesync_finalize (GObject *object)
{
	ECalBackendEteSync *cbetesync = E_CAL_BACKEND_ETESYNC (object);

	g_rec_mutex_lock (&cbetesync->priv->etesync_lock);
	g_clear_object (&cbetesync->priv->connection);
	g_clear_pointer (&cbetesync->priv->col_obj, etebase_collection_destroy);
	g_rec_mutex_unlock (&cbetesync->priv->etesync_lock);

	g_rec_mutex_clear (&cbetesync->priv->etesync_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_etesync_parent_class)->finalize (object);
}

static void
e_cal_backend_etesync_constructed (GObject *object)
{
	ESource *collection;
	ECalBackendEteSync *cbetesync = E_CAL_BACKEND_ETESYNC (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_cal_backend_etesync_parent_class)->constructed (object);

	collection = ecb_etesync_ref_collection_source (cbetesync);
	cbetesync->priv->connection = e_etesync_connection_new (collection);

	g_object_unref (collection);
}

static void
e_cal_backend_etesync_init (ECalBackendEteSync *cbetesync)
{
	cbetesync->priv = e_cal_backend_etesync_get_instance_private (cbetesync);
	g_rec_mutex_init (&cbetesync->priv->etesync_lock);
	cbetesync->priv->connection = NULL;
	cbetesync->priv->col_obj = NULL;
	/* coverity[missing_lock] */
	cbetesync->priv->fetch_from_server = TRUE;
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
	object_class->constructed = e_cal_backend_etesync_constructed;
	object_class->finalize = e_cal_backend_etesync_finalize;
}
