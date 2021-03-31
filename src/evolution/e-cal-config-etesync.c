/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-cal-config-etesync.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include "e-cal-config-etesync.h"
#include "common/e-etesync-connection.h"
#include "common/e-etesync-service.h"
#include "common/e-etesync-utils.h"
#include "common/e-etesync-defines.h"
#include "common/e-source-etesync.h"

G_DEFINE_DYNAMIC_TYPE (
	ECalConfigEteSync,
	e_cal_config_etesync,
	E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
cal_config_etesync_allow_creation (ESourceConfigBackend *backend)
{
	ESourceConfig *config;
	ECalSourceConfig *cal_config;
	ECalClientSourceType source_type;
	gboolean allow_creation = FALSE;

	config = e_source_config_backend_get_config (backend);

	cal_config = E_CAL_SOURCE_CONFIG (config);
	source_type = e_cal_source_config_get_source_type (cal_config);

	switch (source_type) {
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
		case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
			allow_creation = TRUE;
			break;

		default:
			break;
	}

	return allow_creation;
}

static void
cal_config_etesync_insert_widgets (ESourceConfigBackend *backend,
			           ESource *scratch_source)
{
	if (!scratch_source)
		return;

	e_source_config_add_refresh_interval (e_source_config_backend_get_config (backend), scratch_source);
}

static void
cal_config_etesync_commit_changes (ESourceConfigBackend *backend,
				   ESource *scratch_source)
{
	ESource *collection_source;
	ESourceConfig *config;

	config = e_source_config_backend_get_config (backend);
	collection_source = e_source_config_get_collection_source (config);

	/* Get collection source */
	if (collection_source) {
		EEteSyncConnection *connection;
		ENamedParameters *credentials = NULL;
		const gchar *collection_uid;

		collection_uid = e_source_get_uid (collection_source);
		connection = e_etesync_connection_new (collection_source);

		/* Get credentials and set connection object */
		if (e_etesync_service_lookup_credentials_sync (collection_uid, &credentials, NULL, NULL) &&
		    e_etesync_connection_set_connection_from_sources (connection, credentials)) {
			const gchar *extension_name = NULL;
			const gchar *col_type = NULL;

			if (e_source_has_extension (scratch_source, E_SOURCE_EXTENSION_CALENDAR)) {
				extension_name = E_SOURCE_EXTENSION_CALENDAR;
				col_type = E_ETESYNC_COLLECTION_TYPE_CALENDAR;
			}

			if (e_source_has_extension (scratch_source, E_SOURCE_EXTENSION_TASK_LIST)) {
				extension_name = E_SOURCE_EXTENSION_TASK_LIST;
				col_type = E_ETESYNC_COLLECTION_TYPE_TASKS;
			}

			if (e_source_has_extension (scratch_source, E_SOURCE_EXTENSION_MEMO_LIST)) {
				extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
				col_type = E_ETESYNC_COLLECTION_TYPE_NOTES;
			}

			if (col_type) {
				ESourceBackend *source_backend;
				ESourceEteSync *etesync_extension;
				gchar *display_name = NULL;
				gchar *color = g_strdup (E_ETESYNC_COLLECTION_DEFAULT_COLOR);
				const gchar *source_color = NULL;
				EtebaseCollection *col_obj;
				EtebaseCollectionManager *col_mgr;

				display_name = e_source_dup_display_name (scratch_source);
				source_backend = e_source_get_extension (scratch_source, extension_name);
				source_color = e_source_selectable_get_color (E_SOURCE_SELECTABLE (source_backend));
				col_mgr = e_etesync_connection_get_collection_manager (connection);

				if (source_color) {
					g_free (color);
					color = g_strdup (source_color);
				}

				etesync_extension = e_source_get_extension (scratch_source, E_SOURCE_EXTENSION_ETESYNC);
				col_obj = e_etesync_utils_etebase_collection_from_base64 (
							e_source_etesync_get_etebase_collection_b64 (etesync_extension),
							col_mgr);

				/* push modification to server */
				if (e_etesync_connection_collection_modify_upload_sync (connection,
										       col_obj,
										       display_name,
										       NULL,
										       color,
										       NULL)) {
					e_source_etesync_set_etebase_collection_b64 (
							etesync_extension,
							e_etesync_utils_etebase_collection_to_base64 (col_obj, col_mgr));
				}

				etebase_collection_destroy (col_obj);
				g_free (display_name);
				g_free (color);
			}
		}

		g_object_unref (connection);
		g_clear_object (&collection_source);
		e_named_parameters_free (credentials);
	}
}

static void
e_cal_config_etesync_class_init (ECalConfigEteSyncClass *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_CAL_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "etesync";
	backend_class->allow_creation = cal_config_etesync_allow_creation;
	backend_class->insert_widgets = cal_config_etesync_insert_widgets;
	backend_class->commit_changes = cal_config_etesync_commit_changes;
}

static void
e_cal_config_etesync_class_finalize (ECalConfigEteSyncClass *class)
{
}

static void
e_cal_config_etesync_init (ECalConfigEteSync *backend)
{
}

void
e_cal_config_etesync_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_cal_config_etesync_register_type (type_module);
}
