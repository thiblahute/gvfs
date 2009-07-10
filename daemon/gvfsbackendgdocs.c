/* GIO - GLib Input, Output and Streaming Library
 * 
 * Copyright (C) 2008 Red Hat, Inc.
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

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gvfsbackendgdocs.h"
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
	gchar *username, *ask_user, *ask_password, *prompt=NULL, *display_name;
	gboolean aborted, retval, save_password=FALSE;
	GPasswordSave password_save = G_PASSWORD_SAVE_NEVER;
	GAskPasswordFlags flags;
	GError *error;
	guint mount_try;

	/*Get usename*/
	username = g_strdup (g_mount_spec_get (mount_spec, "user"));
	

	/*Set the password asking flags.*/
	if (!username)
	    flags = G_ASK_PASSWORD_NEED_USERNAME;
	else
	{
		flags =  G_ASK_PASSWORD_NEED_PASSWORD;
		if (g_vfs_keyring_is_available ())
			flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;
	}

	/*Check if the password as already been saved for the user*/
	if (username)
	{
		prompt = g_strdup_printf (_("Enter %s's google documents password"), username);
		if (!g_vfs_keyring_lookup_password (username, NULL, NULL, "gdata", NULL, NULL, 0, &ask_user, NULL, &ask_password))
		{
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
			save_password = TRUE;
			ask_user = username;
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
				return;
			}
			save_password = TRUE;
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
			g_vfs_backend_set_mount_spec (backend, gdocs_mount_spec);
			g_mount_spec_unref (gdocs_mount_spec);

			display_name = g_strdup_printf (_("%s's google documents"), ask_user);
			g_vfs_backend_set_display_name (backend, display_name);
			g_free (display_name);
			g_vfs_backend_set_icon_name (backend, "folder-remote");
			break;
		}
		else
		{
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
				return;
			}
			save_password = TRUE;
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
		g_print ("===Connected");
		g_vfs_job_succeeded (G_VFS_JOB (job));
	}
}

static void
do_unmount (GVfsBackend *   backend,
            GVfsJobUnmount *job)
{
  GVfsBackendGdocs *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);

  /* TODO check if there is anything else to do*/
  g_object_unref (gdocs_backend->service);
  g_vfs_job_succeeded (G_VFS_JOB (job));
}

void do_move (GVfsBackend *backend, GVfsJobMove *job, const char *source, const char *destination, GFileCopyFlags flags,
		   GFileProgressCallback progress_callback, gpointer progress_callback_data)
{
	GVfsBackendGdocs *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
	GDataDocumentsQuery *folder_query, *entry_query;
	GDataDocumentsEntry *source_entry;
	GDataDocumentsFolder *destination_folder;
	GError *error;
	GCancellable *cancellable;

	cancellable = G_VFS_JOB (job)->cancellable;
	entry_query = gdata_documents_service_new (NULL);
	gdata_query_set_entry_id (GDATA_QUERY (entry_query), source);
	source_entry = gdata_documents_service_query_documents (gdocs_backend->service, entry_query, cancellable,
															NULL, NULL, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	/*TODO check if it the entry is moved to the root directory and use gdata_documents_service_remove_document_from_folder function*/
	folder_query = gdata_documents_query_new (NULL);
	gdata_query_set_entry_id (GDATA_QUERY (folder_query), destination);
	folder_query = gdata_documents_service_query_documents (gdocs_backend->service, entry_query, cancellable,
															NULL, NULL, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}
}

static void
do_enumerate (GVfsBackend *backend, GVfsJobEnumerate *job, const char *dirname, GFileAttributeMatcher *matcher,
		 	  GFileQueryInfoFlags query_flags)
{
	GDataDocumenService *service = G_VFS_BACKEND_GDOCS (backend)->service;
	GDataDocumentsFeed *documents_feed;
	GDataDocumentsQuery *query;
	GError *error;
	GList *i; /*GDataDocumentsEntry*/
	GList *files; /*GFileInfo*/
	GFileInfo *info;

	query = gdata_documents_query_new (NULL);
	if (strcmp (dirname, "/") != 0)
	{
		/*Gets the documents folder (the last of the dirname*/
		gchar **folders_id_array, folder_id;
		folders_id_array = g_strsplit (dirname, ,"/", 0);
		while (1)
		{
			g_free (folder_id);
			folder_id = folders_id_array[j];
			if (folders_id_array[j++] == NULL)
				break;
		}
		/*Sets the folder id*/
		gdata_documents_query_set_folder_id (query, folder_id);
		g_free (folder_id);
		g_free (folders_id_array);
	}
	gdata_documents_query_set_show_folders (query, TRUE);

	documents_feed = gdata_documents_service_query_documents (service, query, G_VFS_JOB (job)->cancellable, NULL, NULL, &error);
	if (error != NULL)
	{
		g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
		g_error_free (error);
		return;
	}

	for (i = gdata_feed_get_entries (documents_feed); i != NULL; i = i->new_path)
	{
	    if (matcher == NULL || g_file_attribute_matcher_matches_only (matcher, G_FILE_ATTRIBUTE_STANDARD_NAME))
		{
			gchar *filename = gdata_documents_entry_get_document_id (GDATA_DOCUMENTS_ENTRY (i->data));
			gchar *file_display_name = gdata_entry_get_title (GDATA_ENTRY (i->data));
			info = g_file_info_new ();
			g_file_info_set_name (info, filename);
			g_file_info_set_display_name (info, display_name);
			files = g_list_prepend (files, info);
		}
	}
    
	if (files)
	{
	  files = g_list_reverse (files);
	  g_vfs_job_enumerate_add_infos (job, files);
	  g_list_foreach (files, (GFunc)g_object_unref, NULL);
	  g_list_free (files);
	}
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
	backend_class->enumerate = do_enumerate;
	backend_class->enumerate = do_enumerate;
	backend_class->move = do_move;
	backend_class->unmount = do_unmount;
	/*  backend_class->try_open_for_read = try_open_for_read;
		backend_class->try_read          = try_read;
		backend_class->try_seek_on_read  = try_seek_on_read;
		backend_class->try_close_read    = try_close_read;
		backend_class->try_query_info    = try_query_info;
	*/
}
