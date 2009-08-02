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
  const char         *debug;
  backend->service =  gdata_documents_service_new (CLIENT_ID);
  backend->entries_type = g_hash_table_new (g_str_hash, g_str_equal);

  /* Gnome proxy configuration is handle by libgdata*/

  /* Logging */
  debug = g_getenv ("GVFS_GDOCS_DEBUG");
  if (debug)
    {
		/*TODO*/
    }
}

/* ************************************************************************* */
/* public utility functions */
void
g_vfs_backend_gdocs_rebuild_entries_type (GVfsBackendGdocs *backend, GCancellable *cancellable, GError **error)
{
	GDataDocumentsQuery *query;
	GDataDocumentsFeed *tmp_feed;
	GList *entries_list = NULL, *i;
	GDataDocumentsService *service = backend->service;

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
	g_print ("Rebuild entris\n");
	for (i = entries_list; i != NULL; i=i->next)
	{
		const gchar *entry_id = gdata_documents_entry_get_document_id (GDATA_DOCUMENTS_ENTRY (i->data));
		g_print (" %s, %s \n", G_OBJECT_TYPE_NAME (i->data), entry_id);
		g_hash_table_insert (backend->entries_type, entry_id, G_OBJECT_TYPE (i->data));
	}
}

/* ************************************************************************* */
/* virtual functions overrides */
static void
do_mount (GVfsBackend *backend, GVfsJobMount *job, GMountSpec *mount_spec, GMountSource *mount_source,  gboolean is_automount)
{
	GVfsBackendGdocs *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
	GMountSpec *gdocs_mount_spec;
	gchar *username, *dummy_host=NULL, *ask_user, *ask_password, *prompt=NULL, *display_name;
	gboolean aborted, retval, save_password=FALSE;
	GPasswordSave password_save = G_PASSWORD_SAVE_NEVER;
	GAskPasswordFlags flags;
	GError *error;
	guint mount_try;

	/*Get usename*/
	username = g_strdup (g_mount_spec_get (mount_spec, "user"));
	dummy_host = g_strdup (g_mount_spec_get (mount_spec, "host"));


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
			ask_user = username;
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
	
			/*Mount it*/
			gdocs_mount_spec= g_mount_spec_new ("gdocs");
			g_mount_spec_set (gdocs_mount_spec, "user", ask_user);
			if (dummy_host)
			{
				g_mount_spec_set (gdocs_mount_spec, "host", dummy_host);
				display_name = g_strdup_printf (_("%s@%s's google documents"), ask_user, dummy_host);
			}
			else
				display_name = g_strdup_printf (_("%s's google documents"), ask_user);

			g_vfs_backend_set_mount_spec (backend, gdocs_mount_spec);
			g_mount_spec_unref (gdocs_mount_spec);
			g_vfs_backend_set_display_name (backend, display_name);
			g_free (display_name);
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
				g_free (ask_user);
				g_free (ask_password);
				g_free (prompt);
				return;
			}
			save_password = TRUE;
			g_free (prompt);
		}
		mount_try++;
	}
	
	g_free (ask_user);
	//g_free (username);
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
	GDataDocumentsService *service = G_VFS_BACKEND_GDOCS (backend)->service;
	GVfsGDataFile *source_file = NULL, *destination_folder = NULL;
	GDataDocumentsEntry *new_entry;
	GError *error = NULL;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;

	if (flags & G_FILE_COPY_BACKUP)
    {
      /* FIXME: implement? */
      g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_CANT_CREATE_BACKUP, _("backups not supported yet"));
      return;
    }

	source_file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);

	if (error != NULL)
	{

		g_print ("Error making GVfsGDataFile\n");
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (source_file != NULL)
			g_object_unref (source_file);
		return;
	}

	g_print ("\n\nDestination: %s\n", destination);
	if (g_strcmp0 (destination, "/") != 0)
	{
		destination_folder = g_vfs_gdata_file_new_folder_from_gvfs (G_VFS_BACKEND_GDOCS (backend), destination, cancellable, &error);

		if (error != NULL)
		{
			g_print ("Error making GVfsGDataFile::destination_folder\n");
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
		if (!GDATA_IS_DOCUMENTS_ENTRY (new_entry))
		{
			g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("Error moving file %s"), source);
			if (destination_folder != NULL)
				g_object_unref (destination_folder);
			return;
		}

		g_object_unref (source_file);
		g_object_unref (destination_folder);
	}
	else
	{
		destination_folder = g_vfs_gdata_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			g_object_unref (source_file);
			g_object_unref (destination_folder);
			return;
		}
		new_entry = gdata_documents_service_remove_document_from_folder (service, 
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

    g_vfs_job_succeeded (G_VFS_JOB (job));

}

static void
do_enumerate (GVfsBackend *backend, GVfsJobEnumerate *job, const char *dirname, GFileAttributeMatcher *matcher,
		 	  GFileQueryInfoFlags query_flags)
{
	GDataDocumentsService *service = G_VFS_BACKEND_GDOCS (backend)->service;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsFeed *documents_feed;
	GDataDocumentsQuery *query;
	GError *error = NULL;
	GList *i; /*GDataDocumentsEntry*/
	GFileInfo *info;
	gchar *folder_id = g_vfs_gdata_file_get_document_id_from_gvfs (dirname);

	query = gdata_documents_query_new (NULL);
	if (strcmp (dirname, "/") != 0)
	{
		/*Sets the query folder id*/
		gdata_documents_query_set_folder_id (query, folder_id);
		g_print ("Folder ID: %s", folder_id);
	}
	gdata_documents_query_set_show_folders (query, TRUE);

	documents_feed = gdata_documents_service_query_documents (service, query, cancellable, NULL, NULL, &error);
	g_print ("Folder ID: %s", folder_id);
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
	for (i = gdata_feed_get_entries (GDATA_FEED (documents_feed)); i != NULL; i = i->next)
	{
		info = NULL;
		gchar *path, *parent_id;

		path = gdata_documents_entry_get_path (GDATA_DOCUMENTS_ENTRY (i->data));
		parent_id = g_vfs_gdata_file_get_parent_id_from_gvfs (path);

		g_print ("Path %s Folder ID %s, Parents ID %s\n", path, folder_id, parent_id);
		/*We check that the file is in the selected folder (not in a child of it)*/
		if (g_strcmp0 (folder_id, parent_id) == 0)
		{
			GVfsGDataFile *file = g_vfs_gdata_file_new_from_gdata (G_VFS_BACKEND_GDOCS (backend), GDATA_ENTRY (i->data), &error);


			if (error != NULL)
			{
				g_print ("error getting the file");
				g_clear_error (&error);
			}
			else
			{	
				info = g_vfs_gdata_file_get_info (file, info, matcher, &error);
				if (error != NULL)
				{
					g_print ("error getting the infos");
					g_clear_error (&error);
				}
				else
				{
					g_vfs_job_enumerate_add_info (job, info);
				}
			}
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
	GError *error = NULL;
	GDataCategory *folder_category;
	GDataDocumentsFolder *folder, *new_folder;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	gchar *title = g_vfs_gdata_file_get_document_id_from_gvfs (filename);
	GDataDocumentsService *service = G_VFS_BACKEND_GDOCS (backend)->service;

	if (g_strcmp0 (title, "/") == 0)
	{
		g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Can't create a root directory"));
		g_free (title);
		return;

	}

	folder = gdata_documents_folder_new (NULL);
	folder_category = gdata_category_new ("http://schemas.google.com/docs/2007#folder", "http://schemas.google.com/g/2005#kind", "folder");
	gdata_entry_set_title (GDATA_ENTRY (folder), title);
	gdata_entry_add_category (GDATA_ENTRY (folder), folder_category);
	g_free (title);

	new_folder = GDATA_DOCUMENTS_FOLDER (gdata_documents_service_upload_document (service, GDATA_DOCUMENTS_ENTRY (folder), NULL, NULL, cancellable, &error));
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

    g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_open_for_read (GVfsBackend *backend,
                  GVfsJobOpenForRead *job,
                  const char *filename)
{
	gchar *uri;
	SoupMessage *msg;
	GInputStream *stream;
	GError *error = NULL;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	GVfsBackendGdocs *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
	GVfsGDataFile *file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);
	if (error != NULL)
	{
		if (file != NULL)
			g_object_unref (file);
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		return;
	}
	
	uri = g_vfs_gdata_file_get_download_uri (file, cancellable, &error);
	if (error != NULL)
	{
		if (file != NULL)
			g_object_unref (file);
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_free (uri);
		return;
	}

	//stream = gdata_download_stream_new (GDATA_SERVICE (gdocs_backend->service), uri);
	g_free (uri);
	g_object_unref (file);

	g_vfs_job_open_for_read_set_handle (job, stream);
	g_vfs_job_open_for_read_set_can_seek (job, FALSE);

    g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_read (GVfsBackend *backend, GVfsJobRead *job, GVfsBackendHandle handle, char *buffer, gsize bytes_requested)
{
	gssize n_bytes;
	GError *error = NULL;
	GInputStream *stream = G_INPUT_STREAM (handle);
	GVfsBackendGdocs *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);

	n_bytes = g_input_stream_read (stream,
			buffer,	bytes_requested, G_VFS_JOB (job)->cancellable, &error);

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
do_close_read (GVfsBackend *     backend,
               GVfsJobCloseRead *job,
               GVfsBackendHandle handle)
{
	g_object_unref (job);
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_delete (GVfsBackend *backend,
	   GVfsJobDelete *job,
	   const char *filename)
{
	GError *error = NULL;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	GDataService *service = G_VFS_BACKEND_GDOCS (backend)->service;

	GVfsGDataFile *file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), filename, cancellable, &error);

	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		return;
	}
	
	g_print ("Delete entry...");
	gdata_service_delete_entry (service,
								GDATA_ENTRY (g_vfs_gdata_file_get_gdata_entry (file)),
								cancellable, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}
	g_print ("%s :deleted\n", gdata_entry_get_title (g_vfs_gdata_file_get_gdata_entry (file)));

	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_query_info_on_write (GVfsBackend *backend,
			GVfsJobQueryInfoWrite *job,
			GVfsBackendHandle _handle,
			GFileInfo *info,
			GFileAttributeMatcher *matcher)
{
	GError *error = NULL;

	GVfsGDataFile *file = g_vfs_gdata_file_new_from_gdata (G_VFS_BACKEND_GDOCS (backend), GDATA_ENTRY (_handle), &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		return;
	}

	info = g_vfs_gdata_file_get_info (file, info, matcher, &error);

	if (error == NULL)
	{
		g_vfs_job_succeeded (G_VFS_JOB (job));
		g_object_unref (file);
	}
	else
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
	}
}

static void
do_replace (GVfsBackend *backend,
	    GVfsJobOpenForWrite *job,
	    const char *filename,
	    const char *etag,
	    gboolean make_backup,
	    GFileCreateFlags flags)
{
	GError *error = NULL;
	GVfsGDataFile *file;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;

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
		return;
	}
	if (g_strcmp0 (etag, gdata_entry_get_etag (g_vfs_gdata_file_get_gdata_entry (file))) != 0)
	{
		g_set_error_literal (&error,
				G_IO_ERROR,
				G_IO_ERROR_WRONG_ETAG,
				_("The file was externally modified"));
	}
	/*TODO*/
}

static void
do_pull (GVfsBackend *backend, GVfsJobPull *job, const char *source, const char *local_path, GFileCopyFlags flags, 
		 gboolean remove_source, GFileProgressCallback progress_callback, gpointer progress_callback_data)
{
	GError *error = NULL;
	GFile *new_file = NULL;
	gchar *content_type = NULL;
	gboolean replace_if_exists = FALSE;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	GDataDocumentsService *service = G_VFS_BACKEND_GDOCS (backend)->service;
	GVfsGDataFile *file =  g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
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
	if (error != NULL)
	{
		g_print ("error downloading");
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (new_file != NULL)
			g_object_unref (new_file);
		return;
	}
	if (new_file != NULL)
		g_object_unref (new_file);

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
	backend_class->close_read = do_close_read;
	backend_class->read = do_read;

}
