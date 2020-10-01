/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-book-config-etesync.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include "e-book-config-etesync.h"

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
