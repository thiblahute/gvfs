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
#include "gvfsgdocsfile.h"

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
	g_hash_table_destroy (backend->entries);
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
	backend->entries = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);

	/* Gnome proxy configuration is handle by libgdata*/
}

/* ************************************************************************* */
/* public utility functions */
void
g_vfs_backend_gdocs_rebuild_entries (GVfsBackendGdocs *backend, GCancellable *cancellable, GError **error)
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
	for (i = entries_list; i != NULL; i = i->next)
	{
		const gchar *entry_id = gdata_documents_entry_get_document_id (GDATA_DOCUMENTS_ENTRY (i->data));
		g_hash_table_insert (backend->entries, entry_id, g_vfs_gdocs_file_new_from_document_entry (backend, GDATA_DOCUMENTS_ENTRY (i->data), NULL));
	}
}

/* ************************************************************************* */
/* virtual functions overrides */
static void
do_mount (GVfsBackend *backend, GVfsJobMount *job, GMountSpec *mount_spec, GMountSource *mount_source,  gboolean is_automount)
{
	gchar				*ask_user, *ask_password, *prompt, *display_name, *full_username;
	const gchar			*username, *host;
	gboolean			aborted, retval;
	GMountSpec			*gdocs_mount_spec;
	GAskPasswordFlags	flags;

	GPasswordSave		password_save = G_PASSWORD_SAVE_NEVER;
	GVfsBackendGdocs	*gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
	GError				*error = NULL;
	
	/*Get usename*/
	username = g_mount_spec_get (mount_spec, "user");
	g_message ("Mounting %s @ %s\n", username, host);
	host = g_mount_spec_get (mount_spec, "host");
	if (host == NULL)
		host = "gmail.com";


	/*Set the password asking flags.*/
	flags =  G_ASK_PASSWORD_NEED_PASSWORD;
	if (g_vfs_keyring_is_available ())
			flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;
	if (username == NULL)
		flags |= G_ASK_PASSWORD_NEED_USERNAME;

	if (username != NULL)
	{
		/*Check if the password as already been saved for the user, we set the protocol as gdata can be shared by variours google services*/
		if (!g_vfs_keyring_lookup_password (username, host, NULL, "gdata", NULL, NULL, 0, &ask_user, NULL, &ask_password))
		{
			prompt = g_strdup_printf (_("Enter %s's google documents password"), username);
			if (!g_mount_source_ask_password (mount_source, prompt, username, NULL, flags, &aborted, &ask_password, &ask_user,
											  NULL, FALSE, &password_save) || aborted)
			{
				if (aborted)
					g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Password dialog cancelled"));
				else
					g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_FAILED, _("Password access issue"));
				g_free (ask_user);
				g_free (ask_password);
				g_free (prompt);

				return;
			}

			g_free (prompt);
		}
	}
	else
	{
		prompt =  "Enter username and password to access google documents.";
		if (!g_mount_source_ask_password (mount_source, prompt, username, NULL, flags, &aborted, &ask_password, &ask_user,	NULL, FALSE,
					&password_save) || aborted)
		{
			if (aborted)
				g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Password dialog cancelled"));
			else
				g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_FAILED, _("Password access issue"));

			g_free (ask_user);
			g_free (prompt);
			g_free (ask_password);
			return;
		}

	}

	if (ask_user == NULL)
	{
		full_username = g_strdup_printf ("%s@%s", username, host);
		ask_user = g_strdup (username);
	}
	else
		full_username = g_strdup_printf ("%s@%s", ask_user, host);

	/*Try to connect to the server*/
	while (TRUE)
	{
		g_message ("-> Username: %s\n", full_username);
		g_message ("password: ***\n");
		retval = gdata_service_authenticate (GDATA_SERVICE (gdocs_backend->service), full_username, ask_password, NULL, &error);
		if (retval == TRUE)
		{
			/*save the password*/
			g_vfs_keyring_save_password (username, NULL, NULL, "gdata", NULL, NULL, 0, ask_password, password_save);


			/*Mount it*/
			gdocs_mount_spec= g_mount_spec_new ("gdocs");
			g_mount_spec_set (gdocs_mount_spec, "user", ask_user);
			g_mount_spec_set (gdocs_mount_spec, "host", host);

			display_name = g_strconcat (full_username, "'s google documents");
			g_vfs_backend_set_display_name (backend, display_name);
			g_free (display_name);

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
				if (aborted)
					g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Password dialog cancelled"));
				else
					g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_FAILED, _("Password access issue"));
				g_free (ask_password);
				g_free (prompt);
				return;
			}

			g_free (prompt);
		}
	}

	g_free (ask_password);

	g_message ("===Connected\n");
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

void
do_move (GVfsBackend *backend, GVfsJobMove *job, const char *source, const char *destination, GFileCopyFlags flags,
 		 GFileProgressCallback progress_callback, gpointer progress_callback_data)
{
	GDataDocumentsEntry		*new_entry, *renamed_document;
	GVfsGDocsFile			*source_file, *destination_folder, *containing_folder;
	gchar					*destination_parent_id, *source_parent_id, *destination_id, *source_id;
	
	gboolean				need_rename = FALSE;
	gboolean				move = TRUE;
	gboolean				move_to_root = FALSE;
	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	new_entry = NULL;
	destination_folder = NULL;

	if (flags & G_FILE_COPY_BACKUP)
	{
		/* TODO, Implement it*/ 
		g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_CANT_CREATE_BACKUP, _("backups not supported yet"));
		return;
	}

	source_file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	source_id = g_path_get_basename (source);
	source_parent_id = g_path_get_parent_basename (source); 
	destination_id = g_path_get_basename (destination);
	destination_parent_id = g_path_get_parent_basename (destination);

	/*If we move a file to root without renaming it, the file shouldn't be in root*/
	if (g_strcmp0 (source_id, destination_id) == 0 && g_strcmp0 (destination_parent_id, "/") == 0 && g_strcmp0 (source_parent_id, "/") != 0)
		move_to_root = TRUE;
	
	g_message ("Source id: %s, destination ID: %s", source_id, destination_id);
	/*We check if we need to rename, if we need, the detination folder should be the parent one*/
	if (g_strcmp0 (source_id, destination_id) != 0)
		need_rename = TRUE;
	else
		destination_folder = g_vfs_gdocs_file_new_folder_from_gvfs (G_VFS_BACKEND_GDOCS (backend), destination_parent_id, cancellable, &error);

	g_free (source_id);
	g_free (destination_id);

	if (!move_to_root)
	{
		if (destination_folder == NULL)
			destination_folder = g_vfs_gdocs_file_new_folder_from_gvfs (G_VFS_BACKEND_GDOCS (backend), destination, cancellable, &error);

		/*If the destination is not a folder and the parent of the destination is the root, we rename the source file*/
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY) && g_strcmp0 (destination_parent_id, "/") == 0 &&
			g_strcmp0 (source_parent_id, "/") != 0)
		{
			g_clear_error (&error);
			move = FALSE;
			move_to_root = TRUE;
		}
	}

	if (!move_to_root)
	{
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			if (destination_folder != NULL)
				g_object_unref (destination_folder);
			return;
		}

		/*Move the document on the server*/
		g_message ("destination_folder: %s", gdata_documents_entry_get_document_id (g_vfs_gdocs_file_get_document_entry (destination_folder)));
		new_entry = gdata_documents_service_move_document_to_folder (service,
				GDATA_DOCUMENTS_ENTRY (g_vfs_gdocs_file_get_document_entry (source_file)),
				GDATA_DOCUMENTS_FOLDER (g_vfs_gdocs_file_get_document_entry (destination_folder)),
				cancellable, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			g_object_unref (destination_folder);
			return;
		}

	}
	g_free (destination_parent_id);
	/*if (destination_folder != NULL)
		g_object_unref (destination_folder);*/

	if (move_to_root)
	{
		g_message ("Is moving to root");
		/* we need to check for the error that could have happend building the destination_folder*/
		containing_folder = g_vfs_gdocs_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			return;
		}
		g_message ("Moving %s out of %s", gdata_documents_entry_get_document_id (g_vfs_gdocs_file_get_document_entry (source_file)),
										  gdata_documents_entry_get_document_id (g_vfs_gdocs_file_get_document_entry (containing_folder)));
		new_entry = gdata_documents_service_remove_document_from_folder (service,
																		 GDATA_DOCUMENTS_ENTRY (g_vfs_gdocs_file_get_document_entry (source_file)),
																		 g_vfs_gdocs_file_get_document_entry (containing_folder),
																		 cancellable, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			return;
		}
	}

	if (need_rename)
	{
		gchar *new_filename = g_path_get_basename (destination);

		g_message ("Renaming file: %s", new_filename);
		if (new_entry == NULL)
			new_entry = g_vfs_gdocs_file_get_document_entry (source_file);
		/*We rename the entry source entry*/
		gdata_entry_set_title (new_entry, new_filename);
		g_free (new_filename);

		renamed_document = gdata_documents_service_update_document (service, new_entry, NULL, NULL, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			return;
		}
	}
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_set_display_name (GVfsBackend *backend,
                     GVfsJobSetDisplayName *job,
                     const char *filename,
                     const char *display_name)
{	
	GVfsGDocsFile			*file;
	gchar					*new_path, *dirname;
	GDataDocumentsEntry		*entry, *renamed_entry;

	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (g_vfs_gdocs_file_is_root (file))
	{
		g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Can't rename the root directory"));
	}
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}
	entry = g_vfs_gdocs_file_get_document_entry (file);
	gdata_entry_set_title (entry, display_name);
	
	renamed_entry = gdata_documents_service_update_document (service, entry, NULL, NULL, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (renamed_entry != NULL)
			g_object_unref (renamed_entry);
		return;
	}

	dirname = g_path_get_dirname (filename);
	new_path = g_build_filename (dirname, gdata_documents_entry_get_document_id (renamed_entry), NULL);
	g_free (dirname);
	g_object_unref (renamed_entry);
    g_vfs_job_set_display_name_set_new_path (job, new_path);
    g_vfs_job_succeeded (G_VFS_JOB (job));
	g_free (new_path);
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

	gboolean				in_folder =  FALSE;
	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	/*Get documents properties*/
	query = gdata_documents_query_new (NULL);
	folder_id = g_path_get_basename (dirname);
	if (strcmp (dirname, "/") != 0)
	{
		/*Sets the query folder id*/
		gdata_documents_query_set_folder_id (query, folder_id);
		in_folder = TRUE;
		g_message ("Folder ID: %s\n", folder_id);
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

	if (documents_feed == NULL)
		g_message ("FEED NULL???");

	/*List documents*/
	for (i = gdata_feed_get_entries (GDATA_FEED (documents_feed)); i != NULL; i = i->next)
	{
		gchar *path, *parent_id;

		info = NULL;
		path = gdata_documents_entry_get_path (GDATA_DOCUMENTS_ENTRY (i->data));
		parent_id = g_path_get_parent_basename (path);

		//g_message ("Path: %s folder ID %s, parent_id: %s", folder_id, parent_id);
		/*We check that the file is in the selected folder (not in a child of it)*/
		if (g_strcmp0 (folder_id, parent_id) == 0 || in_folder)
		{
			GVfsGDocsFile *file = g_vfs_gdocs_file_new_from_document_entry (G_VFS_BACKEND_GDOCS (backend), GDATA_ENTRY (i->data), &error);
			if (error != NULL)
			{
				g_free (path);
				g_free (parent_id);
				g_clear_error (&error);
				if (file =! NULL)
					g_object_unref (file);
				continue;
			}

			info = g_vfs_gdocs_file_get_info (file, info, matcher, &error);
			if (error != NULL)
			{
				g_free (path);
				g_free (parent_id);
				g_clear_error (&error);
				g_object_unref (file);
				continue;
			}
				
			g_object_unref (file);
			g_vfs_job_enumerate_add_info (job, info);
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
	GVfsGDocsFile			*destination_folder;

	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	title = g_path_get_basename (filename);
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

	destination_folder = g_vfs_gdocs_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
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
										 g_vfs_gdocs_file_get_document_entry (destination_folder), cancellable, &error));
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
	GVfsGDocsFile		*file;
	GOutputStream		*stream;

	GError				*error = NULL;
	GCancellable		*cancellable = G_VFS_JOB (job)->cancellable;
	GVfsBackendGdocs	*gdocs_backend = G_VFS_BACKEND_GDOCS (backend);

	g_message ("OPEN READ\n");

	file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	uri = g_vfs_gdocs_file_get_download_uri (file, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		g_free (uri);
		return;
	}

	stream = gdata_download_stream_new (GDATA_SERVICE (gdocs_backend->service), uri);
	g_free (uri);

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

	g_message ("DO READ\n");
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
	GVfsGDocsFile	*file;

	GError			*error = NULL;
	GCancellable	*cancellable = G_VFS_JOB (job)->cancellable;
	GDataService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

		
	file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	gdata_service_delete_entry (service,
								GDATA_ENTRY (g_vfs_gdocs_file_get_document_entry (file)),
								cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}
	g_message ("%s :deleted\n", gdata_entry_get_title (g_vfs_gdocs_file_get_document_entry (file)));

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_query_info (GVfsBackend *backend, GVfsJobQueryInfo *job, const char *filename, GFileQueryInfoFlags flags, GFileInfo *info, GFileAttributeMatcher *matcher)
{
	GVfsGDocsFile			*file;

	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;

	file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	info = g_vfs_gdocs_file_get_info (file, info, matcher, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (info != NULL)
			g_object_unref (info);
		return;
	}

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_replace (GVfsBackend *backend, GVfsJobOpenForWrite *job,	const char *filename, const char *etag,	gboolean make_backup, GFileCreateFlags flags)
{
	GFile					*local_file;
	GVfsGDocsFile			*file;
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

	file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	local_file = g_file_new_for_path (filename);
	new_entry = gdata_documents_service_update_document (service, GDATA_DOCUMENTS_ENTRY (g_vfs_gdocs_file_get_document_entry (file)),
														 local_file, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
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
	GVfsGDocsFile			*destination_folder;

	GError					*error = NULL;
	GDataDocumentsFolder	*folder_entry = NULL; 
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = (G_VFS_BACKEND_GDOCS (backend)->service);

	destination_folder = g_vfs_gdocs_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), destination, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		g_object_unref (local_file);
		return;
	}

	if (g_vfs_gdocs_file_is_root (destination_folder))
	{
		destination_folder = NULL;
	}
	else
		folder_entry = g_vfs_gdocs_file_get_document_entry (destination_folder),

	entry = gdata_documents_spreadsheet_new (NULL);
	destination_filename = g_path_get_basename (destination);
	gdata_entry_set_title (GDATA_ENTRY (entry), destination_filename);
	g_free (destination_filename);

	local_file = g_file_new_for_path (local_path);
	new_entry = gdata_documents_service_upload_document (GDATA_DOCUMENTS_SERVICE (service), entry, local_file,
														 folder_entry, cancellable, &error);
	g_object_unref (entry);
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
	GVfsGDocsFile			*file;
	GFile					*new_file;

	GError					*error = NULL;
	gchar					*content_type = NULL;
	gboolean				replace_if_exists = FALSE;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	file =  g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	if (flags & G_FILE_COPY_OVERWRITE)
		replace_if_exists = TRUE;

	new_file = g_vfs_gdocs_file_download_file (file, &content_type, local_path, replace_if_exists, TRUE, cancellable, &error);
	if (new_file != NULL)
		g_object_unref (new_file);

	if (error != NULL)
	{
		g_message ("error downloading");
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
	GVfsGDocsFile		*parent_folder;
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

	parent_folder = g_vfs_gdocs_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file_info != NULL)
			g_object_unref (file_info);
		return;
	}
	upload_uri = gdata_documents_service_get_upload_uri (GDATA_DOCUMENTS_FOLDER (g_vfs_gdocs_file_get_document_entry (parent_folder)));

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
	GVfsGDocsFile			*gdata_file;
		
	GError					*error = NULL;
	GCancellable			*cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	gdata_file	= g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

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
	slug = g_file_info_get_display_name (file_info);

	entry = GDATA_ENTRY (g_vfs_gdocs_file_get_document_entry (gdata_file));
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
	backend_class->create = do_create;
	backend_class->set_display_name = do_set_display_name;

}
