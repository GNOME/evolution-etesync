/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  e-etesync-backend-factory.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include "e-etesync-backend-factory.h"
#include "e-etesync-backend.h"

G_DEFINE_DYNAMIC_TYPE (
	EEteSyncBackendFactory,
	e_etesync_backend_factory,
	E_TYPE_COLLECTION_BACKEND_FACTORY)

static void
e_etesync_backend_factory_class_init (EEteSyncBackendFactoryClass *class)
{
	ECollectionBackendFactoryClass *factory_class;

	factory_class = E_COLLECTION_BACKEND_FACTORY_CLASS (class);
	factory_class->factory_name = "etesync";
	factory_class->backend_type = E_TYPE_ETESYNC_BACKEND;
}

static void
e_etesync_backend_factory_class_finalize (EEteSyncBackendFactoryClass *class)
{
}

static void
e_etesync_backend_factory_init (EEteSyncBackendFactory *factory)
{
}

void
e_etesync_backend_factory_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_etesync_backend_factory_register_type (type_module);
}
