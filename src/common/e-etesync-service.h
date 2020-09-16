/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-service.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_ETESYNC_SERVICE_H
#define E_ETESYNC_SERVICE_H

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

gboolean	e_etesync_service_store_credentials_sync
						(const gchar *uid,
						 const gchar *labal,
						 const ENamedParameters *credentials,
						 gboolean permanently,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_service_lookup_credentials_sync
						(const gchar *uid,
						 ENamedParameters **out_credentials,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_service_lookup_password_sync
						(const gchar *uid,
						 gchar **out_password,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_etesync_service_delete_sync 	(const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
G_END_DECLS

#endif /* E_ETESYNC_SERVICE_H */
