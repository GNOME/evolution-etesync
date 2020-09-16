/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-source-credentials-provider-impl-etesync.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>

#include "e-source-credentials-provider-impl-etesync.h"
#include "common/e-etesync-service.h"
#include "common/e-etesync-defines.h"

struct _ESourceCredentialsProviderImplEteSyncPrivate {
	gboolean dummy;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	ESourceCredentialsProviderImplEteSync,
	e_source_credentials_provider_impl_etesync,
	E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL,
	0,
	G_ADD_PRIVATE_DYNAMIC (ESourceCredentialsProviderImplEteSync))

/* ---------------------------STORE IN KEYERING----------------------- */

static gboolean
e_source_cpi_etesync_store_credentials_sync (ESource *source,
					     const ENamedParameters *credentials,
					     gboolean permanently,
					     GCancellable *cancellable,
					     GError **error)
{
	gboolean success;
	const gchar *uid;
	gchar *label;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (credentials != NULL, FALSE);

	uid = e_source_get_uid (source);
	label = e_source_dup_secret_label (source);

	success = e_etesync_service_store_credentials_sync (uid, label, credentials, permanently, cancellable, error);

	g_free (label);

	return success;
}

/* ---------------------------LOOKUP IN KEYERING----------------------- */

static gboolean
e_source_cpi_etesync_lookup_credentials_sync (ESource *source,
					      GCancellable *cancellable,
					      ENamedParameters **out_credentials,
					      GError **error)
{
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	uid = e_source_get_uid (source);

	return e_etesync_service_lookup_credentials_sync (uid, out_credentials, cancellable, error);
}

static gboolean
e_source_cpi_etesync_lookup_password_sync (ESource *source,
					   GCancellable *cancellable,
					   gchar **out_password,
					   GError **error)
{
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	uid = e_source_get_uid (source);

	return e_etesync_service_lookup_password_sync (uid, out_password, cancellable, error);
}

/* ---------------------------DELETE FROM KEYERING----------------------- */

static gboolean
e_source_cpi_etesync_delete_credentials_sync (ESource *source,
					      GCancellable *cancellable,
					      GError **error)
{
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	uid = e_source_get_uid (source);

	return e_etesync_service_delete_sync (uid, cancellable, error);
}

/* ------------------------------------------------------------------------------------- */

static gboolean
e_source_credentials_provider_impl_etesync_can_process (ESourceCredentialsProviderImpl *provider_impl,
							ESource *source)
{
	ESourceAuthentication *authentication_extension;

	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	authentication_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	if (!g_str_equal (e_source_authentication_get_method (authentication_extension), "EteSync"))
		return FALSE;

	return TRUE;
}

static gboolean
e_source_credentials_provider_impl_etesync_can_store (ESourceCredentialsProviderImpl *provider_impl)
{
	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC (provider_impl), FALSE);

	return TRUE;
}

static gboolean
e_source_credentials_provider_impl_etesync_can_prompt (ESourceCredentialsProviderImpl *provider_impl)
{
	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC (provider_impl), FALSE);

	return TRUE;
}

static gboolean
e_source_credentials_provider_impl_etesync_lookup_sync (ESourceCredentialsProviderImpl *provider_impl,
							ESource *source,
							GCancellable *cancellable,
							ENamedParameters **out_credentials,
							GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (out_credentials != NULL, FALSE);

	*out_credentials = NULL;

	if (!e_source_cpi_etesync_lookup_credentials_sync (source, cancellable, out_credentials, error)) {
		return FALSE;
	}

	if (!*out_credentials) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("Credentials not found"));
		e_named_parameters_free (*out_credentials);
		*out_credentials = NULL;
		return FALSE;
	}

	/* This means that this is the first time for authentication
	   in which we haven't yet stored the credentials as NamedParameters.
	   So we have to retrieve the password as text, we check if session key exists.
	   Ss normally we don't store the password itself in the NamedParameters */
	if (!e_named_parameters_exists (*out_credentials, E_ETESYNC_CREDENTIAL_SESSION_KEY)) {
		gchar *out_password = NULL;

		e_named_parameters_clear (*out_credentials);

		if (e_source_cpi_etesync_lookup_password_sync (source, cancellable, &out_password, error)) {
			e_named_parameters_set (*out_credentials, E_SOURCE_CREDENTIAL_PASSWORD, out_password);
		} else {
			success = FALSE;
		}
		e_util_safe_free_string (out_password);
	}

	return success;
}

static gboolean
e_source_credentials_provider_impl_etesync_store_sync (ESourceCredentialsProviderImpl *provider_impl,
						       ESource *source,
						       const ENamedParameters *credentials,
						       gboolean permanently,
						       GCancellable *cancellable,
						       GError **error)
{
	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (credentials != NULL, FALSE);

	return e_source_cpi_etesync_store_credentials_sync (source, credentials ,permanently, cancellable, error);
}

static gboolean
e_source_credentials_provider_impl_etesync_delete_sync (ESourceCredentialsProviderImpl *provider_impl,
							ESource *source,
							GCancellable *cancellable,
							GError **error)
{
	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_ETESYNC (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	return e_source_cpi_etesync_delete_credentials_sync (source, cancellable, error);
}

static void
e_source_credentials_provider_impl_etesync_dispose (GObject *object)
{
}

static void
e_source_credentials_provider_impl_etesync_class_init (ESourceCredentialsProviderImplEteSyncClass *klass)
{
	ESourceCredentialsProviderImplClass *impl_class;
	GObjectClass *object_class;

	impl_class = E_SOURCE_CREDENTIALS_PROVIDER_IMPL_CLASS (klass);
	impl_class->can_process = e_source_credentials_provider_impl_etesync_can_process;
	impl_class->can_store = e_source_credentials_provider_impl_etesync_can_store;
	impl_class->can_prompt = e_source_credentials_provider_impl_etesync_can_prompt;

	impl_class->lookup_sync = e_source_credentials_provider_impl_etesync_lookup_sync;
	impl_class->store_sync = e_source_credentials_provider_impl_etesync_store_sync;
	impl_class->delete_sync = e_source_credentials_provider_impl_etesync_delete_sync;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = e_source_credentials_provider_impl_etesync_dispose;
}

static void
e_source_credentials_provider_impl_etesync_class_finalize (ESourceCredentialsProviderImplEteSyncClass *klass)
{
}

static void
e_source_credentials_provider_impl_etesync_init (ESourceCredentialsProviderImplEteSync *provider_impl)
{
	provider_impl->priv = e_source_credentials_provider_impl_etesync_get_instance_private (provider_impl);
}

void
e_source_credentials_provider_impl_etesync_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_source_credentials_provider_impl_etesync_register_type (type_module);
}
