/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-book-config-etesync.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include "e-book-config-etesync.h"
#include "common/e-etesync-connection.h"
#include "common/e-etesync-service.h"
#include "common/e-etesync-utils.h"
#include "common/e-etesync-defines.h"
#include "common/e-source-etesync.h"

G_DEFINE_DYNAMIC_TYPE (
	EBookConfigEteSync,
	e_book_config_etesync,
	E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
book_config_etesync_allow_creation (ESourceConfigBackend *backend)
{
	return TRUE;
}

static void
book_config_etesync_insert_widgets (ESourceConfigBackend *backend,
				    ESource *scratch_source)
{
	if (!scratch_source)
		return;

	e_source_config_add_refresh_interval (e_source_config_backend_get_config (backend), scratch_source);
}

static void
book_config_etesync_commit_changes (ESourceConfigBackend *backend,
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

		/* Get credentials and Set connection object */
		if (e_etesync_service_lookup_credentials_sync (collection_uid, &credentials, NULL, NULL) &&
		    e_source_has_extension (scratch_source, E_SOURCE_EXTENSION_ADDRESS_BOOK) &&
		    e_etesync_connection_set_connection_from_sources (connection, credentials)) {
			ESourceEteSync *etesync_extension;
			gchar *display_name = NULL;
			EtebaseCollection *col_obj;
			EtebaseCollectionManager *col_mgr;

			col_mgr = e_etesync_connection_get_collection_manager (connection);
			display_name = e_source_dup_display_name (scratch_source);
			etesync_extension = e_source_get_extension (scratch_source, E_SOURCE_EXTENSION_ETESYNC);
			col_obj = e_etesync_utils_etebase_collection_from_base64 (
						e_source_etesync_get_etebase_collection_b64 (etesync_extension),
						col_mgr);

			/* push modification to server */
			if (e_etesync_connection_collection_modify_upload_sync (connection,
										col_obj,
										display_name,
										NULL,
										NULL,
										NULL)) {
				e_source_etesync_set_etebase_collection_b64 (
							etesync_extension,
							e_etesync_utils_etebase_collection_to_base64 (col_obj, col_mgr));
			}

			etebase_collection_destroy (col_obj);
			g_free (display_name);
		}

		g_object_unref (connection);
		g_clear_object (&collection_source);
		e_named_parameters_free (credentials);
	}
}

static void
e_book_config_etesync_class_init (EBookConfigEteSyncClass *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_BOOK_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "etesync";
	backend_class->allow_creation = book_config_etesync_allow_creation;
	backend_class->insert_widgets = book_config_etesync_insert_widgets;
	backend_class->commit_changes = book_config_etesync_commit_changes;
}

static void
e_book_config_etesync_class_finalize (EBookConfigEteSyncClass *class)
{
}

static void
e_book_config_etesync_init (EBookConfigEteSync *backend)
{
}

void
e_book_config_etesync_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_book_config_etesync_register_type (type_module);
}
