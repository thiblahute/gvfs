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


void do_move (GVfsBackend *backend, GVfsJobMove *job, const char *source, const char *destination, GFileCopyFlags flags,\
		GFileProgressCallback progress_callback, gpointer progress_callback_data);
G_DEFINE_TYPE (GVfsBackendGdocs, g_vfs_backend_gdocs, G_VFS_TYPE_BACKEND)


static void
g_vfs_backend_gdocs_finalize (GObject *object)
{
	/*TODO Check if I use it*/
	GVfsBackendGdocs *backend;

	backend = G_VFS_BACKEND_GDOCS (object);
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

/* ************************************************************************* */
/* virtual functions overrides */
static void
do_mount (GVfsBackend *backend,
          GVfsJobMount *job,
          GMountSpec *mount_spec,
          GMountSource *mount_source,
          gboolean is_automount)
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
				g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
						_("Password dialog cancelled"));
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
		prompt = "Enter a username to access google documents.";
		if (!g_mount_source_ask_password (mount_source, prompt, username, NULL, flags, &aborted, &ask_password, &ask_user,	NULL, FALSE,
					&password_save) || aborted)
			{
				g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
						_("Password dialog cancelled"));
				g_free (username);
				g_free (ask_user);
				g_free (ask_password);
				return;
			}
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
		g_print ("->Username: %s\n", ask_user);
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

static void
do_unmount (GVfsBackend *   backend,
            GVfsJobUnmount *job,
            GMountUnmountFlags flags,
            GMountSource *mount_source)
{
  GVfsBackendGdocs *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);

  /* TODO check if there is anything else to do*/
  g_object_unref (gdocs_backend->service);
  g_vfs_job_succeeded (G_VFS_JOB (job));
}

void 
do_move (GVfsBackend *backend, GVfsJobMove *job, const char *source, const char *destination, GFileCopyFlags flags,
		   GFileProgressCallback progress_callback, gpointer progress_callback_data)
{
	GDataDocumentsService *service = G_VFS_BACKEND_GDOCS (backend)->service;
	GVfsGDataFile *source_gdata_file, *destination_folder;
	GDataDocumentsEntry *new_entry;
	GError *error = NULL;
	GCancellable *cancellable;

	if (flags & G_FILE_COPY_BACKUP)
    {
      /* FIXME: implement? */
      g_vfs_job_failed (G_VFS_JOB (job),
                           G_IO_ERROR,
                           G_IO_ERROR_CANT_CREATE_BACKUP,
                           _("backups not supported yet"));
      return;
    }
	cancellable = G_VFS_JOB (job)->cancellable;

	source_gdata_file = g_vfs_gdata_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, NULL, NULL);

	if (error != NULL)
	{
		g_print ("Error making GVfsGDataFile\n");
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		if (source_gdata_file != NULL)
			g_object_unref (source_gdata_file);
		return;
	}

	if (strcmp (destination, "/") != 0)
	{
		destination_folder = g_vfs_gdata_file_new_folder_from_gvfs (G_VFS_BACKEND_GDOCS (backend), destination, cancellable, &error);

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
																	 GDATA_DOCUMENTS_ENTRY (g_vfs_gdata_file_get_gdata_entry (source_gdata_file)),
																	 GDATA_DOCUMENTS_FOLDER (g_vfs_gdata_file_get_gdata_entry (destination_folder)),
																	 cancellable, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			g_object_unref (source_gdata_file);
			g_object_unref (destination_folder);
			return;
		}
		if (!GDATA_IS_DOCUMENTS_ENTRY (new_entry))
		{
			g_vfs_job_failed (G_VFS_JOB (job),
					G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
					_("Error moving file %s"), source);
			if (destination_folder != NULL)
				g_object_unref (destination_folder);
			return;
		}

		g_object_unref (source_gdata_file);
		g_object_unref (destination_folder);
	}
	else
	{
		destination_folder = g_vfs_gdata_file_new_parent_from_gvfs (G_VFS_BACKEND_GDOCS (backend), source, cancellable, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			g_object_unref (source_gdata_file);
			g_object_unref (destination_folder);
			return;
		}
		new_entry = gdata_documents_service_remove_document_from_folder (service, 
															 GDATA_DOCUMENTS_ENTRY (g_vfs_gdata_file_get_gdata_entry (source_gdata_file)), 
															 GDATA_DOCUMENTS_FOLDER (g_vfs_gdata_file_get_gdata_entry (destination_folder)),
														     cancellable, &error);
		if (error != NULL)
		{
			g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
			g_error_free (error);
			g_object_unref (source_gdata_file);
			g_object_unref (destination_folder);
			return;
		}
		g_object_unref (source_gdata_file);
		g_object_unref (destination_folder);
	}

    g_vfs_job_succeeded (G_VFS_JOB (job));

}

static void
do_enumerate (GVfsBackend *backend, GVfsJobEnumerate *job, const char *dirname, GFileAttributeMatcher *matcher,
		 	  GFileQueryInfoFlags query_flags)
{
	GDataDocumentsService *service = G_VFS_BACKEND_GDOCS (backend)->service;
	GDataDocumentsFeed *documents_feed;
	GDataDocumentsQuery *query;
	GError *error = NULL;
	GList *i; /*GDataDocumentsEntry*/
	GFileInfo *info;

	query = gdata_documents_query_new (NULL);
	if (strcmp (dirname, "/") != 0)
	{
		gchar *folder_id = g_vfs_gdata_file_get_document_id_from_gvfs (dirname);
		g_print ("FolderId: %s\n", folder_id);

		/*Sets the query folder id*/
		gdata_documents_query_set_folder_id (query, folder_id);
		g_free (folder_id);
	}
	gdata_documents_query_set_show_folders (query, TRUE);

	documents_feed = gdata_documents_service_query_documents (service, query, NULL, NULL, NULL, &error);
	
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
	}

	for (i = gdata_feed_get_entries (GDATA_FEED (documents_feed)); i != NULL; i = i->next)
	{
		info = NULL;
		gchar *path, **strv_path;

		path = gdata_documents_entry_get_path (GDATA_DOCUMENTS_ENTRY (i->data));
		strv_path = g_strsplit (path, "/", 3);

		/*We check if the file is in the selected folder or in the root one*/
		if (g_strv_length (strv_path) < 4 || g_strcmp0 (path, "/") == 0)
		{
			GVfsGDataFile *file = g_vfs_gdata_file_new_from_gdata (backend, GDATA_ENTRY (i->data), &error);

			if (g_vfs_gdata_file_is_folder (file))
				g_print ("FOLDER ID: ==%s== ", gdata_documents_entry_get_document_id (GDATA_DOCUMENTS_ENTRY (g_vfs_gdata_file_get_gdata_entry (file))));

			if (error != NULL)
			{
				g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
				g_error_free (error);
				return;
			}

			info = g_vfs_gdata_file_get_info (file, info, matcher, error);
			if (error != NULL)
			{
				g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
				g_error_free (error);
				return;
			}
			g_vfs_job_enumerate_add_info (job, info);
		}
		g_free (path);
		g_strfreev (strv_path);
	}

	g_vfs_job_enumerate_done (job);
	g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_make_directory (GVfsBackend *backend,
                   GVfsJobMakeDirectory *job,
                   const char *filename)
{
	GDataDocumentsService *service = G_VFS_BACKEND_GDOCS (backend)->service;
	GDataDocumentsFolder *folder, *new_folder;
	GError *error = NULL;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	GDataCategory *folder_category;
	gchar *title = g_vfs_gdata_file_get_document_id_from_gvfs (filename);

	folder = gdata_documents_folder_new (NULL);
	folder_category = gdata_category_new ("http://schemas.google.com/docs/2007#folder", "http://schemas.google.com/g/2005#kind", "folder");
	gdata_entry_set_title (GDATA_ENTRY (folder), title);
	gdata_entry_add_category (GDATA_ENTRY (folder), folder_category);
	g_free (title);

	new_folder = GDATA_DOCUMENTS_FOLDER (gdata_documents_service_upload_document (service, folder, NULL, NULL, cancellable, &error));
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
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	GError *error = NULL;

	GVfsGDataFile *file = g_vfs_gdata_file_new_from_gvfs (backend, filename, cancellable, &error);

	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	if (g_vfs_gdata_file_is_folder (file))
	{
        g_vfs_job_failed (G_VFS_JOB (job),
                          G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY,
                          _("Can't open directory"));
		return;
	}
}

static void
do_delete (GVfsBackend *backend,
	   GVfsJobDelete *job,
	   const char *filename)
{
	GError *error=NULL;
	GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
	GDataService *service = G_VFS_BACKEND_GDOCS (backend)->service;

	g_print ("BAckend Error adress: %p\n", error);
	g_print ("BAckend &Error adress: %p\n", &error);
	GVfsGDataFile *file = g_vfs_gdata_file_new_from_gvfs (backend, filename, cancellable, &error);

	/*TODO use the error as test*/
	if (file == NULL)
	{
		g_print ("BAckend Error adress: %p\n", error);
		g_print ("BAckend &Error adress: %p\n", &error);
		g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
								  _("%s not found"), filename);
		return;
	}
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

	GVfsGDataFile *file = g_vfs_gdata_file_new_from_gdata (backend, G_VFS_GDATA_FILE  (_handle), &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), &error);
		g_error_free (error);
		if (file != NULL)
			g_object_unref (file);
		return;
	}

	g_vfs_gdata_file_get_info (file, info, matcher, &error);

	if (error == NULL)
	{
		g_vfs_job_succeeded (G_VFS_JOB (job));
		g_object_unref (file);
	else
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), &error);
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
	GVfsGDataFile *file;

	if (make_backup)
	{
		/* FIXME: implement! */
		g_set_error_literal (&task.error,
				G_IO_ERROR,
				G_IO_ERROR_CANT_CREATE_BACKUP,
				_("backups not supported yet"));
		g_vfs_ftp_task_done (&task);
		return;
	}

	file = g_vfs_ftp_file_new_from_gvfs (backend, filename);
	if (g_strcmp0 (etag, gdata_entry_get_etag (g_vfs_gdata_file_get_gdata_entry (file))) != 0)
	{
		g_free (current_etag);
		g_set_error_literal (&error,
				G_IO_ERROR,
				G_IO_ERROR_WRONG_ETAG,
				_("The file was externally modified"));
	}

		g_vfs_ftp_dir_cache_purge_file (ftp->dir_cache, file);
		g_vfs_ftp_file_free (file);

		g_vfs_ftp_task_done (&task);
}


static void
do_write (GVfsBackend *backend,
	  GVfsJobWrite *job,
	  GVfsBackendHandle _handle,
	  char *buffer,
	  gsize buffer_size)
{
  GVfsBackendSmb *op_backend = G_VFS_BACKEND_SMB (backend);
  SmbWriteHandle *handle = _handle;
  ssize_t res;
  smbc_write_fn smbc_write;

  smbc_write = smbc_getFunctionWrite (op_backend->smb_context);
  res = smbc_write (op_backend->smb_context, handle->file,
					buffer, buffer_size);
  if (res == -1)
    g_vfs_job_failed_from_errno (G_VFS_JOB (job), errno);
  else
    {
      g_vfs_job_write_set_written_size (job, res);
      g_vfs_job_succeeded (G_VFS_JOB (job));
    }
}

static void
do_seek_on_write (GVfsBackend *backend,
		  GVfsJobSeekWrite *job,
		  GVfsBackendHandle _handle,
		  goffset    offset,
		  GSeekType  type)
{
  GVfsBackendSmb *op_backend = G_VFS_BACKEND_SMB (backend);
  SmbWriteHandle *handle = _handle;
  int whence;
  off_t res;
  smbc_lseek_fn smbc_lseek;

  switch (type)
    {
    case G_SEEK_SET:
      whence = SEEK_SET;
      break;
    case G_SEEK_CUR:
      whence = SEEK_CUR;
      break;
    case G_SEEK_END:
      whence = SEEK_END;
      break;
    default:
      g_vfs_job_failed (G_VFS_JOB (job),
			G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Unsupported seek type"));
      return;
    }

  smbc_lseek = smbc_getFunctionLseek (op_backend->smb_context);
  res = smbc_lseek (op_backend->smb_context, handle->file, offset, whence);

  if (res == (off_t)-1)
    g_vfs_job_failed_from_errno (G_VFS_JOB (job), errno);
  else
    {
      g_vfs_job_seek_write_set_offset (job, res);
      g_vfs_job_succeeded (G_VFS_JOB (job));
    }

  return;
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
	backend_class->open_for_read = do_open_for_read;
	backend_class->make_directory = do_make_directory;
	backend_class->enumerate = do_enumerate;
	backend_class->move = do_move;
	backend_class->unmount = do_unmount;
	backend_class->delete = do_delete;
	backend_class->replace = do_replace;
}
