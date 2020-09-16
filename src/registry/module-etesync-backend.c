/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  module-etesync-backend.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>

#include "e-etesync-backend.h"
#include "e-etesync-backend-factory.h"
#include "common/e-source-etesync.h"
#include "common/e-source-etesync-account.h"

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, ETESYNC_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_etesync_backend_factory_type_register (type_module);

	e_source_etesync_type_register (type_module);
	e_source_etesync_account_type_register (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}
