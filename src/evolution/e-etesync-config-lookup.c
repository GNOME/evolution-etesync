/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-etesync-config-lookup.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>
#include <e-util/e-util.h>
#include "common/e-etesync-defines.h"
#include "common/e-etesync-connection.h"
#include "common/e-etesync-utils.h"
#include "e-etesync-config-lookup.h"
#include <etebase.h>

/* Standard GObject macros */
#define E_TYPE_ETESYNC_CONFIG_LOOKUP \
	(e_etesync_config_lookup_get_type ())
#define E_ETESYNC_CONFIG_LOOKUP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_ETESYNC_CONFIG_LOOKUP, EEteSyncConfigLookup))
#define E_ETESYNC_CONFIG_LOOKUP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_ETESYNC_CONFIG_LOOKUP, EEteSyncConfigLookupClass))
#define E_IS_ETESYNC_CONFIG_LOOKUP(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_ETESYNC_CONFIG_LOOKUP))
#define E_IS_ETESYNC_CONFIG_LOOKUP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_ETESYNC_CONFIG_LOOKUP))
#define E_ETESYNC_CONFIG_LOOKUP_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_ETESYNC_CONFIG_LOOKUP, EEteSyncConfigLookupClass))

typedef struct _EEteSyncConfigLookup EEteSyncConfigLookup;
typedef struct _EEteSyncConfigLookupClass EEteSyncConfigLookupClass;

struct _EEteSyncConfigLookup {
	EExtension parent;
};

struct _EEteSyncConfigLookupClass {
	EExtensionClass parent_class;
};

GType e_etesync_config_lookup_get_type (void) G_GNUC_CONST;

static void etesync_config_lookup_worker_iface_init (EConfigLookupWorkerInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EEteSyncConfigLookup, e_etesync_config_lookup, E_TYPE_EXTENSION, 0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_CONFIG_LOOKUP_WORKER, etesync_config_lookup_worker_iface_init))

static const gchar *
etesync_config_lookup_worker_get_display_name (EConfigLookupWorker *lookup_worker)
{
	return _("Look up for an EteSync account");
}

static void
etesync_config_lookup_worker_result (EConfigLookupWorker *lookup_worker,
				     EConfigLookup *config_lookup,
				     const gchar *user,
				     const ENamedParameters *params)
{
	const gchar *servers;
	EConfigLookupResult *lookup_result;

	g_return_if_fail (E_IS_ETESYNC_CONFIG_LOOKUP (lookup_worker));
	g_return_if_fail (E_IS_CONFIG_LOOKUP (config_lookup));

	servers = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_SERVERS);

	lookup_result = e_config_lookup_result_simple_new (E_CONFIG_LOOKUP_RESULT_COLLECTION,
		E_CONFIG_LOOKUP_RESULT_PRIORITY_POP3,
		TRUE, "etesync",
		_("EteSync account"),
		_("EteSync end-to-end encrypts your contacts, calendars, memos and tasks."),
		params && e_named_parameters_exists (params, E_CONFIG_LOOKUP_PARAM_PASSWORD) &&
		e_named_parameters_exists (params, E_CONFIG_LOOKUP_PARAM_REMEMBER_PASSWORD) ?
		e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_PASSWORD) : NULL);

	e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_COLLECTION,
		"backend-name", "etesync");

	e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_COLLECTION,
		"identity", user);

	e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_AUTHENTICATION,
		"user", user);

	e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_AUTHENTICATION,
		"method", "EteSync");

	if (servers && *servers) {
		e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_COLLECTION,
			"contacts-url", servers);
		e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_COLLECTION,
			"calendar-url", servers);
	} else {
		e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_COLLECTION,
			"contacts-url", etebase_get_default_server_url ());
		e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_COLLECTION,
			"calendar-url", etebase_get_default_server_url ());
	}

	e_config_lookup_add_result (config_lookup, lookup_result);
}

static gboolean
etesync_config_lookup_discover (const ENamedParameters *params,
				GError **error)
{
	gboolean success = TRUE;
	const gchar *email_address, *server_url, *default_server_url;
	EtebaseClient *etebase_client;

	email_address = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_EMAIL_ADDRESS);
	server_url = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_SERVERS);
	default_server_url = etebase_get_default_server_url ();

	if (!email_address)
		return FALSE;

	if (!server_url || !*server_url)
		server_url = default_server_url;

	etebase_client = etebase_client_new (PACKAGE "/" VERSION, server_url);

	if (etebase_client) {
		gint32 etebase_server_check = 0;

		if (!g_str_equal (server_url, default_server_url))
			etebase_server_check = etebase_client_check_etebase_server (etebase_client);

		/* Returns 0 if client is pointing an etebase server, 1 if not, -1 on error */
		if (etebase_server_check != 0) {
			if (etebase_server_check == 1)
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Etebase server not found."));
			else
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed connecting to server."));
			success = FALSE;

		} else if (!e_named_parameters_exists (params, E_CONFIG_LOOKUP_PARAM_PASSWORD)) {
			g_set_error_literal (error, E_CONFIG_LOOKUP_WORKER_ERROR, E_CONFIG_LOOKUP_WORKER_ERROR_REQUIRES_PASSWORD,
				_("Requires password to continue."));

			success = FALSE;
		} else {
			EtebaseErrorCode etebase_error;
			EEteSyncConnection *connection;
			const gchar *password;

			connection = e_etesync_connection_new (NULL);
			password = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_PASSWORD);

			if (e_etesync_connection_login_connection_sync (connection, email_address, password, server_url, &etebase_error)) {

				/* The connection was successfully set, but need to check permession denied error using token valid */
				if (e_etesync_connection_check_session_key_validation_sync (connection, &etebase_error, NULL) != E_SOURCE_AUTHENTICATION_ACCEPTED)
					success = FALSE;

				etebase_account_logout (e_etesync_connection_get_etebase_account (connection));
			} else
				success = FALSE;

			if (!success) {
				if (etebase_error == ETEBASE_ERROR_CODE_UNAUTHORIZED)
					g_set_error_literal (error, E_CONFIG_LOOKUP_WORKER_ERROR, E_CONFIG_LOOKUP_WORKER_ERROR_REQUIRES_PASSWORD, etebase_error_get_message ());
				else
					e_etesync_utils_set_io_gerror (etebase_error, etebase_error_get_message (), error);
			}

			g_object_unref (connection);
		}

		etebase_client_destroy (etebase_client);
	} else {
		if (server_url) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				_("Malformed server name, please make sure to enter a full url (e.g https://etesync.example.com)."));
		}

		success = FALSE;
	}

	return success;
}

static void
etesync_config_lookup_worker_run (EConfigLookupWorker *lookup_worker,
				  EConfigLookup *config_lookup,
				  const ENamedParameters *params,
				  ENamedParameters **out_restart_params,
				  GCancellable *cancellable,
				  GError **error)
{
	const gchar *email_address;

	g_return_if_fail (E_IS_ETESYNC_CONFIG_LOOKUP (lookup_worker));
	g_return_if_fail (E_IS_CONFIG_LOOKUP (config_lookup));
	g_return_if_fail (params != NULL);

	email_address = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_EMAIL_ADDRESS);

	if (!email_address || !*email_address)
		return;

	if (etesync_config_lookup_discover (params, error))
		etesync_config_lookup_worker_result (lookup_worker, config_lookup, email_address, params);

	if (out_restart_params && !*out_restart_params)
		*out_restart_params = e_named_parameters_new_clone (params);
}

static void
etesync_config_lookup_constructed (GObject *object)
{
	EConfigLookup *config_lookup;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_etesync_config_lookup_parent_class)->constructed (object);

	config_lookup = E_CONFIG_LOOKUP (e_extension_get_extensible (E_EXTENSION (object)));

	e_config_lookup_register_worker (config_lookup, E_CONFIG_LOOKUP_WORKER (object));
}

static void
e_etesync_config_lookup_class_init (EEteSyncConfigLookupClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = etesync_config_lookup_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_CONFIG_LOOKUP;
}

static void
e_etesync_config_lookup_class_finalize (EEteSyncConfigLookupClass *class)
{
}

static void
etesync_config_lookup_worker_iface_init (EConfigLookupWorkerInterface *iface)
{
	iface->get_display_name = etesync_config_lookup_worker_get_display_name;
	iface->run = etesync_config_lookup_worker_run;
}

static void
e_etesync_config_lookup_init (EEteSyncConfigLookup *extension)
{
}

void
e_etesync_config_lookup_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_etesync_config_lookup_register_type (type_module);
}
