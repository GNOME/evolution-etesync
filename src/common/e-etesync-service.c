/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-service.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>
#include "e-etesync-service.h"

gboolean
e_etesync_service_store_credentials_sync (const gchar *uid,
					  const gchar *label,
					  const ENamedParameters *credentials,
					  gboolean permanently,
					  GCancellable *cancellable,
					  GError **error)
{
	gboolean success = FALSE;
	gchar *secret = NULL;

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (label != NULL, FALSE);
	g_return_val_if_fail (credentials != NULL, FALSE);

	secret = e_named_parameters_to_string (credentials);

	if (secret)
		success = e_secret_store_store_sync (uid, secret, label, permanently, cancellable, error);

	e_util_safe_free_string (secret);

	return success;
}

gboolean
e_etesync_service_lookup_credentials_sync (const gchar *uid,
					   ENamedParameters **out_credentials,
					   GCancellable *cancellable,
					   GError **error)
{
	gboolean success = FALSE;
	gchar *secret;

	g_return_val_if_fail (uid != NULL, FALSE);

	if (!e_secret_store_lookup_sync (uid, &secret, cancellable, error))
		return FALSE;

	if (!secret) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("EteSync secret not found"));
		return FALSE;
	}

	*out_credentials = e_named_parameters_new_string (secret);

	if (*out_credentials)
		success = TRUE;

	e_util_safe_free_string (secret);

	return success;
}

gboolean
e_etesync_service_lookup_password_sync (const gchar *uid,
					gchar **out_password,
					GCancellable *cancellable,
					GError **error)
{
	gboolean success = FALSE;
	gchar *secret;

	g_return_val_if_fail (uid != NULL, FALSE);

	if (!e_secret_store_lookup_sync (uid, &secret, cancellable, error))
		return FALSE;

	if (!secret) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("EteSync secret not found"));
		return FALSE;
	}

	if (out_password) {
		*out_password = g_strdup (secret);
		success = TRUE;
	}

	e_util_safe_free_string (secret);

	return success;
}

gboolean
e_etesync_service_delete_sync (const gchar *uid,
			       GCancellable *cancellable,
			       GError **error)
{
	g_return_val_if_fail (uid != NULL, FALSE);

	return e_secret_store_delete_sync (uid, cancellable, error);;
}
