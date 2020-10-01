/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-cal-config-etesync.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include "e-cal-config-etesync.h"

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
