/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-utils.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <libedata-book/libedata-book.h>
#include <libedata-cal/libedata-cal.h>
#include "e-etesync-defines.h"
#include "e-etesync-utils.h"

static const gchar *const collection_supported_types[] = {
			E_ETESYNC_COLLECTION_TYPE_ADDRESS_BOOK,
			E_ETESYNC_COLLECTION_TYPE_CALENDAR,
			E_ETESYNC_COLLECTION_TYPE_TASKS,
			E_ETESYNC_COLLECTION_TYPE_NOTES};
static const gchar *const collection_supported_types_default_names[] = {
			"My Contacts",
			"My Calendar",
			"My Tasks",
			"My Notes"};

void
e_etesync_utils_get_time_now (time_t *now)
{
	*now = g_get_real_time() / 1000;
}

gboolean
e_etesync_utils_get_component_uid_revision (const gchar *content,
					    gchar **out_component_uid,
					    gchar **out_revision)
{
	ICalComponent *vcalendar, *subcomp;
	gboolean success = FALSE;

	vcalendar = i_cal_component_new_from_string (content);

	*out_component_uid = NULL;
	*out_revision = NULL;

	for (subcomp = i_cal_component_get_first_component (vcalendar, I_CAL_ANY_COMPONENT);
	     subcomp && (!*out_component_uid || !*out_revision);
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vcalendar, I_CAL_ANY_COMPONENT)) {

		ICalComponentKind kind = i_cal_component_isa (subcomp);

		if (kind == I_CAL_VEVENT_COMPONENT ||
		    kind == I_CAL_VJOURNAL_COMPONENT ||
		    kind == I_CAL_VTODO_COMPONENT) {
			if (!*out_component_uid){
				*out_component_uid = g_strdup (i_cal_component_get_uid (subcomp));
				success = TRUE;
			}

			if (!*out_revision) {
				ICalProperty *prop;

				prop = i_cal_component_get_first_property (vcalendar, I_CAL_LASTMODIFIED_PROPERTY);
				if (prop) {
					ICalTime *itt;

					itt = i_cal_property_get_lastmodified (prop);
					*out_revision = i_cal_time_as_ical_string (itt);
					g_clear_object (&itt);
					g_object_unref (prop);
				} else {
					*out_revision = NULL;
				}
			}
		}
	}

	if (subcomp)
		g_object_unref (subcomp);
	g_object_unref (vcalendar);

	return success;
}

void
e_etesync_utils_get_contact_uid_revision (const gchar *content,
					  gchar **out_contact_uid,
					  gchar **out_revision)
{
	EContact *contact;

	contact = e_contact_new_from_vcard (content);

	if (contact) {
		*out_contact_uid = e_contact_get (contact, E_CONTACT_UID);
		*out_revision = e_contact_get (contact, E_CONTACT_REV);

		g_object_unref (contact);
	}
}

void
e_etesync_utils_set_io_gerror (EtebaseErrorCode etebase_error,
			       const gchar* etesync_message,
			       GError **error)
{
	if (etebase_error == ETEBASE_ERROR_CODE_TEMPORARY_SERVER_ERROR)
		return;

	g_clear_error (error);
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, etesync_message);
}

gchar *
e_etesync_utils_etebase_item_to_base64 (const EtebaseItem *item,
					EtebaseItemManager *item_mgr)
{
	gchar *item_cache_b64;
	guintptr item_cache_size;
	void *item_cache_blob;

	/* cache item as base64 in extra paremater as it will be used for modification and deletion */
	item_cache_blob = etebase_item_manager_cache_save (item_mgr, item, &item_cache_size);
	item_cache_b64 = g_malloc (ETEBASE_UTILS_TO_BASE64_MAX_LEN (item_cache_size));
	etebase_utils_to_base64 (item_cache_blob, item_cache_size, item_cache_b64,
				 ETEBASE_UTILS_TO_BASE64_MAX_LEN (item_cache_size));

	g_free (item_cache_blob);

	return item_cache_b64;
}

EtebaseItem *
e_etesync_utils_etebase_item_from_base64 (const gchar *item_cache_b64,
					  EtebaseItemManager *item_mgr)
{
	EtebaseItem *item;
	void *item_cache_blob;
	guintptr item_cache_size = 0, len;

	/* get cached item from base64 to EtebaseItem in extra paremater as it will be used for modification and deletion */
	len = ETEBASE_UTILS_FROM_BASE64_MAX_LEN (strlen (item_cache_b64));
	item_cache_blob = g_slice_alloc (len);
	etebase_utils_from_base64 (item_cache_b64, item_cache_blob, ETEBASE_UTILS_FROM_BASE64_MAX_LEN (strlen (item_cache_b64)), &item_cache_size);
	item = etebase_item_manager_cache_load (item_mgr, item_cache_blob, item_cache_size);

	g_slice_free1 (len, item_cache_blob);

	return item;
}

gchar *
e_etesync_utils_etebase_collection_to_base64 (const EtebaseCollection *collection,
					      EtebaseCollectionManager *col_mgr)
{
	gchar *collection_cache_b64;
	guintptr collection_cache_size;
	void *collection_cache_blob;

	collection_cache_blob = etebase_collection_manager_cache_save (col_mgr, collection, &collection_cache_size);
	collection_cache_b64 = g_malloc (ETEBASE_UTILS_TO_BASE64_MAX_LEN (collection_cache_size));
	etebase_utils_to_base64 (collection_cache_blob, collection_cache_size, collection_cache_b64,
				ETEBASE_UTILS_TO_BASE64_MAX_LEN (collection_cache_size));

	g_free (collection_cache_blob);

	return collection_cache_b64;
}

EtebaseCollection *
e_etesync_utils_etebase_collection_from_base64 (const gchar *collection_cache_b64,
						EtebaseCollectionManager *col_mgr)
{
	EtebaseCollection *collection;
	void *collection_cache_blob;
	guintptr collection_cache_size = 0, len;

	len = ETEBASE_UTILS_FROM_BASE64_MAX_LEN (strlen (collection_cache_b64));
	collection_cache_blob = g_slice_alloc (len);
	etebase_utils_from_base64 (collection_cache_b64, collection_cache_blob, ETEBASE_UTILS_FROM_BASE64_MAX_LEN (strlen (collection_cache_b64)), &collection_cache_size);
	collection = etebase_collection_manager_cache_load (col_mgr, collection_cache_blob, collection_cache_size);

	g_slice_free1 (len, collection_cache_blob);

	return collection;
}

const gchar *const *
e_etesync_util_get_collection_supported_types (void)
{
	return collection_supported_types;
}

const gchar *const *
e_etesync_util_get_collection_supported_types_default_names (void)
{
	return collection_supported_types_default_names;
}