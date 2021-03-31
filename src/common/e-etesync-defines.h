/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-etesync-defines.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_ETESYNC_DEFINES_H
#define E_ETESYNC_DEFINES_H

typedef enum {
	E_ETESYNC_ITEM_ACTION_CREATE,
	E_ETESYNC_ITEM_ACTION_MODIFY,
	E_ETESYNC_ITEM_ACTION_DELETE,
} EteSyncAction;

typedef enum {
	E_ETESYNC_ADDRESSBOOK,
	E_ETESYNC_CALENDAR
} EteSyncType;

#define E_ETESYNC_CREDENTIAL_SESSION_KEY "session_key"

#define E_ETESYNC_COLLECTION_TYPE_CALENDAR "etebase.vevent"
#define E_ETESYNC_COLLECTION_TYPE_ADDRESS_BOOK "etebase.vcard"
#define E_ETESYNC_COLLECTION_TYPE_TASKS "etebase.vtodo"
#define E_ETESYNC_COLLECTION_TYPE_NOTES "etebase.md.note"

#define E_ETESYNC_COLLECTION_DEFAULT_COLOR "#8BC34A"

#define E_ETESYNC_COLLECTION_FETCH_LIMIT 30
#define E_ETESYNC_ITEM_FETCH_LIMIT 50
#define E_ETESYNC_ITEM_PUSH_LIMIT 30

#endif /* E_ETESYNC_DEFINES_H */
