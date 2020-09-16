/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-source-etesync.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include "e-etesync-defines.h"
#include "e-source-etesync.h"

struct _ESourceEteSyncPrivate {
	gchar *collection_id;
	gchar *color;
	gchar *description;
	gchar *etebase_collection_b64;
};

enum {
	PROP_0,
	PROP_COLOR,
	PROP_DESCRIPTION,
	PROP_COLLECTION_ID,
	PROP_ETEBASE_COLLECTION_B64
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
			e_source_etesync_set_collection_color (
				E_SOURCE_ETESYNC (object),
				g_value_get_string (value));
			return;

		case PROP_DESCRIPTION:
			e_source_etesync_set_collection_description (
				E_SOURCE_ETESYNC (object),
				g_value_get_string (value));
			return;

		case PROP_COLLECTION_ID:
			e_source_etesync_set_collection_id (
				E_SOURCE_ETESYNC (object),
				g_value_get_string (value));
			return;
		case PROP_ETEBASE_COLLECTION_B64:
			e_source_etesync_set_etebase_collection_b64 (
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
			g_value_take_string (
				value,
				e_source_etesync_dup_collection_color (
				E_SOURCE_ETESYNC (object)));
			return;

		case PROP_DESCRIPTION:
			g_value_take_string (
				value,
				e_source_etesync_dup_collection_description (
				E_SOURCE_ETESYNC (object)));
			return;

		case PROP_COLLECTION_ID:
			g_value_take_string (
				value,
				e_source_etesync_dup_collection_id (
				E_SOURCE_ETESYNC (object)));
			return;
		case PROP_ETEBASE_COLLECTION_B64:
			g_value_take_string (
				value,
				e_source_etesync_dup_etebase_collection_b64 (
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

	g_free (priv->collection_id);
	g_free (priv->description);
	g_free (priv->color);
	g_free (priv->etebase_collection_b64);

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
		PROP_COLLECTION_ID,
		g_param_spec_string (
			"collection-id",
			"Collection ID",
			"This is the collection ID, used when submitting changes or getting data using EteSync API",
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
			"Description of the collection",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_COLOR,
		g_param_spec_string (
			"color",
			"Color",
			"Color of the EteSync collection resource",
			E_ETESYNC_COLLECTION_DEFAULT_COLOR,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_ETEBASE_COLLECTION_B64,
		g_param_spec_string (
			"etebase-collection",
			"Etebase Collection B64",
			"Etebase collection object cached as string in base64",
			NULL,
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
e_source_etesync_get_collection_id (ESourceEteSync *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);

	return extension->priv->collection_id;
}

gchar *
e_source_etesync_dup_collection_id (ESourceEteSync *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);
	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_etesync_get_collection_id (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_etesync_set_collection_id (ESourceEteSync *extension,
				    const gchar *collection_id)
{
	g_return_if_fail (E_IS_SOURCE_ETESYNC (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->collection_id, collection_id) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->collection_id);
	extension->priv->collection_id = e_util_strdup_strip (collection_id);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "collection-id");
}

const gchar *
e_source_etesync_get_collection_description (ESourceEteSync *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);

	return extension->priv->description;
}

gchar *
e_source_etesync_dup_collection_description (ESourceEteSync *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_etesync_get_collection_description (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_etesync_set_collection_description (ESourceEteSync *extension,
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

const gchar *
e_source_etesync_get_collection_color (ESourceEteSync *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);

	return extension->priv->color;
}

gchar *
e_source_etesync_dup_collection_color (ESourceEteSync *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);
	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_etesync_get_collection_color (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_etesync_set_collection_color (ESourceEteSync *extension,
				       const gchar *color)
{
	g_return_if_fail (E_IS_SOURCE_ETESYNC (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->color, color) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->color);
	extension->priv->color = e_util_strdup_strip (color);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "color");
}

const gchar *
e_source_etesync_get_etebase_collection_b64 (ESourceEteSync *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);

	return extension->priv->etebase_collection_b64;
}

gchar *
e_source_etesync_dup_etebase_collection_b64 (ESourceEteSync *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_ETESYNC (extension), NULL);
	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_etesync_get_etebase_collection_b64 (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_etesync_set_etebase_collection_b64 (ESourceEteSync *extension,
					     const gchar *etebase_collection_b64)
{
	g_return_if_fail (E_IS_SOURCE_ETESYNC (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->etebase_collection_b64, etebase_collection_b64) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->etebase_collection_b64);
	extension->priv->etebase_collection_b64 = e_util_strdup_strip (etebase_collection_b64);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "etebase-collection");
}
