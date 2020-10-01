/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-backend-etesync-factory.c - EteSync contact backend factory.
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>

#include "e-book-backend-etesync.h"
#include "common/e-source-etesync.h"

#define FACTORY_NAME "etesync"

typedef EBookBackendFactory EBookBackendEteSyncFactory;
typedef EBookBackendFactoryClass EBookBackendEteSyncFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_book_backend_etesync_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EBookBackendEteSyncFactory,
	e_book_backend_etesync_factory,
	E_TYPE_BOOK_BACKEND_FACTORY)

static void
e_book_backend_etesync_factory_class_init (EBookBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->backend_type = E_TYPE_BOOK_BACKEND_ETESYNC;
}

static void
e_book_backend_etesync_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_etesync_factory_init (EBookBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, ETESYNC_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_module = E_MODULE (type_module);

	e_source_etesync_type_register (type_module);
	e_book_backend_etesync_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
