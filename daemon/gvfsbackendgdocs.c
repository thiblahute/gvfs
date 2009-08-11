/* GIO - GLib Input, Output and Streaming Library
 *
 * Copyright (C) Thibault Saunier 2009 <saunierthibault@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Thibault Saunier <saunierthibault@gmail.com>
 */

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gvfsbackendgdocs.h"
#include "gvfsgdatafile.h"

#include "gvfsjobopenforread.h"
#include "gvfsjobread.h"
#include "gvfsjobseekread.h"
#include "gvfsjobopenforwrite.h"
#include "gvfsjobwrite.h"
#include "gvfsjobseekwrite.h"
#include "gvfsjobsetdisplayname.h"
#include "gvfsjobqueryinfo.h"
#include "gvfsjobqueryfsinfo.h"
#include "gvfsjobqueryattributes.h"
#include "gvfsjobenumerate.h"
#include "gvfsdaemonprotocol.h"
#include "gvfskeyring.h"

#include "soup-input-stream.h"

G_DEFINE_TYPE (GVfsBackendGdocs, g_vfs_backend_gdocs, G_VFS_TYPE_BACKEND)


static void
g_vfs_backend_gdocs_finalize (GObject *object)
{
	/*TODO Check if I use it*/
	GVfsBackendGdocs *backend;

	backend = G_VFS_BACKEND_GDOCS (object);
	g_hash_table_destroy (backend->entries_type);
}

static void
g_vfs_backend_gdocs_dispose (GObject *object)
{
	GVfsBackendGdocs *backend;

	backend = G_VFS_BACKEND_GDOCS (object);
	if (backend->service != NULL )
		g_object_unref (backend->service);
}

#define DEBUG_MAX_BODY_SIZE (100 * 1024 * 1024)

static void
g_vfs_backend_gdocs_init (GVfsBackendGdocs *backend)
{
	backend->service =  gdata_documents_service_new (CLIENT_ID);
	backend->entries_type = g_hash_table_new (g_str_hash, g_str_equal);

	/* Gnome proxy configuration is handle by libgdata*/
}

/* ************************************************************************* */
/* public utility functions */
void
g_vfs_backend_gdocs_rebuild_entries_type (GVfsBackendGdocs *backend, GCancellable *cancellable, GError **error)
{
	GList					*entries_list, *i;
	GDataDocumentsQuery		*query;
	GDataDocumentsFeed		*tmp_feed;

	GDataDocumentsService	*service = backend->service;
	
	entries_list = NULL;
	
	/*Get all entries (as feed) on the server*/
	query = gdata_documents_query_new (NULL);
	gdata_documents_query_set_show_folders (query, TRUE);
	tmp_feed = gdata_documents_service_query_documents (service, query, cancellable, NULL, NULL, error);
	g_object_unref (query);

	if (*error != NULL)
	{
		if (tmp_feed != NULL)
			g_object_unref (tmp_feed);
		return;
	}

	entries_list = gdata_feed_get_entries (GDATA_FEED (tmp_feed));
	for (i = entries_list; i != NULL; i=i->next)
	{
		const gchar *entry_id = gdata_documents_entry_get_document_id (GDATA_DOCUMENTS_ENTRY (i->data));
		g_hash_table_insert (backend->entries_type, entry_id, G_OBJECT_TYPE (i->data));
	}
}

/* ************************************************************************* */
/* virtual functions overrides */
static void
do_mount (GVfsBackend *backend, GVfsJobMount *job, GMountSpec *mount_spec, GMountSource *mount_source,  gboolean is_automount)
{
	gchar				*username, *dummy_host, *tmp, *ask_user, *ask_password, *prompt, *display_name;
	gboolean			aborted, retval, save_password;
	GMountSpec			*gdocs_mount_spec;
	GAskPasswordFlags	flags;
	guint				mount_try;

	GPasswordSave		password_save = G_PASSWORD_SAVE_NEVER;
	GVfsBackendGdocs	*gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
	GError				*error = NULL;
	
	prompt = NULL;
	prompt = NULL;
	dummy_host = NULL;
	tmp = NULL;
	save_password = FALSE;	

	/*Get usename*/
	username = g_strdup (g_mount_spec_get (mount_spec, "user"));
	tmp = dummy_host = g_strdup (g_mount_spec_get (mount_spec, "host"));


	g_print ("Mounting\n");
	/*Set the password asking flags.*/
	if (!username)
		flags = G_ASK_PASSWORD_NEED_USERNAME;
	else
	{
		flags =  G_ASK_PASSWORD_NEED_PASSWORD;
		if (g_vfs_keyring_is_available ())
			flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;
	}

	if (username)
	{
		/*Check if the password as already been saved for the user*/
		if (!g_vfs_keyring_lookup_password (username, NULL, NULL, "gdata", NULL, NULL, 0, &ask_user, NULL, &ask_password))
		{
			prompt = g_strdup_printf (_("Enter %s's google documents password"), username);
			if (!g_mount_source_ask_password (mount_source, prompt, username, NULL, flags, &aborted, &ask_password, &ask_user,	NULL, FALSE,
						&password_save) || aborted)
			{
				g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Password dialog cancelled"));
				g_free (username);
				g_free (ask_user);
				g_free (ask_password);
				g_free (prompt);
				return;
			}
			save_password = TRUE;
			ask_user = g_strdup (username);
			g_free (prompt);
		}
	}
	else
	{
		prompt = g_strdup ("Enter a username to access google documents.");
		if (!g_mount_source_ask_password (mount_source, prompt, username, NULL, flags, &aborted, &ask_password, &ask_user,	NULL, FALSE,
					&password_save) || aborted)
		{
			g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Password dialog cancelled"));
			g_free (username);
			g_free (ask_user);
			g_free (prompt);
			g_free (ask_password);
			return;
		}
		g_free (prompt);
		username = g_strdup (ask_user);
		if (!g_vfs_keyring_lookup_password (username, NULL, NULL, "gdata", NULL, NULL, 0, &ask_user, NULL, &ask_password))
		{
			/*Set password asking prompt and flags*/
			prompt = g_strdup_printf (_("Enter %s's google documents password"), username);
			flags = G_ASK_PASSWORD_NEED_PASSWORD;
			if (g_vfs_keyring_is_available ())
				flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;

			if (!g_mount_source_ask_password (mount_source, prompt, username, NULL, flags, &aborted, &ask_password, &username,	NULL, FALSE,
						&password_save) || aborted)
			{
				g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Password dialog cancelled"));
				g_free (username);
				g_free (ask_user);
				g_free (ask_password);
				g_free (prompt);
				return;
			}
			save_password = TRUE;
			g_free (prompt);
		}
	}

	mount_try = 0;
	/*Try to connect to the server*/
	while (mount_try < 3) /*TODO find something better than 3 tries hard coded*/
	{
		g_print ("-> Username: %s\n", ask_user);
		g_print ("password: ***\n");
		retval = gdata_service_authenticate (GDATA_SERVICE (gdocs_backend->service), ask_user, ask_password, NULL, &error);
		if (retval == TRUE)
		{
			/*save the password*/
			if (save_password == TRUE)
			{
				g_vfs_keyring_save_password (username, NULL, NULL, "gdata", NULL, NULL, 0,
						ask_password, password_save);
			}

			if (dummy_host == NULL)
				dummy_host = "gdocs.com";

			/*Mount it*/
			gdocs_mount_spec= g_mount_spec_new ("gdocs");
			g_mount_spec_set (gdocs_mount_spec, "user", ask_user);
			g_mount_spec_set (gdocs_mount_spec, "host", dummy_host);

			display_name = g_strdup_printf (_("%s@%s's google documents"), ask_user, dummy_host);
			g_vfs_backend_set_display_name (backend, display_name);
			g_free (display_name);
			g_free (tmp);
			g_free (ask_user);

			g_vfs_backend_set_mount_spec (backend, gdocs_mount_spec);
			g_mount_spec_unref (gdocs_mount_spec);
			g_vfs_backend_set_icon_name (backend, "folder-remote");
			break;
		}
		else
		{
			flags = G_ASK_PASSWORD_NEED_PASSWORD;
			prompt = g_strdup_printf (_("Enter %s's google documents password"), username);
			if (g_vfs_keyring_is_available ())
				flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;
			if (!g_mount_source_ask_password (mount_source, prompt, username, NULL, flags, &aborted, &ask_password, &username,	NULL, FALSE,
						&password_save) || aborted)
			{
				g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Password dialog cancelled"));
				g_free (username);
				g_free (ask_password);
				g_free (prompt);
				return;
			}
			save_password = TRUE;
			g_free (prompt);
		}
		mount_try++;
	}

	g_free (username);
	g_free (ask_password);

	if (retval == FALSE)
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
	else
	{
		g_print ("===Connected\n");
		g_vfs_job_succeeded (G_VFS_JOB (job));
	}
}

void
do_move (GVfsBackend *backend, GVfsJobMove *job, const char *source, const char *destination, GFileCopyFlags flags,
 		 GFileProgressCallback progress_callback, gpointer progress_callback_data)
{
	GDataDocumentsEntry		*new_entry, *renamed_document;
	GVfsGDataFile			*source_file, *destination_folder, *containing_folder;
	gchar					*destination_folder_name;
	
	gboolean				need_rename = FALSE;
	gboolean				move = TRUE;
	gboolean				move_to_root = FALSE;
	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	new_entry = NULL;

	if (flags & G_FILE_COPY_BACKUP)
	{
		/* TODO, Implement it*/ 
		g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_CANT_CREATE_BACKUP, _("backups not supported yet"));
		return;
	}

	source_file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
	if (error != NULL)
	{

		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (source_file != NULL)
			g_object_unref (source_file);
		return;
	}


	if (g_strcmp0 (destination, "/") == 0)
		move_to_root = TRUE;

	if (!move_to_root)
	{
		destination_folder = g_vfs_gdata_file_new_folder_from_gvfs (G_VFS_BACKEND_GDOCS (backend), destination, cancellable, &error);
		destination_folder_name = g_vfs_gdata_file_get_parent_id_from_gvfs (destination);

		if (g_strcmp0 (destination_folder_name, "/") == 0)
		{
			gchar *source_folder = g_vfs_gdata_file_get_parent_id_from_gvfs (source);
			g_print ("Test IS ////");

			need_rename = TRUE;
			move = FALSE;
			g_clear_error (&error);
			if (error != NULL)
				g_print ("Folder as just been cleared");

			if (g_strcmp0 (source_folder, "/") != 0)
				move_to_root = TRUE;

			if (destination_folder != NULL)
				g_object_unref (destination_folder);

		}

		if (move)
		{
			if (destination_folder == NULL)
			{
				g_print ("Pourquoi t la??=====\n");
				g_clear_error (&error);
				destination_folder = g_vfs_gdata_file_new_folder_from_gvfs (G_VFS_BACKEND_GDOCS (backend), destination_folder_name, cancellable, &error);
				need_rename = TRUE;
			}
			g_print ("Pourquoi t la??\n");
			if (error != NULL)
			{
				g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
				g_error_free (error);
				if (destination_folder != NULL)
					g_object_unref (destination_folder);
				return;
			}

			/*Move the document on the server*/
			new_entry = gdata_documents_service_move_document_to_folder (service,
					GDATA_DOCUMENTS_ENTRY (g_vfs_gdata_file_get_gdata_entry (source_file)),
					GDATA_DOCUMENTS_FOLDER (g_vfs_gdata_file_get_gdata_entry (destination_folder)),
					cancellable, &error);
			if (error != NULL)
			{
				g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
				g_error_free (error);
				g_object_unref (source_file);
				g_object_unref (destination_folder);
				return;
			}

			g_object_unref (source_file);
			g_object_unref (destination_folder);
		}
	}
	g_free (destination_folder_name);

	if (move_to_root)
	{
		g_print ("Bien");
		containing_folder = g_vfs_gdata_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			g_object_unref (source_file);
			g_object_unref (containing_folder);
			return;
		}
		new_entry = gdata_documents_service_remove_document_from_folder (service,
																		 GDATA_DOCUMENTS_ENTRY (g_vfs_gdata_file_get_gdata_entry (source_file)),
																		 GDATA_DOCUMENTS_FOLDER (g_vfs_gdata_file_get_gdata_entry (containing_folder)),
														 				 cancellable, &error);
		g_object_unref (source_file);
		g_object_unref (containing_folder);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			return;
		}
	}

	if (need_rename)
	{
		gchar *new_filename = g_vfs_gdata_file_get_document_id_from_gvfs (destination);

		if (error != NULL)
			g_print ("ba Pkoi");

		g_print ("Renaming file");
		if (new_entry == NULL)
			new_entry = g_vfs_gdata_file_get_gdata_entry (source_file);
		/*We rename the entry source entry*/
		gdata_entry_set_title (new_entry, new_filename);
		g_free (new_filename);

		renamed_document = gdata_documents_service_update_document (service, new_entry, NULL, NULL, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			g_object_unref (source_file);
			return;
		}
	}
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_enumerate (GVfsBackend *backend, GVfsJobEnumerate *job, const char *dirname, GFileAttributeMatcher *matcher,
			  GFileQueryInfoFlags query_flags)
{
	gchar					*folder_id ;
	GList					*i; /*GDataDocumentsEntry*/
	GFileInfo				*info;
	GDataDocumentsFeed		*documents_feed;
	GDataDocumentsQuery		*query ;

	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	/*Get documents properties*/
	query = gdata_documents_query_new (NULL);
	folder_id = g_vfs_gdata_file_get_document_id_from_gvfs (dirname);
	if (strcmp (dirname, "/") != 0)
	{
		/*Sets the query folder id*/
		gdata_documents_query_set_folder_id (query, folder_id);
		g_print ("Folder ID: %s", folder_id);
	}
	gdata_documents_query_set_show_folders (query, TRUE);
	documents_feed = gdata_documents_service_query_documents (service, query, cancellable, NULL, NULL, &error);
	g_object_unref (query);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		g_free (folder_id);
		if (documents_feed != NULL)
			g_object_unref (documents_feed);
		return;
	}

	g_vfs_job_succeeded (G_VFS_JOB (job));

	/*List documents*/
	for (i = gdata_feed_get_entries (GDATA_FEED (documents_feed)); i != NULL; i = i->next)
	{
		gchar *path, *parent_id;

		info = NULL;
		path = gdata_documents_entry_get_path (GDATA_DOCUMENTS_ENTRY (i->data));
		parent_id = g_vfs_gdata_file_get_parent_id_from_gvfs (path);

		/*We check that the file is in the selected folder (not in a child of it)*/
		if (g_strcmp0 (folder_id, parent_id) == 0)
		{
			GVfsGDataFile *file = g_vfs_gdata_file_new_from_gdata (G_VFS_BACKEND_GDOCS (backend), GDATA_ENTRY (i->data), &error);
			if (error != NULL)
			{
				g_free (path);
				g_free (parent_id);
				if (file != NULL)
					g_object_unref (file);
				g_clear_error (&error);
				continue;
			}

			info = g_vfs_gdata_file_get_info (file, info, matcher, &error);
			if (error != NULL)
			{
				g_free (path);
				g_free (parent_id);
				g_clear_error (&error);
				if (file != NULL)
					g_object_unref (file);
				continue;
			}
				
			g_vfs_job_enumerate_add_info (job, info);
			g_object_unref (file);
		}
		g_free (path);
		g_free (parent_id);
	}

	g_free (folder_id);
	g_vfs_job_enumerate_done (job);
}

static void
do_make_directory (GVfsBackend *backend, GVfsJobMakeDirectory *job, const char *filename)
{
	gchar					*title;
	GDataCategory			*folder_category;
	GDataDocumentsFolder	*folder, *new_folder;
	GVfsGDataFile			*destination_folder;

	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	title = g_vfs_gdata_file_get_document_id_from_gvfs (filename);
	if (g_strcmp0 (title, "/") == 0)
	{
		g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Can't create a root directory"));
		g_free (title);
		return;

	}

	folder = gdata_documents_folder_new (NULL);
	folder_category = gdata_category_new ("http://schemas.google.com/docs/2007#folder", "http://schemas.google.com/g/2005#kind", "folder");
	gdata_entry_add_category (GDATA_ENTRY (folder), folder_category);
	g_object_unref (folder_category);
	gdata_entry_set_title (GDATA_ENTRY (folder), title);
	g_free (title);

	destination_folder = g_vfs_gdata_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		g_object_unref (folder);
		if (destination_folder != NULL)
			g_object_unref (destination_folder);
		return;
	}

	new_folder = GDATA_DOCUMENTS_FOLDER (gdata_documents_service_upload_document (service, GDATA_DOCUMENTS_ENTRY (folder), NULL, 
										 g_vfs_gdata_file_get_gdata_entry (destination_folder), cancellable, &error));
	g_object_unref (folder);
	g_object_unref (destination_folder);
	if (new_folder != NULL)
		g_object_unref (new_folder);

	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_open_for_read (GVfsBackend *backend, GVfsJobOpenForRead *job, const char *filename)
{
	gchar				*uri;
	SoupMessage			*msg;
	GVfsGDataFile		*file;
	GOutputStream		*stream;

	GError				*error = NULL;
	GCancellable		*cancellable = G_VFS_JOB (job)->cancellable;
	GVfsBackendGdocs	*gdocs_backend = G_VFS_BACKEND_GDOCS (backend);

	g_print ("OPEN READ\n");

	file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		return;
	}

	uri = g_vfs_gdata_file_get_download_uri (file, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		g_free (uri);
		return;
	}

	stream = gdata_download_stream_new (GDATA_SERVICE (gdocs_backend->service), uri);
	g_free (uri);
	g_object_unref (file);

	g_vfs_job_open_for_read_set_handle (job, stream);
	g_vfs_job_open_for_read_set_can_seek (job, FALSE);
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_read (GVfsBackend *backend, GVfsJobRead *job, GVfsBackendHandle handle, char *buffer, gsize bytes_requested)
{
	gssize				n_bytes;

	GError				*error = NULL;
	GOutputStream		*stream = G_INPUT_STREAM (handle);
	GVfsBackendGdocs	*gdocs_backend = G_VFS_BACKEND_GDOCS (backend);

	g_print ("DO READ\n");
	n_bytes = g_input_stream_read (stream, buffer, bytes_requested, G_VFS_JOB (job)->cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		return;
	}

	if (n_bytes >= 0)
		g_vfs_job_read_set_size (job, n_bytes);

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_close_read (GVfsBackend *backend, GVfsJobCloseRead *job, GVfsBackendHandle handle)
{
	g_object_unref (job);
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_delete (GVfsBackend *backend, GVfsJobDelete *job, const char *filename)
{
	GVfsGDataFile	*file;

	GError			*error = NULL;
	GCancellable	*cancellable = G_VFS_JOB (job)->cancellable;
	GDataService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

		
	file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		return;
	}

	gdata_service_delete_entry (service,
								GDATA_ENTRY (g_vfs_gdata_file_get_gdata_entry (file)),
								cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		return;
	}
	g_print ("%s :deleted\n", gdata_entry_get_title (g_vfs_gdata_file_get_gdata_entry (file)));

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_query_info (GVfsBackend *backend, GVfsJobQueryInfo *job, const char *filename, GFileQueryInfoFlags flags, GFileInfo *info, GFileAttributeMatcher *matcher)
{
	GVfsGDataFile			*file;

	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;

	file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		return;
	}

	info = g_vfs_gdata_file_get_info (file, info, matcher, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		if (info != NULL)
			g_object_unref (info);
		return;
	}

	g_print ("File Info: %s", g_file_info_get_content_type (info));

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_replace (GVfsBackend *backend, GVfsJobOpenForWrite *job,	const char *filename, const char *etag,	gboolean make_backup, GFileCreateFlags flags)
{
	GFile					*local_file;
	GVfsGDataFile			*file;
	GDataDocumentsEntry		*new_entry;

	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	if (make_backup)
	{
		/* FIXME: implement! */
		g_set_error_literal (&error,
				G_IO_ERROR,
				G_IO_ERROR_CANT_CREATE_BACKUP,
				_("backups not supported yet"));
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		return;
	}

	file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		return;
	}

	local_file = g_file_new_for_path (filename);
	new_entry = gdata_documents_service_update_document (service, GDATA_DOCUMENTS_ENTRY (g_vfs_gdata_file_get_gdata_entry (file)),
														 local_file, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		g_object_unref (local_file);
		return;
	}
}

static void
do_push (GVfsBackend *backend, GVfsJobPull *job, const char *destination, const char *local_path, GFileCopyFlags flags,
		 gboolean remove_source, GFileProgressCallback progress_callback, gpointer progress_callback_data)
{
	gchar					*destination_filename;
	GDataDocumentsEntry		*entry, *new_entry;
	GFile					*local_file;
	GVfsGDataFile			*destination_folder;

	GError					*error = NULL;
	GDataDocumentsFolder	*folder_entry = NULL; 
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = (G_VFS_BACKEND_GDOCS (backend)->service);

	destination_folder = g_vfs_gdata_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), destination, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (destination_folder != NULL)
			g_object_unref (destination_folder);
		g_object_unref (local_file);
		return;
	}

	if (g_vfs_gdata_file_is_root (destination_folder))
	{
		g_object_unref (destination_folder);
		destination_folder = NULL;
	}
	else
		folder_entry = g_vfs_gdata_file_get_gdata_entry (destination_folder),

	entry = gdata_documents_spreadsheet_new (NULL);
	destination_filename = g_vfs_gdata_file_get_document_id_from_gvfs (destination);
	gdata_entry_set_title (GDATA_ENTRY (entry), destination_filename);
	g_free (destination_filename);

	local_file = g_file_new_for_path (local_path);
	new_entry = gdata_documents_service_upload_document (GDATA_DOCUMENTS_SERVICE (service), entry, local_file,
														 folder_entry, cancellable, &error);
	g_object_unref (entry);
	if (destination_folder != NULL)
		g_object_unref (destination_folder);
	g_object_unref (local_file);
	if (new_entry != NULL)
		g_object_unref (new_entry);

	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_pull (GVfsBackend *backend, GVfsJobPull *job, const char *source, const char *local_path, GFileCopyFlags flags,
		gboolean remove_source, GFileProgressCallback progress_callback, gpointer progress_callback_data)
{
	GVfsGDataFile			*file;
	GFile					*new_file;

	GError					*error = NULL;
	gchar					*content_type = NULL;
	gboolean				replace_if_exists = FALSE;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	file =  g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		return;
	}

	if (flags & G_FILE_COPY_OVERWRITE)
		replace_if_exists = TRUE;

	new_file = g_vfs_gdata_file_download_file (file, &content_type, local_path, replace_if_exists, TRUE, cancellable, &error);
	g_object_unref (file);
	if (new_file != NULL)
		g_object_unref (new_file);

	if (error != NULL)
	{
		g_print ("error downloading");
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_create (GVfsBackend *backend, GVfsJobOpenForWrite *job, const char *filename, GFileCreateFlags flags)
{
	const gchar			*title, *content_type;
	gchar				*upload_uri;
	GDataCategory		*category;
	GFile				*file;
	GFileInfo			*file_info;
	GVfsGDataFile		*parent_folder;
	GOutputStream		*output_stream;

	GError				*error = NULL;
	GDataDocumentsEntry *entry = NULL;

	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService *service = G_VFS_BACKEND_GDOCS (backend)->service;

	file = g_file_new_for_path (filename);
	file_info =  g_file_query_info (file, "standard::display-name,standard::content-type", G_FILE_QUERY_INFO_NONE, NULL, &error);
	g_object_unref (file);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file_info != NULL)
			g_object_unref (file_info);
		return;
	}

	content_type = g_file_info_get_content_type (file_info);
	title = g_file_info_get_display_name (file_info);

	parent_folder =g_vfs_gdata_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file_info != NULL)
			g_object_unref (file_info);
		if (parent_folder != NULL)
			g_object_unref (parent_folder);
		return;
	}
	upload_uri = gdata_documents_service_get_upload_uri (GDATA_DOCUMENTS_FOLDER (g_vfs_gdata_file_get_gdata_entry (parent_folder)));

	output_stream = gdata_upload_stream_new (service, SOUP_METHOD_POST, upload_uri, NULL, title, content_type);
	g_free (upload_uri);
	g_object_unref (file_info);

	g_vfs_job_open_for_write_set_can_seek (job, FALSE);
	g_vfs_job_open_for_write_set_handle (job, output_stream);
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_append_to (GVfsBackend *backend, GVfsJobOpenForWrite *job, const char *filename, GFileCreateFlags flags)
{
	GFileInfo				*file_info;
	gchar					*upload_uri;
	const gchar				*content_type;
	GDataEntry				*entry, *slug;
	GDataUploadStream		*output_stream;
	GFile					*file ;
	GVfsGDataFile			*gdata_file;
		
	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	gdata_file	= g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (gdata_file != NULL)
			g_object_unref (gdata_file);
		return;
	}

	file = g_file_new_for_path (filename);
	file_info =  g_file_query_info (file, "standard::display-name,standard::content-type", G_FILE_QUERY_INFO_NONE, NULL, &error);
	g_object_unref (file);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		g_object_unref (gdata_file);
		if (file_info != NULL)
			g_object_unref (file_info);
		return;
	}
	content_type = g_file_info_get_content_type (file_info);
	slug = g_file_info_get_display_name (file_info);

	entry = GDATA_ENTRY (g_vfs_gdata_file_get_gdata_entry (gdata_file));
	upload_uri = gdata_entry_look_up_link (GDATA_ENTRY (entry), GDATA_LINK_EDIT_MEDIA);

	output_stream = gdata_upload_stream_new (service, SOUP_METHOD_PUT, gdata_link_get_uri (upload_uri), entry, slug, content_type);

	g_object_unref (file_info);

	g_vfs_job_open_for_write_set_can_seek (job, FALSE);
	g_vfs_job_open_for_write_set_handle (job, output_stream);
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_write (GVfsBackend *backend, GVfsJobWrite *job,  GVfsBackendHandle _handle, char *buffer, gsize buffer_size)
{
	gssize				n_bytes;

	GError				*error = NULL;
	GCancellable		*cancellable = G_VFS_JOB (job)->cancellable;
	GDataUploadStream	*output_stream = G_OUTPUT_STREAM (_handle);

	n_bytes = g_output_stream_write (output_stream, buffer, buffer_size, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	if (n_bytes >= 0)
		g_vfs_job_write_set_written_size (job, n_bytes);

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_close_write (GVfsBackend *backend, GVfsJobCloseWrite *job, GVfsBackendHandle _handle)
{
	g_object_unref (_handle);
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
g_vfs_backend_gdocs_class_init (GVfsBackendGdocsClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GVfsBackendClass *backend_class;

	gobject_class->finalize  = g_vfs_backend_gdocs_finalize;
	gobject_class->dispose  = g_vfs_backend_gdocs_dispose;

	backend_class = G_VFS_BACKEND_CLASS (klass);

	backend_class->mount = do_mount;
	backend_class->make_directory = do_make_directory;
	backend_class->enumerate = do_enumerate;
	backend_class->move = do_move;
	backend_class->delete = do_delete;
	backend_class->replace = do_replace;
	backend_class->pull = do_pull;
	backend_class->push = do_push;
	backend_class->open_for_read = do_open_for_read;
	backend_class->read = do_read;
	backend_class->close_read = do_close_read;
	backend_class->close_write = do_close_write;
	backend_class->write = do_write;
	backend_class->query_info = do_query_info;
/*	backend_class->create = do_create;*/

}
