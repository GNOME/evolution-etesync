/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-source-etesync-account.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include "e-source-etesync-account.h"

struct _ESourceEteSyncAccountPrivate {
	gchar *collection_stoken;
};

enum {
	PROP_0,
	PROP_COLLECTION_STOKEN
};

G_DEFINE_TYPE_WITH_PRIVATE (ESourceEteSyncAccount, e_source_etesync_account, E_TYPE_SOURCE_EXTENSION)

static void
source_etesync_account_set_property (GObject *object,
				     guint property_id,
				     const GValue *value,
				     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_COLLECTION_STOKEN:
			e_source_etesync_account_set_collection_stoken (
				E_SOURCE_ETESYNC_ACCOUNT (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_etesync_account_get_property (GObject *object,
				     guint property_id,
				     GValue *value,
				     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_COLLECTION_STOKEN:
			g_value_take_string (
				value,
				e_source_etesync_account_dup_collection_stoken (
				E_SOURCE_ETESYNC_ACCOUNT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_etesync_account_finalize (GObject *object)
{
	ESourceEteSyncAccount *etesync_account = E_SOURCE_ETESYNC_ACCOUNT (object);

	g_free (etesync_account->priv->collection_stoken);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_source_etesync_account_parent_class)->finalize (object);
}

static void
e_source_etesync_account_class_init (ESourceEteSyncAccountClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_etesync_account_set_property;
	object_class->get_property = source_etesync_account_get_property;
	object_class->finalize = source_etesync_account_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_ETESYNC_ACCOUNT;

	g_object_class_install_property (
		object_class,
		PROP_COLLECTION_STOKEN,
		g_param_spec_string (
			"collection-stoken",
			"Collection stoken",
			"This is the account collection stoken, used to get changes,"
			"instead of getting the whole list of existing collections",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_etesync_account_init (ESourceEteSyncAccount *extension)
{
	extension->priv = e_source_etesync_account_get_instance_private (extension);
}

void
e_source_etesync_account_type_register (GTypeModule *type_module)
{
	/* We need to ensure this is registered, because it's looked up
	 * by name in e_source_get_extension(). */
	g_type_ensure (E_TYPE_SOURCE_ETESYNC_ACCOUNT);
}

const gchar *
e_source_etesync_account_get_collection_stoken (ESourceEteSyncAccount *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ETESYNC_ACCOUNT (extension), NULL);

	return extension->priv->collection_stoken;
}

gchar *
e_source_etesync_account_dup_collection_stoken (ESourceEteSyncAccount *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_ETESYNC_ACCOUNT (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_etesync_account_get_collection_stoken (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_etesync_account_set_collection_stoken (ESourceEteSyncAccount *extension,
						const gchar *collection_stoken)
{
	g_return_if_fail (E_IS_SOURCE_ETESYNC_ACCOUNT (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->collection_stoken, collection_stoken) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->collection_stoken);
	extension->priv->collection_stoken = e_util_strdup_strip (collection_stoken);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "collection-stoken");
}
