/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-source-etesync.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_SOURCE_ETESYNC_H
#define E_SOURCE_ETESYNC_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_ETESYNC \
	(e_source_etesync_get_type ())
#define E_SOURCE_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_ETESYNC, ESourceEteSync))
#define E_SOURCE_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_ETESYNC, ESourceEteSyncClass))
#define E_IS_SOURCE_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_ETESYNC))
#define E_IS_SOURCE_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_ETESYNC))
#define E_SOURCE_ETESYNC_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_ETESYNC, ESourceEteSyncClass))

#define E_SOURCE_EXTENSION_ETESYNC "EteSync Backend"

G_BEGIN_DECLS

typedef struct _ESourceEteSync ESourceEteSync;
typedef struct _ESourceEteSyncClass ESourceEteSyncClass;
typedef struct _ESourceEteSyncPrivate ESourceEteSyncPrivate;

struct _ESourceEteSync {
	ESourceExtension parent;
	ESourceEteSyncPrivate *priv;
};

struct _ESourceEteSyncClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_etesync_get_type	(void) G_GNUC_CONST;
void		e_source_etesync_type_register  (GTypeModule *type_module);
const gchar *	e_source_etesync_get_collection_id
						(ESourceEteSync *extension);
gchar *		e_source_etesync_dup_collection_id
						(ESourceEteSync *extension);
void		e_source_etesync_set_collection_id
						(ESourceEteSync *extension,
						 const gchar *collection_id);
const gchar *	e_source_etesync_get_collection_description
						(ESourceEteSync *extension);
gchar *		e_source_etesync_dup_collection_description
						(ESourceEteSync *extension);
void		e_source_etesync_set_collection_description
						(ESourceEteSync *extension,
						 const gchar *description);
const gchar *	e_source_etesync_get_collection_color
						(ESourceEteSync *extension);
gchar *		e_source_etesync_dup_collection_color
						(ESourceEteSync *extension);
void		e_source_etesync_set_collection_color
						(ESourceEteSync *extension,
						 const gchar *color);
const gchar *	e_source_etesync_get_etebase_collection_b64
						(ESourceEteSync *extension);
gchar *		e_source_etesync_dup_etebase_collection_b64
						(ESourceEteSync *extension);
void		e_source_etesync_set_etebase_collection_b64
						(ESourceEteSync *extension,
						 const gchar *etebase_collection_b64);
G_END_DECLS

#endif /* E_SOURCE_ETESYNC_H */
