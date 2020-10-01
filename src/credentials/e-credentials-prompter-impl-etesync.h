/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-credentials-prompter-impl-etesync.h
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CREDENTIALS_PROMPTER_IMPL_ETESYNC_H
#define E_CREDENTIALS_PROMPTER_IMPL_ETESYNC_H

#include <libedataserverui/libedataserverui.h>

/* Standard GObject macros */
#define E_TYPE_CREDENTIALS_PROMPTER_IMPL_ETESYNC \
	(e_credentials_prompter_impl_etesync_get_type ())
#define E_CREDENTIALS_PROMPTER_IMPL_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_ETESYNC, ECredentialsPrompterImplEteSync))
#define E_CREDENTIALS_PROMPTER_IMPL_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL_ETESYNC, ECredentialsPrompterImplEteSyncClass))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_ETESYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_ETESYNC))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_ETESYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL_ETESYNC))
#define E_CREDENTIALS_PROMPTER_IMPL_ETESYNC_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_ETESYNC, ECredentialsPrompterImplEteSyncClass))

G_BEGIN_DECLS

typedef struct _ECredentialsPrompterImplEteSync ECredentialsPrompterImplEteSync;
typedef struct _ECredentialsPrompterImplEteSyncClass ECredentialsPrompterImplEteSyncClass;
typedef struct _ECredentialsPrompterImplEteSyncPrivate ECredentialsPrompterImplEteSyncPrivate;

/*
 * ECredentialsPrompterImplEteSync:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 */
struct _ECredentialsPrompterImplEteSync {
	ECredentialsPrompterImpl parent;
	ECredentialsPrompterImplEteSyncPrivate *priv;
};

struct _ECredentialsPrompterImplEteSyncClass {
	ECredentialsPrompterImplClass parent_class;
};

GType		e_credentials_prompter_impl_etesync_get_type
						(void) G_GNUC_CONST;
void		e_credentials_prompter_impl_etesync_type_register
						(GTypeModule *type_module);
ECredentialsPrompterImpl *
		e_credentials_prompter_impl_etesync_new
						(void);
G_END_DECLS

#endif /* E_CREDENTIALS_PROMPTER_IMPL_ETESYNC_H */
