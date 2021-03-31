/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-utils.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_ETESYNC_UTILS_H
#define E_ETESYNC_UTILS_H

#include <libedataserver/libedataserver.h>
#include <etebase.h>

#define EETESYNC_UTILS_SUPPORTED_TYPES_SIZE 4

/* Collection indexs in 'collection_supported_types' */
enum {
	COLLECTION_INDEX_TYPE_ADDRESSBOOK,
	COLLECTION_INDEX_TYPE_CALENDAR,
	COLLECTION_INDEX_TYPE_TASKS,
	COLLECTION_INDEX_TYPE_NOTES
};

G_BEGIN_DECLS

void	e_etesync_utils_get_time_now	(time_t *now);
gboolean	e_etesync_utils_get_component_uid_revision
						(const gchar *content,
						 gchar **out_component_uid,
						 gchar **out_revision);
void		e_etesync_utils_get_contact_uid_revision
						(const gchar *content,
						 gchar **out_contact_uid,
						 gchar **out_revision);
void		e_etesync_utils_set_io_gerror	(EtebaseErrorCode etesync_error,
						 const gchar* etesync_message,
						 GError **error);
gchar *		e_etesync_utils_etebase_item_to_base64
						(const EtebaseItem *item,
						 EtebaseItemManager *item_mgr);
EtebaseItem *	e_etesync_utils_etebase_item_from_base64
						(const gchar *item_cache_b64,
						 EtebaseItemManager *item_mgr);
gchar *		e_etesync_utils_etebase_collection_to_base64
						(const EtebaseCollection *collection,
						 EtebaseCollectionManager *col_mgr);
EtebaseCollection *
		e_etesync_utils_etebase_collection_from_base64
						(const gchar *collection_cache_b64,
						 EtebaseCollectionManager *col_mgr);
const gchar *const *
		e_etesync_util_get_collection_supported_types (void);
const gchar *const *
		e_etesync_util_get_collection_supported_types_default_names (void);

G_END_DECLS
#endif /* E_ETESYNC_UTILS_H */
