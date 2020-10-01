/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-source-etesync.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <etesync.h>
#include "e-source-etesync.h"

struct _ESourceEteSyncPrivate {
	gchar *journal_id;
	gint32 color;
	gchar *description;
};

enum {
	PROP_0,
	PROP_COLOR,
	PROP_DESCRIPTION,
	PROP_JOURNAL_ID
};

G_DEFINE_TYPE_WITH_PRIVATE (ESourceEteSync, e_source_etesync, E_TYPE_SOURCE_EXTENSION)

static void
source_etesync_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_COLOR:
			e_source_etesync_set_journal_color (
				E_SOURCE_ETESYNC (object),
				g_value_get_int (value));
			return;

		case PROP_DESCRIPTION:
			e_source_etesync_set_journal_description (
				E_SOURCE_ETESYNC (object),
				g_value_get_string (value));
			return;

		case PROP_JOURNAL_ID:
			e_source_etesync_set_journal_id (
				E_SOURCE_ETESYNC (object),
				g_value_get_string (value));
			return;


	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_etesync_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_COLOR:
			g_value_set_int (
				value,
				e_source_etesync_get_journal_color (
				E_SOURCE_ETESYNC (object)));
			return;

		case PROP_DESCRIPTION:
			g_value_take_string (
				value,
				e_source_etesync_dup_journal_description (
				E_SOURCE_ETESYNC (object)));
			return;

		case PROP_JOURNAL_ID:
			g_value_take_string (
				value,
				e_source_etesync_dup_journal_id (
				E_SOURCE_ETESYNC (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_etesync_finalize (GObject *object)
{
	ESourceEteSyncPrivate *priv;

	priv = e_source_etesync_get_instance_private (E_SOURCE_ETESYNC (object));

	g_free (priv->journal_id);
	g_free (priv->description);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_etesync_parent_class)->finalize (object);
}

static void
e_source_etesync_class_init (ESourceEteSyncClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_etesync_set_property;
	object_class->get_property = source_etesync_get_property;
	object_class->finalize = source_etesync_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_ETESYNC;

	g_object_class_install_property (
		object_class,
		PROP_JOURNAL_ID,
		g_param_spec_string (
			"journal-id",
			"Journal ID",
			"This is the journal ID, used when submitting changes or getting data using EteSync API",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_DESCRIPTION,
		g_param_spec_string (
			"description",
			"Description",
			"Description of the journal",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_COLOR,
		g_param_spec_int (
			"color",
			"Color",
			"Color of the EteSync journal resource",
			G_MININT, G_MAXINT, ETESYNC_COLLECTION_DEFAULT_COLOR,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_etesync_init (ESourceEteSync *extension)
{
	extension->priv = e_source_etesync_get_instance_private (extension);
}

void
e_source_etesync_type_register (GTypeModule *type_module)
{
	/* We need to ensure this is registered, because it's looked up
	 * by name in e_source_get_extension(). */
	g_type_ensure (E_TYPE_SOURCE_ETESYNC);
}

const gchar *
e_source_etesync_get_journal_id	(ESourceEteSync *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);

	return extension->priv->journal_id;
}

gchar *
e_source_etesync_dup_journal_id	(ESourceEteSync *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);
	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_etesync_get_journal_id (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_etesync_set_journal_id	(ESourceEteSync *extension,
				 const gchar *journal_id)
{
	g_return_if_fail (E_IS_SOURCE_ETESYNC (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->journal_id, journal_id) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->journal_id);
	extension->priv->journal_id = e_util_strdup_strip (journal_id);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "journal-id");
}

const gchar *
e_source_etesync_get_journal_description (ESourceEteSync *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);

	return extension->priv->description;
}

gchar *
e_source_etesync_dup_journal_description (ESourceEteSync *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_etesync_get_journal_description (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_etesync_set_journal_description (ESourceEteSync *extension,
					  const gchar *description)
{
	g_return_if_fail (E_IS_SOURCE_ETESYNC (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->description, description) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->description);
	extension->priv->description = e_util_strdup_strip (description);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "description");
}

gint32
e_source_etesync_get_journal_color (ESourceEteSync *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), ETESYNC_COLLECTION_DEFAULT_COLOR);

	return extension->priv->color;
}

void
e_source_etesync_set_journal_color (ESourceEteSync *extension,
				    const gint32 color)
{
	g_return_if_fail (E_IS_SOURCE_ETESYNC (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	extension->priv->color = color;

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "color");
}
