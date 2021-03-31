/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cal-backend-etesync-factory.c - EteSync calendar backend factory.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>

#include "e-cal-backend-etesync.h"
#include "common/e-source-etesync.h"

#define FACTORY_NAME "etesync"

typedef ECalBackendFactory ECalBackendEteSyncEventsFactory;
typedef ECalBackendFactoryClass ECalBackendEteSyncEventsFactoryClass;

typedef ECalBackendFactory ECalBackendEteSyncTodosFactory;
typedef ECalBackendFactoryClass ECalBackendEteSyncTodosFactoryClass;

typedef ECalBackendFactory ECalBackendEteSyncJournalFactory;
typedef ECalBackendFactoryClass ECalBackendEteSyncJournalFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_etesync_events_factory_get_type (void);
GType e_cal_backend_etesync_todos_factory_get_type (void);
GType e_cal_backend_etesync_journal_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendEteSyncEventsFactory,
	e_cal_backend_etesync_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendEteSyncTodosFactory,
	e_cal_backend_etesync_todos_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendEteSyncJournalFactory,
	e_cal_backend_etesync_journal_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_etesync_events_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_ETESYNC;
}

static void
e_cal_backend_etesync_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_etesync_events_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_etesync_todos_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_ETESYNC;
}

static void
e_cal_backend_etesync_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_etesync_todos_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_etesync_journal_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VJOURNAL_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_ETESYNC;
}

static void
e_cal_backend_etesync_journal_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_etesync_journal_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, ETESYNC_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_module = E_MODULE (type_module);

	e_source_etesync_type_register (type_module);

	e_cal_backend_etesync_events_factory_register_type (type_module);
	e_cal_backend_etesync_todos_factory_register_type (type_module);
	e_cal_backend_etesync_journal_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
