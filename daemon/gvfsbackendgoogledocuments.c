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

#include "gvfsbackendgoogledocuments.h"
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

#include "soup-input-stream.h"


G_DEFINE_TYPE (GVfsBackendGoogleDocuments, g_vfs_backend_google_documents, G_VFS_TYPE_BACKEND)

static void
g_vfs_backend_google_documents_finalize (GObject *object)
{
  GVfsBackendGoogleDocuments *backend;

  backend = G_VFS_BACKEND_GOOGLE_DOCUMENTS (object);
}

static void
g_vfs_backend_google_documents_dispose (GObject *object)
{
  GVfsBackendGoogleDocuments *backend;

  backend = G_VFS_BACKEND_GOOGLE_DOCUMENTS (object);
  if (backend->service != NULL )
	  g_object_unref (backend->service);
}

#define DEBUG_MAX_BODY_SIZE (100 * 1024 * 1024)

static void
g_vfs_backend_google_documents_init (GVfsBackendGoogleDocuments *backend)
{
  const char         *debug;
  backend->service =  gdata_documents_service_new (CLIENT_ID);

  /* Gnome proxy configuration is handle by libgdata*/

  /* Logging */
  debug = g_getenv ("GVFS_GOOGLE_DOCUMENTS_DEBUG");
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
	GVfsBackendGoogleDocuments *google_documents = G_VFS_BACKEND_GOOGLE_DOCUMENTS (backend);
	gchar *username, *initial_user, *password, *prompt, display_name;
	gboolean aborted;
	GPasswordSave password_save = G_PASSWORD_SAVE_NEVER;
	GError *error = NULL;


	username = g_strdup (g_mount_spec_get (mount_spec, "user"));

	if (!g_vfs_keyring_lookup_password (gdata_service_get_username (GDATA_SERVICE (google_documents->service)), NULL,
																	NULL, "gdata", NULL, NULL, NULL, &username, NULL,
																   	&password))
	{
		GAskPasswordFlags flags;
		prompt = g_strdup_printf (_("Enter password to access %s's google documents."), username);

		flags = G_ASK_PASSWORD_NEED_PASSWORD;
			
		if (g_vfs_keyring_is_available ())
			flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;

		/* TODO: See how to handle several password askings*/
		if (!g_mount_source_ask_password (mount_source, prompt, username, NULL, flags, &aborted, &password, &username,	NULL, FALSE,
										  &password_save) || aborted)
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, _("Password dialog cancelled")); /*TODO check what to do here*/
    }
     
    if (gdata_service_authenticate (google_documents->service, username, password, NULL, &error) == TRUE)
	{
		g_free (username);
		g_free (password);
	}

	if (prompt && error == NULL)
	{
      /* a prompt was created and we could connect on the google documents service, so we have to save the password */
      g_vfs_keyring_save_password (gdata_service_get_username ( GDATA_SERVICE (google_documents->service)), NULL, NULL, "gdata", NULL, NULL, NULL,
									gdata_service_get_password (google_documents->service), password_save);
      g_free (prompt);
    }

	mount_spec = g_mount_spec_new ("google-documents");
	display_name = g_strdup_printf (_("%s on google documents"), gdata_service_get_username (GDATA_SERVICE (google_documents->service)));

	g_vfs_backend_set_mount_spec (backend, mount_spec);
	g_mount_spec_unref (mount_spec);

	g_vfs_backend_set_display_name (backend, display_name);
	g_free (display_name);
	g_vfs_backend_set_icon_name (backend, "folder-remote");

}

static void
g_vfs_backend_google_documents_class_init (GVfsBackendGoogleDocumentsClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GVfsBackendClass *backend_class;
  
  gobject_class->finalize  = g_vfs_backend_google_documents_finalize;
  gobject_class->dispose  = g_vfs_backend_google_documents_dispose;

  backend_class = G_VFS_BACKEND_CLASS (klass); 

  backend_class->mount         = do_mount;
/*  backend_class->try_open_for_read = try_open_for_read;
  backend_class->try_read          = try_read;
  backend_class->try_seek_on_read  = try_seek_on_read;
  backend_class->try_close_read    = try_close_read;
  backend_class->try_query_info    = try_query_info;
*/
}
