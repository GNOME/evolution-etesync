/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-credentials-prompter-impl-etesync.c
 *
 * SPDX-FileCopyrightText: (C) 2020 Nour E-Din El-Nhass <nouredinosama.gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-etesync-config.h"

#include <glib/gi18n-lib.h>

#include "e-credentials-prompter-impl-etesync.h"
#include "common/e-etesync-defines.h"
#include "common/e-etesync-connection.h"

typedef enum {
	TYPE_NO_ERROR,
	TYPE_NO_PASSWORD,
	TYPE_NO_ENCRYPTION_PASSWORD,
	TYPE_WRONG_PASSWORD,
	TYPE_WRONG_ENCRYPTION_PASSWORD,
	TYPE_NEW_ACCOUNT
} DialogErrorType;

struct _ECredentialsPrompterImplEteSyncPrivate {
	GMutex property_lock;

	gpointer prompt_id;
	ESource *auth_source;
	ESource *cred_source;
	gchar *error_text;
	ENamedParameters *credentials;

	GtkDialog *dialog;
	gulong show_dialog_idle_id;

	DialogErrorType dialog_error;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	ECredentialsPrompterImplEteSync,
	e_credentials_prompter_impl_etesync,
	E_TYPE_CREDENTIALS_PROMPTER_IMPL,
	0,
	G_ADD_PRIVATE_DYNAMIC (ECredentialsPrompterImplEteSync))

typedef struct {
	GWeakRef *prompter_etesync; /* ECredentialsPrompterImplEteSync * */
	EEteSyncConnection *connection;
	gchar *username;
	gchar *password;
	gchar *encryption_password;
	gchar *server_url;
	gboolean out_success;
} AuthenticationData;

static void
e_credentials_prompter_impl_etesync_free_prompt_data (ECredentialsPrompterImplEteSync *prompter_etesync)
{
	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_ETESYNC (prompter_etesync));

	prompter_etesync->priv->prompt_id = NULL;

	g_clear_object (&prompter_etesync->priv->auth_source);
	g_clear_object (&prompter_etesync->priv->cred_source);
	g_clear_pointer (&prompter_etesync->priv->error_text, g_free);
	g_clear_pointer (&prompter_etesync->priv->credentials, e_named_parameters_free);
}

static void
cpi_etesync_token_thread_data_free (gpointer user_data)
{
	AuthenticationData *data = user_data;

	if (data) {
		g_clear_object (&data->connection);
		e_weak_ref_free (data->prompter_etesync);
		g_free (data->username);
		g_free (data->password);
		g_free (data->encryption_password);
		g_free (data->server_url);
		g_slice_free (AuthenticationData, data);
	}
}

static void
cpi_etesync_get_token_set_credentials_thread (GTask *task,
					      gpointer source_object,
					      gpointer task_data,
					      GCancellable *cancellable)
{
	AuthenticationData *data = task_data;
	ECredentialsPrompterImplEteSync *prompter_etesync;

	g_return_if_fail (data != NULL);

	prompter_etesync = g_weak_ref_get (data->prompter_etesync);

	data->out_success = data->username && data->password &&
			    e_etesync_connection_set_connection (data->connection,
								 data->username,
								 data->password,
								 data->encryption_password,
								 data->server_url,
								 NULL);

	if (prompter_etesync->priv->dialog_error == TYPE_NEW_ACCOUNT) {
		data->out_success = e_etesync_connection_get_derived_key (data->connection) ?
				    e_etesync_connection_init_userinfo (data->connection) : FALSE;
	}

	if (prompter_etesync) {

		e_named_parameters_clear (prompter_etesync->priv->credentials);
		e_named_parameters_set (prompter_etesync->priv->credentials,
			E_SOURCE_CREDENTIAL_PASSWORD, data->password);
		e_named_parameters_set (prompter_etesync->priv->credentials,
			E_ETESYNC_CREDENTIAL_ENCRYPTION_PASSWORD, data->encryption_password);

		g_clear_pointer (&prompter_etesync->priv->error_text, g_free);
		if (!data->out_success) {
			prompter_etesync->priv->error_text = g_strdup_printf (
					_("Failed to obtain access token for account “%s”"),
					data->username);
		} else {
			e_named_parameters_set (prompter_etesync->priv->credentials,
				E_ETESYNC_CREDENTIAL_TOKEN, e_etesync_connection_get_token (data->connection));
			e_named_parameters_set (prompter_etesync->priv->credentials,
				E_ETESYNC_CREDENTIAL_DERIVED_KEY, e_etesync_connection_get_derived_key (data->connection));
		}
	}
	g_clear_object (&prompter_etesync);
}

static void
cpi_etesync_get_token_set_credentials_cb (GObject *source,
					  GAsyncResult *result,
					  gpointer user_data)
{
	ECredentialsPrompterImplEteSync *prompter_etesync = g_object_ref (user_data);

	e_credentials_prompter_impl_prompt_finish (
		E_CREDENTIALS_PROMPTER_IMPL (prompter_etesync),
		prompter_etesync->priv->prompt_id,
		prompter_etesync->priv->credentials);

	e_credentials_prompter_impl_etesync_free_prompt_data (prompter_etesync);
}

static gboolean
etesync_dialog_map_event_cb (GtkWidget *dialog,
			     GdkEvent *event,
			     GtkWidget *entry)
{
	gtk_widget_grab_focus (entry);

	return FALSE;
}

static void
credentials_prompter_impl_etesync_get_prompt_strings (ESourceRegistry *registry,
						      ESource *source,
						      gchar **prompt_title,
						      GString **prompt_description,
						      DialogErrorType dialog_error)
{
	GString *description;
	const gchar *message;
	gchar *display_name;
	gchar *host_name = NULL, *tmp;

	display_name = e_util_get_source_full_name (registry, source);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *extension;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		host_name = e_source_authentication_dup_host (extension);
	}

	description = g_string_sized_new (256);

	message = _("EteSync account authentication request");

	switch (dialog_error) {
		case TYPE_NO_PASSWORD:
			g_string_append_printf (description,
				_("Please enter the password for account “%s”."), display_name);
			break;
		case TYPE_NO_ENCRYPTION_PASSWORD:
			g_string_append_printf (description,
				_("Please enter the encryption password for account “%s”."), display_name);
			break;
		case TYPE_WRONG_PASSWORD:
			g_string_append_printf (description,
				_("Wrong password!\n\nPlease enter the correct password for account “%s”."), display_name);
			break;
		case TYPE_WRONG_ENCRYPTION_PASSWORD:
			g_string_append_printf (description,
				_("Wrong encryption password!\n\nPlease enter the correct encryption password for account “%s”."), display_name);
			break;
		case TYPE_NEW_ACCOUNT:
			g_string_append_printf (description,
				_("Welcome to EteSync!\n\n"
				"Please set an encryption password below for account “%s”, and make sure you got it right, "
				"as it can't be recovered if lost!"), display_name);
				break;
		default:  /* generic account prompt */
			g_string_append_printf (description,
				_("Please enter the correct credentials for account “%s”."), display_name);
			break;
	}

	if (host_name != NULL) {
		/* Translators: This is part of a credential prompt, constructing for example: "Please enter the correct credentials for account “%s”.\n(host: hostname)" */
		g_string_append_printf (description, _("\n(host: %s)"), host_name);
	}

	tmp = g_markup_escape_text (description->str, -1);

	g_string_assign (description, "");
	g_string_append_printf (description, "<big><b>%s</b></big>\n\n%s", message, tmp);

	*prompt_title = g_strdup (message);
	*prompt_description = description;

	g_free (tmp);
	g_free (display_name);
	g_free (host_name);
}

static gboolean
e_credentials_prompter_impl_etesync_show_new_account_dialog (ECredentialsPrompterImplEteSync *prompter_etesync)
{
	GtkWidget *dialog, *content_area, *widget;
	GtkGrid *grid;
	GtkEntry *encryption_password_entry1;
	GtkEntry *encryption_password_entry2;
	GtkToggleButton *remember_toggle = NULL;
	GtkWindow *dialog_parent;
	ECredentialsPrompter *prompter;
	gchar *title;
	GString *info_markup;
	gint row = 0;
	ESourceAuthentication *auth_extension = NULL;
	gboolean success, is_scratch_source = TRUE;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_ETESYNC (prompter_etesync), FALSE);
	g_return_val_if_fail (prompter_etesync->priv->prompt_id != NULL, FALSE);
	g_return_val_if_fail (prompter_etesync->priv->dialog == NULL, FALSE);

	prompter = e_credentials_prompter_impl_get_credentials_prompter (E_CREDENTIALS_PROMPTER_IMPL (prompter_etesync));
	g_return_val_if_fail (prompter != NULL, FALSE);

	dialog_parent = e_credentials_prompter_get_dialog_parent (prompter);

	credentials_prompter_impl_etesync_get_prompt_strings (
		e_credentials_prompter_get_registry (prompter),
		prompter_etesync->priv->auth_source, &title, &info_markup, prompter_etesync->priv->dialog_error);
	if (prompter_etesync->priv->error_text && *prompter_etesync->priv->error_text) {
		gchar *escaped = g_markup_printf_escaped ("%s", prompter_etesync->priv->error_text);

		g_string_append_printf (info_markup, "\n\n%s", escaped);
		g_free (escaped);
	}

	dialog = gtk_dialog_new_with_buttons (title, dialog_parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_OK"), GTK_RESPONSE_OK,
		NULL);

	prompter_etesync->priv->dialog = GTK_DIALOG (dialog);
	gtk_dialog_set_default_response (prompter_etesync->priv->dialog, GTK_RESPONSE_OK);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	if (dialog_parent)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), dialog_parent);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);

	content_area = gtk_dialog_get_content_area (prompter_etesync->priv->dialog);

	/* Override GtkDialog defaults */
	gtk_box_set_spacing (GTK_BOX (content_area), 12);
	gtk_container_set_border_width (GTK_CONTAINER (content_area), 0);

	grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_column_spacing (grid, 12);
	gtk_grid_set_row_spacing (grid, 6);

	gtk_box_pack_start (GTK_BOX (content_area), GTK_WIDGET (grid), FALSE, TRUE, 0);

	/* Password Image */
	widget = gtk_image_new_from_icon_name ("dialog-password", GTK_ICON_SIZE_DIALOG);
	g_object_set (
		G_OBJECT (widget),
		"halign", GTK_ALIGN_START,
		"vexpand", TRUE,
		"valign", GTK_ALIGN_START,
		NULL);

	gtk_grid_attach (grid, widget, 0, row, 1, 1);

	/* Encryotion password Label */
	widget = gtk_label_new (NULL);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_label_set_markup (GTK_LABEL (widget), info_markup->str);
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_CENTER,
		"width-chars", 60,
		"max-width-chars", 80,
		"xalign", 0.0,
		NULL);

	gtk_grid_attach (grid, widget, 1, row, 1, 1);
	row++;

	if (e_source_has_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		GDBusObject *dbus_object;

		dbus_object = e_source_ref_dbus_object (prompter_etesync->priv->cred_source);
		is_scratch_source = !dbus_object;
		g_clear_object (&dbus_object);

		auth_extension = e_source_get_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_AUTHENTICATION);
	}

	/* Setting encryption password entry 1 */
	encryption_password_entry1 = GTK_ENTRY (gtk_entry_new ());
	gtk_entry_set_visibility (encryption_password_entry1, FALSE);
	gtk_entry_set_activates_default (encryption_password_entry1, TRUE);
	g_object_set (
		G_OBJECT (encryption_password_entry1),
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		NULL);

	gtk_grid_attach (grid, GTK_WIDGET (encryption_password_entry1), 1, row, 1, 1);
	row++;

	/* Setting encryption password entry 2 */
	encryption_password_entry2 = GTK_ENTRY (gtk_entry_new ());
	gtk_entry_set_visibility (encryption_password_entry2, FALSE);
	gtk_entry_set_activates_default (encryption_password_entry2, TRUE);
	g_object_set (
		G_OBJECT (encryption_password_entry2),
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		NULL);

	gtk_grid_attach (grid, GTK_WIDGET (encryption_password_entry2), 1, row, 1, 1);
	row++;


	if (encryption_password_entry1 && encryption_password_entry2) {
		widget = gtk_label_new_with_mnemonic (_("_Encryption Password:"));
		g_object_set (
			G_OBJECT (widget),
			"hexpand", FALSE,
			"vexpand", FALSE,
			"halign", GTK_ALIGN_END,
			"valign", GTK_ALIGN_CENTER,
			NULL);

		gtk_label_set_mnemonic_widget (GTK_LABEL (widget), GTK_WIDGET (encryption_password_entry1));
		gtk_grid_attach (grid, widget, 0, row - 2, 1, 1);

		widget = gtk_label_new_with_mnemonic (_("Encryption _Password again:"));
		g_object_set (
			G_OBJECT (widget),
			"hexpand", FALSE,
			"vexpand", FALSE,
			"halign", GTK_ALIGN_END,
			"valign", GTK_ALIGN_CENTER,
			NULL);

		gtk_label_set_mnemonic_widget (GTK_LABEL (widget), GTK_WIDGET (encryption_password_entry2));
		gtk_grid_attach (grid, widget, 0, row - 1, 1, 1);
	}

	if (auth_extension && !is_scratch_source) {
		/* Remember password check */
		widget = gtk_check_button_new_with_mnemonic (_("_Add this password to your keyring"));
		remember_toggle = GTK_TOGGLE_BUTTON (widget);
		gtk_toggle_button_set_active (remember_toggle, e_source_authentication_get_remember_password (auth_extension));
		g_object_set (
			G_OBJECT (widget),
			"hexpand", TRUE,
			"halign", GTK_ALIGN_FILL,
			"valign", GTK_ALIGN_FILL,
			"margin-top", 12,
			NULL);

		gtk_grid_attach (grid, widget, 1, row, 1, 1);
	}

	g_signal_connect (dialog, "map-event", G_CALLBACK (etesync_dialog_map_event_cb), encryption_password_entry1);

	gtk_widget_show_all (GTK_WIDGET (grid));
	success = gtk_dialog_run (prompter_etesync->priv->dialog) == GTK_RESPONSE_OK;

	g_mutex_lock (&prompter_etesync->priv->property_lock);
	if (success) {
		/* In this part we should have username, password, encryption_password
		   We need to get token and derived key and store them */
		AuthenticationData *data;
		ESourceCollection *collection_extension;
		GTask *task;

		data = g_slice_new0 (AuthenticationData);
		data->connection = e_etesync_connection_new ();
		data->prompter_etesync = e_weak_ref_new (prompter_etesync);

		/* Need to set password and encryption as they are always stored to retrive proper error message when dialog opens */
		data->password = g_strdup (e_named_parameters_get (prompter_etesync->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD));
		data->encryption_password = g_strdup ("");

		if (!g_strcmp0 (gtk_entry_get_text (encryption_password_entry1),
			        gtk_entry_get_text (encryption_password_entry2))) {
			collection_extension = e_source_get_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_COLLECTION);

			g_free (data->encryption_password);
			data->encryption_password = g_strdup (gtk_entry_get_text (encryption_password_entry1));

			data->username = e_source_authentication_dup_user (auth_extension);
			data->server_url = e_source_collection_dup_contacts_url (collection_extension);

			if (auth_extension && remember_toggle) {
				e_source_authentication_set_remember_password (auth_extension,
					gtk_toggle_button_get_active (remember_toggle));
			}
		}

		/* We must in either way invoke this thread, as in it's callback it calls finish,
		   if encryption_password_entries didn't match, just send a dummy data (empty connection)
		   which will fail and open another dialog */
		task = g_task_new (NULL, NULL, cpi_etesync_get_token_set_credentials_cb, prompter_etesync);
		g_task_set_task_data (task, data, cpi_etesync_token_thread_data_free);
		g_task_run_in_thread (task, cpi_etesync_get_token_set_credentials_thread);
		g_object_unref (task);
	}
	prompter_etesync->priv->dialog = NULL;
	g_mutex_unlock (&prompter_etesync->priv->property_lock);

	gtk_widget_destroy (dialog);

	g_string_free (info_markup, TRUE);
	g_free (title);

	return success;
}

static gboolean
e_credentials_prompter_impl_etesync_show_credentials_dialog (ECredentialsPrompterImplEteSync *prompter_etesync)
{
	GtkWidget *dialog, *content_area, *widget;
	GtkGrid *grid;
	GtkEntry *username_entry = NULL;
	GtkEntry *password_entry;
	GtkEntry *encryption_password_entry;
	GtkToggleButton *remember_toggle = NULL;
	GtkWindow *dialog_parent;
	ECredentialsPrompter *prompter;
	gchar *title;
	GString *info_markup;
	gint row = 0;
	ESourceAuthentication *auth_extension = NULL;
	gboolean success, is_scratch_source = TRUE;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_ETESYNC (prompter_etesync), FALSE);
	g_return_val_if_fail (prompter_etesync->priv->prompt_id != NULL, FALSE);
	g_return_val_if_fail (prompter_etesync->priv->dialog == NULL, FALSE);

	prompter = e_credentials_prompter_impl_get_credentials_prompter (E_CREDENTIALS_PROMPTER_IMPL (prompter_etesync));
	g_return_val_if_fail (prompter != NULL, FALSE);

	dialog_parent = e_credentials_prompter_get_dialog_parent (prompter);

	credentials_prompter_impl_etesync_get_prompt_strings (
		e_credentials_prompter_get_registry (prompter),
		prompter_etesync->priv->auth_source, &title, &info_markup, prompter_etesync->priv->dialog_error);
	if (prompter_etesync->priv->error_text && *prompter_etesync->priv->error_text) {
		gchar *escaped = g_markup_printf_escaped ("%s", prompter_etesync->priv->error_text);

		g_string_append_printf (info_markup, "\n\n%s", escaped);
		g_free (escaped);
	}

	dialog = gtk_dialog_new_with_buttons (title, dialog_parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_OK"), GTK_RESPONSE_OK,
		NULL);

	prompter_etesync->priv->dialog = GTK_DIALOG (dialog);
	gtk_dialog_set_default_response (prompter_etesync->priv->dialog, GTK_RESPONSE_OK);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	if (dialog_parent)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), dialog_parent);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);

	content_area = gtk_dialog_get_content_area (prompter_etesync->priv->dialog);

	/* Override GtkDialog defaults */
	gtk_box_set_spacing (GTK_BOX (content_area), 12);
	gtk_container_set_border_width (GTK_CONTAINER (content_area), 0);

	grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_column_spacing (grid, 12);
	gtk_grid_set_row_spacing (grid, 7);

	gtk_box_pack_start (GTK_BOX (content_area), GTK_WIDGET (grid), FALSE, TRUE, 0);

	/* Password Image */
	widget = gtk_image_new_from_icon_name ("dialog-password", GTK_ICON_SIZE_DIALOG);
	g_object_set (
		G_OBJECT (widget),
		"halign", GTK_ALIGN_START,
		"vexpand", TRUE,
		"valign", GTK_ALIGN_START,
		NULL);

	gtk_grid_attach (grid, widget, 0, row, 1, 1);

	/* Password Label */
	widget = gtk_label_new (NULL);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_label_set_markup (GTK_LABEL (widget), info_markup->str);
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_CENTER,
		"width-chars", 60,
		"max-width-chars", 80,
		"xalign", 0.0,
		NULL);

	gtk_grid_attach (grid, widget, 1, row, 1, 1);
	row++;

	if (e_source_has_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		GDBusObject *dbus_object;

		dbus_object = e_source_ref_dbus_object (prompter_etesync->priv->cred_source);
		is_scratch_source = !dbus_object;
		g_clear_object (&dbus_object);

		auth_extension = e_source_get_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_AUTHENTICATION);

		if (is_scratch_source || e_source_get_writable (prompter_etesync->priv->cred_source)) {
			gchar *username;

			username = e_source_authentication_dup_user (auth_extension);
			if ((!username || !*username) &&
			    e_source_has_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_COLLECTION)) {
				ESourceCollection *collection_extension;
				gchar *tmp;

				collection_extension = e_source_get_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_COLLECTION);

				tmp = e_source_collection_dup_identity (collection_extension);
				if (tmp && *tmp) {
					g_free (username);
					username = tmp;
					tmp = NULL;
				}

				g_free (tmp);
			}

			username_entry = GTK_ENTRY (gtk_entry_new ());
			g_object_set (
				G_OBJECT (username_entry),
				"hexpand", TRUE,
				"halign", GTK_ALIGN_FILL,
				NULL);

			gtk_grid_attach (grid, GTK_WIDGET (username_entry), 1, row, 1, 1);
			row++;

			if (username && *username)
				gtk_entry_set_text (username_entry, username);

			g_free (username);
		}
	}

	/* Setting password entry */
	password_entry = GTK_ENTRY (gtk_entry_new ());
	gtk_entry_set_visibility (password_entry, FALSE);
	gtk_entry_set_activates_default (password_entry, TRUE);
	g_object_set (
		G_OBJECT (password_entry),
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		NULL);
	if (e_named_parameters_get (prompter_etesync->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD))
		gtk_entry_set_text (password_entry, e_named_parameters_get (prompter_etesync->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD));

	gtk_grid_attach (grid, GTK_WIDGET (password_entry), 1, row, 1, 1);
	row++;

	/* Setting encryption_password entry */
	encryption_password_entry = GTK_ENTRY (gtk_entry_new ());
	gtk_entry_set_visibility (encryption_password_entry, FALSE);
	gtk_entry_set_activates_default (encryption_password_entry, TRUE);
	g_object_set (
		G_OBJECT (encryption_password_entry),
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		NULL);
	if (e_named_parameters_get (prompter_etesync->priv->credentials, E_ETESYNC_CREDENTIAL_ENCRYPTION_PASSWORD))
		gtk_entry_set_text (encryption_password_entry, e_named_parameters_get (prompter_etesync->priv->credentials, E_ETESYNC_CREDENTIAL_ENCRYPTION_PASSWORD));

	gtk_grid_attach (grid, GTK_WIDGET (encryption_password_entry), 1, row, 1, 1);
	row++;


	if (username_entry && password_entry && encryption_password_entry) {
		widget = gtk_label_new_with_mnemonic (_("_User Name:"));
		g_object_set (
			G_OBJECT (widget),
			"hexpand", FALSE,
			"vexpand", FALSE,
			"halign", GTK_ALIGN_END,
			"valign", GTK_ALIGN_CENTER,
			NULL);

		gtk_label_set_mnemonic_widget (GTK_LABEL (widget), GTK_WIDGET (username_entry));
		gtk_grid_attach (grid, widget, 0, row - 3, 1, 1);

		widget = gtk_label_new_with_mnemonic (_("_Password:"));
		g_object_set (
			G_OBJECT (widget),
			"hexpand", FALSE,
			"vexpand", FALSE,
			"halign", GTK_ALIGN_END,
			"valign", GTK_ALIGN_CENTER,
			NULL);

		gtk_label_set_mnemonic_widget (GTK_LABEL (widget), GTK_WIDGET (password_entry));
		gtk_grid_attach (grid, widget, 0, row - 2, 1, 1);

		widget = gtk_label_new_with_mnemonic (_("_Encryption Password:"));
		g_object_set (
			G_OBJECT (widget),
			"hexpand", FALSE,
			"vexpand", FALSE,
			"halign", GTK_ALIGN_END,
			"valign", GTK_ALIGN_CENTER,
			NULL);

		gtk_label_set_mnemonic_widget (GTK_LABEL (widget), GTK_WIDGET (encryption_password_entry));
		gtk_grid_attach (grid, widget, 0, row - 1, 1, 1);
	}

	if (auth_extension && !is_scratch_source) {
		/* Remember password check */
		widget = gtk_check_button_new_with_mnemonic (_("_Add this password to your keyring"));
		remember_toggle = GTK_TOGGLE_BUTTON (widget);
		gtk_toggle_button_set_active (remember_toggle, e_source_authentication_get_remember_password (auth_extension));
		g_object_set (
			G_OBJECT (widget),
			"hexpand", TRUE,
			"halign", GTK_ALIGN_FILL,
			"valign", GTK_ALIGN_FILL,
			"margin-top", 12,
			NULL);

		gtk_grid_attach (grid, widget, 1, row, 1, 1);
	}

	if (username_entry && g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (username_entry)), "") == 0)
		g_signal_connect (dialog, "map-event", G_CALLBACK (etesync_dialog_map_event_cb), username_entry);
	else if (prompter_etesync->priv->dialog_error == TYPE_NO_PASSWORD || prompter_etesync->priv->dialog_error == TYPE_WRONG_PASSWORD)
		g_signal_connect (dialog, "map-event", G_CALLBACK (etesync_dialog_map_event_cb), password_entry);
	else if (prompter_etesync->priv->dialog_error == TYPE_NO_ENCRYPTION_PASSWORD || prompter_etesync->priv->dialog_error == TYPE_WRONG_ENCRYPTION_PASSWORD)
		g_signal_connect (dialog, "map-event", G_CALLBACK (etesync_dialog_map_event_cb), encryption_password_entry);

	gtk_widget_show_all (GTK_WIDGET (grid));
	success = gtk_dialog_run (prompter_etesync->priv->dialog) == GTK_RESPONSE_OK;

	g_mutex_lock (&prompter_etesync->priv->property_lock);
	if (success) {
		/* In this part we should have username, password, encryption_password
		   We need to get token and derived key and store them */
		AuthenticationData *data;
		ESourceCollection *collection_extension;
		GTask *task;

		collection_extension = e_source_get_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_COLLECTION);

		data = g_slice_new0 (AuthenticationData);
		data->prompter_etesync = e_weak_ref_new (prompter_etesync);
		data->connection = e_etesync_connection_new ();
		data->username = g_strdup (gtk_entry_get_text (username_entry));
		data->password = g_strdup (gtk_entry_get_text (password_entry));
		data->encryption_password = g_strdup (gtk_entry_get_text (encryption_password_entry));
		data->server_url = g_strdup (e_source_collection_get_contacts_url (collection_extension));

		task = g_task_new (NULL, NULL, cpi_etesync_get_token_set_credentials_cb, prompter_etesync);
		g_task_set_task_data (task, data, cpi_etesync_token_thread_data_free);
		g_task_run_in_thread (task, cpi_etesync_get_token_set_credentials_thread);
		g_object_unref (task);

		if (auth_extension && remember_toggle) {
			e_source_authentication_set_remember_password (auth_extension,
				gtk_toggle_button_get_active (remember_toggle));
		}
	}
	prompter_etesync->priv->dialog = NULL;
	g_mutex_unlock (&prompter_etesync->priv->property_lock);

	gtk_widget_destroy (dialog);

	g_string_free (info_markup, TRUE);
	g_free (title);
	return success;
}

static gboolean
e_credentials_prompter_impl_etesync_show_dialog_idle_cb (gpointer user_data)
{
	ECredentialsPrompterImplEteSync *prompter_etesync = user_data;

	if (g_source_is_destroyed (g_main_current_source ()))
		return FALSE;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_ETESYNC (prompter_etesync), FALSE);

	g_mutex_lock (&prompter_etesync->priv->property_lock);
	if (g_source_get_id (g_main_current_source ()) == prompter_etesync->priv->show_dialog_idle_id) {
		gboolean success;

		prompter_etesync->priv->show_dialog_idle_id = 0;

		g_mutex_unlock (&prompter_etesync->priv->property_lock);

		g_warn_if_fail (prompter_etesync->priv->dialog == NULL);

		if (prompter_etesync->priv->dialog_error == TYPE_NEW_ACCOUNT)
			success = e_credentials_prompter_impl_etesync_show_new_account_dialog (prompter_etesync);
		else
			success = e_credentials_prompter_impl_etesync_show_credentials_dialog (prompter_etesync);

		if (!success) {
			e_credentials_prompter_impl_prompt_finish (
				E_CREDENTIALS_PROMPTER_IMPL (prompter_etesync),
				prompter_etesync->priv->prompt_id,
				NULL);

			e_credentials_prompter_impl_etesync_free_prompt_data (prompter_etesync);
		}
	} else {
		gpointer prompt_id = prompter_etesync->priv->prompt_id;

		if (!prompter_etesync->priv->show_dialog_idle_id)
			e_credentials_prompter_impl_etesync_free_prompt_data (prompter_etesync);

		g_mutex_unlock (&prompter_etesync->priv->property_lock);

		if (prompt_id)
			e_credentials_prompter_impl_prompt_finish (E_CREDENTIALS_PROMPTER_IMPL (prompter_etesync), prompt_id, NULL);
	}


	return FALSE;
}

static void
cpi_etesync_set_dialog_error_thread (GTask *task,
				     gpointer source_object,
				     gpointer task_data,
				     GCancellable *cancellable)
{
	ECredentialsPrompterImplEteSync *prompter_etesync = task_data;
	ESourceCollection *collection_extension;
	ESourceAuthentication *auth_extension;
	EEteSyncConnection *connection = NULL;
	const gchar *username, *password = NULL, *encryption_password = NULL, *server_url;
	gboolean success = FALSE;
	EteSyncErrorCode etesync_error = ETESYNC_ERROR_CODE_NO_ERROR;

	collection_extension = e_source_get_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_COLLECTION);
	auth_extension = e_source_get_extension (prompter_etesync->priv->cred_source, E_SOURCE_EXTENSION_AUTHENTICATION);

	connection = e_etesync_connection_new ();
	username = e_source_authentication_get_user (auth_extension);
	server_url = e_source_collection_get_contacts_url (collection_extension);
	password = e_named_parameters_get (prompter_etesync->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD);
	encryption_password = e_named_parameters_get (prompter_etesync->priv->credentials, E_ETESYNC_CREDENTIAL_ENCRYPTION_PASSWORD);

	if (!password || !*password) {
		prompter_etesync->priv->dialog_error = TYPE_NO_PASSWORD;
	} else {
		success = e_etesync_connection_set_connection (connection, username, password, encryption_password, server_url, &etesync_error);

		switch (etesync_error) {
			case ETESYNC_ERROR_CODE_HTTP:
			case ETESYNC_ERROR_CODE_UNAUTHORIZED:
				prompter_etesync->priv->dialog_error = TYPE_WRONG_PASSWORD;
				break;
			case ETESYNC_ERROR_CODE_ENCRYPTION_MAC:
				prompter_etesync->priv->dialog_error = TYPE_WRONG_ENCRYPTION_PASSWORD;
				break;
			case ETESYNC_ERROR_CODE_NOT_FOUND:
				prompter_etesync->priv->dialog_error = TYPE_NEW_ACCOUNT;
				break;
			default:
				prompter_etesync->priv->dialog_error = !encryption_password || !*encryption_password ? TYPE_NO_ENCRYPTION_PASSWORD : TYPE_NO_ERROR;
				break;
		}
	}

	if (success)
		g_task_return_pointer (task, g_object_ref (connection), (GDestroyNotify) g_object_unref);
	else {
		/* Since there was a problem error, and we got a token then invalidate that token
		   Because will get a new one after the user enters the data, as this one will be lost. */
		if (e_etesync_connection_get_token (connection))
			etesync_auth_invalidate_token (e_etesync_connection_get_etesync (connection), e_etesync_connection_get_token (connection));
		g_task_return_pointer (task, NULL, NULL);
	}

	g_object_unref (connection);
}

static void
cpi_etesync_set_dialog_error_cb (GObject *source,
				 GAsyncResult *result,
				 gpointer user_data)
{
	ECredentialsPrompterImplEteSync *prompter_etesync;
	EEteSyncConnection *connection = NULL;

	prompter_etesync = g_object_ref (user_data);
	connection = g_task_propagate_pointer (G_TASK (result), NULL);

	g_mutex_lock (&prompter_etesync->priv->property_lock);

	/* if there is an error then pop the dialog to the user,
	   if not then the credentials are ok, but need to add new token only */
	if (prompter_etesync->priv->dialog_error == TYPE_NO_ERROR && connection){
		e_named_parameters_set (prompter_etesync->priv->credentials,
				E_ETESYNC_CREDENTIAL_TOKEN, e_etesync_connection_get_token (connection));
		e_named_parameters_set (prompter_etesync->priv->credentials,
				E_ETESYNC_CREDENTIAL_DERIVED_KEY, e_etesync_connection_get_derived_key (connection));

		cpi_etesync_get_token_set_credentials_cb (NULL, NULL, prompter_etesync);
	} else {
		prompter_etesync->priv->show_dialog_idle_id = g_idle_add (
			e_credentials_prompter_impl_etesync_show_dialog_idle_cb,
			prompter_etesync);
	}

	g_object_unref (prompter_etesync);
	if (connection)
		g_object_unref (connection);

	g_mutex_unlock (&prompter_etesync->priv->property_lock);
}

static void
e_credentials_prompter_impl_etesync_process_prompt (ECredentialsPrompterImpl *prompter_impl,
						    gpointer prompt_id,
						    ESource *auth_source,
						    ESource *cred_source,
						    const gchar *error_text,
						    const ENamedParameters *credentials)
{
	ECredentialsPrompterImplEteSync *prompter_etesync;
	GTask *task;

	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_ETESYNC (prompter_impl));

	prompter_etesync = E_CREDENTIALS_PROMPTER_IMPL_ETESYNC (prompter_impl);
	g_return_if_fail (prompter_etesync->priv->prompt_id == NULL);
	g_return_if_fail (prompter_etesync->priv->show_dialog_idle_id == 0);

	prompter_etesync->priv->prompt_id = prompt_id;
	prompter_etesync->priv->auth_source = g_object_ref (auth_source);
	prompter_etesync->priv->cred_source = g_object_ref (cred_source);
	prompter_etesync->priv->error_text = g_strdup (error_text);
	prompter_etesync->priv->credentials = e_named_parameters_new_clone (credentials);

	task = g_task_new (NULL, NULL, cpi_etesync_set_dialog_error_cb, prompter_etesync);
	g_task_set_task_data (task, g_object_ref (prompter_etesync), g_object_unref);
	g_task_run_in_thread (task, cpi_etesync_set_dialog_error_thread);
	g_object_unref (task);
}

static void
e_credentials_prompter_impl_etesync_cancel_prompt (ECredentialsPrompterImpl *prompter_impl,
						   gpointer prompt_id)
{
	ECredentialsPrompterImplEteSync *prompter_etesync;

	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_ETESYNC (prompter_impl));

	prompter_etesync = E_CREDENTIALS_PROMPTER_IMPL_ETESYNC (prompter_impl);
	g_return_if_fail (prompter_etesync->priv->prompt_id == prompt_id);

	/* This also closes the dialog. */
	if (prompter_etesync->priv->dialog)
		gtk_dialog_response (prompter_etesync->priv->dialog, GTK_RESPONSE_CANCEL);
}

static void
e_credentials_prompter_impl_etesync_constructed (GObject *object)
{
	ECredentialsPrompterImplEteSync *prompter_etesync = E_CREDENTIALS_PROMPTER_IMPL_ETESYNC (object);
	ECredentialsPrompterImpl *prompter_impl = E_CREDENTIALS_PROMPTER_IMPL (prompter_etesync);
	ECredentialsPrompter *prompter = E_CREDENTIALS_PROMPTER (e_extension_get_extensible (E_EXTENSION (prompter_impl)));

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_credentials_prompter_impl_etesync_parent_class)->constructed (object);

	e_credentials_prompter_register_impl (prompter, "EteSync", prompter_impl);
}

static void
e_credentials_prompter_impl_etesync_dispose (GObject *object)
{
	ECredentialsPrompterImplEteSync *prompter_etesync = E_CREDENTIALS_PROMPTER_IMPL_ETESYNC (object);

	g_mutex_lock (&prompter_etesync->priv->property_lock);

	if (prompter_etesync->priv->show_dialog_idle_id) {
		g_source_remove (prompter_etesync->priv->show_dialog_idle_id);
		prompter_etesync->priv->show_dialog_idle_id = 0;
	}

	g_mutex_unlock (&prompter_etesync->priv->property_lock);

	g_warn_if_fail (prompter_etesync->priv->prompt_id == NULL);
	g_warn_if_fail (prompter_etesync->priv->dialog == NULL);

	e_credentials_prompter_impl_etesync_free_prompt_data (prompter_etesync);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_credentials_prompter_impl_etesync_parent_class)->dispose (object);
}

static void
e_credentials_prompter_impl_etesync_finalize (GObject *object)
{
	ECredentialsPrompterImplEteSync *prompter_etesync = E_CREDENTIALS_PROMPTER_IMPL_ETESYNC (object);

	g_mutex_clear (&prompter_etesync->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_credentials_prompter_impl_etesync_parent_class)->finalize (object);
}

static void
e_credentials_prompter_impl_etesync_class_init (ECredentialsPrompterImplEteSyncClass *class)
{
	static const gchar *authentication_methods[] = {
		"EteSync",
		NULL
	};

	GObjectClass *object_class;
	ECredentialsPrompterImplClass *prompter_impl_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = e_credentials_prompter_impl_etesync_constructed;
	object_class->dispose = e_credentials_prompter_impl_etesync_dispose;
	object_class->finalize = e_credentials_prompter_impl_etesync_finalize;

	prompter_impl_class = E_CREDENTIALS_PROMPTER_IMPL_CLASS (class);
	prompter_impl_class->authentication_methods = (const gchar * const *) authentication_methods;
	prompter_impl_class->process_prompt = e_credentials_prompter_impl_etesync_process_prompt;
	prompter_impl_class->cancel_prompt = e_credentials_prompter_impl_etesync_cancel_prompt;
}

static void
e_credentials_prompter_impl_etesync_class_finalize (ECredentialsPrompterImplEteSyncClass *klass)
{
}

static void
e_credentials_prompter_impl_etesync_init (ECredentialsPrompterImplEteSync *prompter_etesync)
{
	prompter_etesync->priv = e_credentials_prompter_impl_etesync_get_instance_private (prompter_etesync);

	g_mutex_init (&prompter_etesync->priv->property_lock);
}

ECredentialsPrompterImpl *
e_credentials_prompter_impl_etesync_new (void)
{
	return g_object_new (E_TYPE_CREDENTIALS_PROMPTER_IMPL_ETESYNC, NULL);
}

void
e_credentials_prompter_impl_etesync_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_credentials_prompter_impl_etesync_register_type (type_module);
}
