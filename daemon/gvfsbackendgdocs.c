/*
 *  GIO - GLib Input, Output and Streaming Library
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
#include "gvfsjobmove.h"
#include "gvfsdaemonprotocol.h"
#include "gvfskeyring.h"


void g_vfs_backend_gdocs_remove_gdocs_file  (GVfsBackendGdocs   *backend,
                                             GVfsGDocsFile      *file);
void g_vfs_backend_gdocs_add_gdocs_file     (GVfsBackendGdocs   *backend,
                                             GVfsGDocsFile      *file);


/*Gdata client ID, we should ask for a new one for GVFS*/
#define CLIENT_ID "ytapi-GNOME-libgdata-444fubtt-0"

G_DEFINE_TYPE (GVfsBackendGdocs, g_vfs_backend_gdocs, G_VFS_TYPE_BACKEND)
#define G_VFS_BACKEND_GDOCS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
                G_VFS_TYPE_BACKEND_GDOCS, GVfsBackendGdocsPrivate))

/*Private structure*/
struct _GVfsBackendGdocsPrivate {
    GDataDocumentsService	*service;
    GHashTable				*entries;
};

/* ********************************************************************************** */
/* Private functions */
void
g_vfs_backend_gdocs_add_gdocs_file (GVfsBackendGdocs   *backend,
                                    GVfsGDocsFile      *file)
{
    const gchar *entry_id;
    GDataDocumentsEntry *entry;

    GHashTable *entries = backend->priv->entries;
    
    g_return_if_fail (G_VFS_IS_GDOCS_FILE (file));

    if (g_vfs_gdocs_file_is_root (file))
        entry_id = "/";
    else
      { 
        entry = g_vfs_gdocs_file_get_document_entry (file);
        entry_id =  gdata_documents_entry_get_document_id (entry);
      }

    g_hash_table_insert (entries, (gchar*) entry_id, file);
}

void
g_vfs_backend_gdocs_remove_gdocs_file  (GVfsBackendGdocs   *backend,
                                        GVfsGDocsFile      *file)
{
    const gchar *entry_id;
    GDataDocumentsEntry *entry;

    g_return_if_fail (G_VFS_IS_GDOCS_FILE (file));

    GHashTable *entries = backend->priv->entries;
    
    if (g_vfs_gdocs_file_is_root (file))
        entry_id = "/";
    else
      { 
        entry = g_vfs_gdocs_file_get_document_entry (file);
        entry_id =  gdata_documents_entry_get_document_id (entry);
      }

    g_hash_table_remove (entries, entry_id);
}
/* ********************************************************************************** */
/* public utility functions */

GVfsGDocsFile *
g_vfs_backend_gdocs_look_up_file (const GVfsBackendGdocs *backend, const gchar *entry_id)
{
    GHashTable *entries = backend->priv->entries; 

    g_return_val_if_fail (G_VFS_IS_BACKEND_GDOCS (backend), NULL);
    g_return_val_if_fail (entry_id != NULL, NULL);

    return g_hash_table_lookup (entries, entry_id);
}

gint
g_vfs_backend_gdocs_count_files (const GVfsBackendGdocs *backend)
{
    GHashTable *entries = backend->priv->entries; 

    g_return_val_if_fail (G_VFS_IS_BACKEND_GDOCS (backend), -1);

    return g_hash_table_size (entries);
}

GDataDocumentsService *
g_vfs_backend_gdocs_get_service (const GVfsBackendGdocs *backend)
{
    g_return_val_if_fail (G_VFS_IS_BACKEND_GDOCS (backend), NULL);

    return backend->priv->service;
}

void
g_vfs_backend_gdocs_rebuild_entries (GVfsBackendGdocs   *backend,
                                     GCancellable       *cancellable,
                                     GError             **error)
{
    GList                   *list_entries;
    GDataDocumentsQuery     *query;
    GDataDocumentsFeed      *documents_feed;
    GVfsGDocsFile           *root_file;

    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (backend);

    /*Get all entries (as feed) on the server*/
    query = gdata_documents_query_new (NULL);
    gdata_documents_query_set_show_folders (query, TRUE);
    documents_feed = gdata_documents_service_query_documents (service,
                                                              query,
                                                              cancellable,
                                                              NULL,
                                                              NULL,
                                                              error);
    g_object_unref (query);

    if (*error != NULL)
      {
        if (documents_feed != NULL)
            g_object_unref (documents_feed);
        return;
      }

    for (list_entries = gdata_feed_get_entries (GDATA_FEED (documents_feed));
         list_entries != NULL; 
         list_entries = list_entries->next)
      {
        GDataDocumentsEntry *document_entry;
        GVfsGDocsFile       *file;

        document_entry = GDATA_DOCUMENTS_ENTRY (list_entries->data);
        file = g_vfs_gdocs_file_new_from_document_entry (backend,
                                                         document_entry,
                                                         NULL);
        g_vfs_backend_gdocs_add_gdocs_file (backend, file); 
      }
    g_object_unref (documents_feed);

    /*We add the root folder so we don't have problem querying it afterward*/
    root_file = g_vfs_gdocs_file_new_from_document_entry (backend,
                                                          NULL,
                                                          NULL);

    g_vfs_backend_gdocs_add_gdocs_file (backend, root_file);
}


/* ********************************************************************************** */
/* virtual functions overrides */
static void
do_mount (GVfsBackend   *backend,
          GVfsJobMount  *job,
          GMountSpec    *mount_spec,
          GMountSource  *mount_source,
          gboolean      is_automount)
{
    gchar                   *ask_user, *ask_password, *display_name;
    const gchar             *username, *host;
    gboolean                aborted, retval;
    GMountSpec              *gdocs_mount_spec;
    GAskPasswordFlags       flags;

    gchar                   *prompt = NULL;
    gchar                   *full_username = NULL;
    GError                  *error = NULL;
    gboolean                show_dialog = TRUE;
    GPasswordSave           password_save_flags = G_PASSWORD_SAVE_NEVER;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    /*Get usename*/
    username = g_mount_spec_get (mount_spec, "user");
    host = g_mount_spec_get (mount_spec, "host");

    /* We want an uri like gdocs://a_username to work
     * We don't care about gdocs://host since it should never be used
     * The host is actually the part after the '@' of the user's email address
     * The default host is gmail.com since it's almost the only used
     **/
    if (host == NULL)
        host = "gmail.com";
    else if (username == NULL)
      {
        username = host;
        host = "gmail.com";
      }

    /* Set the password asking flags.*/
    flags =  G_ASK_PASSWORD_NEED_PASSWORD;
    if (g_vfs_keyring_is_available ())
        flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;

    if (username != NULL)
      {
        /* Check if the password as already been saved for the user,
         * we set the protocol as gdata can be shared by variours google services
         */
        if (!g_vfs_keyring_lookup_password (username,
                                            host,
                                            NULL,
                                            "gdata",
                                            NULL,
                                            NULL,
                                            0,
                                            &ask_user,
                                            NULL,
                                            &ask_password))
            prompt = g_strdup_printf (_("Enter %s@%s's google documents password"),
                                      username,
                                      host);
        else
            show_dialog = FALSE;

      }
    else
      {
        prompt =  g_strdup ("Enter username and password to access google documents.");
        flags |= G_ASK_PASSWORD_NEED_USERNAME;
      }



    /* We build the complete adress with which we are going to connect*/

    /*Connect to the server*/
    while (TRUE)
      {
        /* We first check that we need to show the ask password dialog */
        if (show_dialog == TRUE && (!g_mount_source_ask_password (mount_source,
                                                                  prompt,
                                                                  username,
                                                                  NULL,
                                                                  flags,
                                                                  &aborted,
                                                                  &ask_password,
                                                                  &ask_user,
                                                                  NULL,
                                                                  FALSE,
                                                                  &password_save_flags)
                                   || aborted))
          {
            if (aborted)
              g_vfs_job_failed (G_VFS_JOB (job),
                                G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED,
                                _("Password dialog cancelled"));
            else
              g_vfs_job_failed (G_VFS_JOB (job),
                                G_IO_ERROR,
                                G_IO_ERROR_FAILED,
                                _("Password access issue"));
            g_free (ask_user);
            g_free (ask_password);
            g_free (prompt);

            return;
          }

        /* If it's the first loop, we create the full username 
         * (which is the user email adresse
         **/
        if (full_username == NULL)
          {
            if (ask_user == NULL)
              {
                full_username = g_strdup_printf ("%s@%s", username, host);
                ask_user = g_strdup (username);
              }
            else
              full_username = g_strdup_printf ("%s@%s", ask_user, host);
          }

        g_message ("-> Username: %s\n", full_username);
        g_message ("password: ***\n");
        
        retval = gdata_service_authenticate (GDATA_SERVICE (service),
                                             full_username,
                                             ask_password,
                                             NULL,
                                             &error);
        if (retval == TRUE)
          {
            /* Save the password, we use "gdata" as protocol  name since we could 
             * use the same protocol later for other google services (as picassaweb)
             * which would have the same password
             **/
            g_vfs_keyring_save_password (username,
                                         host,
                                         NULL,
                                         "gdata",
                                         NULL,
                                         NULL,
                                         0,
                                         ask_password,
                                         password_save_flags);

            /*Mount it*/
            gdocs_mount_spec= g_mount_spec_new ("gdocs");
            g_mount_spec_set (gdocs_mount_spec, "user", ask_user);
            g_mount_spec_set (gdocs_mount_spec, "host", host);

            display_name = g_strdup_printf ("%s's google documents", full_username);
            g_vfs_backend_set_display_name (backend, display_name);
            g_free (display_name);

            g_vfs_backend_set_mount_spec (backend, gdocs_mount_spec);
            g_mount_spec_unref (gdocs_mount_spec);
            g_vfs_backend_set_icon_name (backend, "folder-remote");
            break;
          }

        flags = G_ASK_PASSWORD_NEED_PASSWORD;
        if (g_vfs_keyring_is_available ())
            flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;

        prompt = g_strdup_printf (_("Wrong password, enter %s's password again."),
                                  full_username);
        show_dialog = TRUE;
        g_clear_error (&error);
      }

    g_free (prompt);
    g_free (full_username);
    g_free (ask_password);
    g_free (ask_user);

    g_vfs_job_succeeded (G_VFS_JOB (job));
    g_message ("===Connected\n");
}

static void
do_move (GVfsBackend            *backend,
         GVfsJobMove            *job,
         const char             *source,
         const char             *destination,
         GFileCopyFlags         flags,
         GFileProgressCallback  progress_callback,
         gpointer               progress_callback_data)
{
    GDataDocumentsEntry     *entry, *new_entry, *renamed_document;
    GDataDocumentsFolder    *folder_entry;
    GVfsGDocsFile           *source_file, *destination_folder, *containing_folder;
    gchar                   *destination_parent_id, *source_parent_id;
    gchar                   *destination_id, *source_id;

    gboolean                need_rename = FALSE;
    gboolean                move = TRUE;
    gboolean                move_to_root = FALSE;
    GError                  *error = NULL;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    new_entry = NULL;
    destination_folder = NULL;

    if (flags & G_FILE_COPY_BACKUP)
      {
        /* TODO, Implement it*/
        g_vfs_job_failed (G_VFS_JOB (job),
                    G_IO_ERROR,
                    G_IO_ERROR_CANT_CREATE_BACKUP,
                    _("backups not supported yet"));
        return;
      }

    source_file = g_vfs_gdocs_file_new_from_gvfs (gdocs_backend,
                                                  source,
                                                  cancellable,
                                                  &error);
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

    /* If we move a file to root without renaming it
     * We check that the file isn't in root */
    if (g_strcmp0 (source_id, destination_id) == 0
                && g_strcmp0 (destination_parent_id, "/") == 0
                && g_strcmp0 (source_parent_id, "/") != 0)
      move_to_root = TRUE;

    g_message ("Source id: %s, destination ID: %s", source_id, destination_id);

    /* We check if we need to rename, if we need, the destination folder should be
     * the parent one */
    if (g_strcmp0 (source_id, destination_id) != 0)
      need_rename = TRUE;
    else
      destination_folder = g_vfs_gdocs_file_new_folder_from_gvfs (gdocs_backend,
                                                                  destination_parent_id,
                                                                  cancellable,
                                                                  &error);
    g_free (source_id);
    g_free (destination_id);

    if (!move_to_root)
      {
        if (destination_folder == NULL)
          destination_folder = g_vfs_gdocs_file_new_folder_from_gvfs (gdocs_backend,
                                                                      destination,
                                                                      cancellable,
                                                                      &error);

        /* If the destination is not a folder and the parent of the destination
         *  is the root, we rename the source file*/
        if (g_error_matches (error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_DIRECTORY)
                    && g_strcmp0 (destination_parent_id, "/") == 0
                    && g_strcmp0 (source_parent_id, "/") != 0)
          {
            g_clear_error (&error);
            move = FALSE;
            move_to_root = TRUE;
          }
      }

    if (!move_to_root)
      {
        GDataDocumentsEntry *tmp_entry;
        if (error != NULL)
          {
            g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
            g_error_free (error);
            if (destination_folder != NULL)
              g_object_unref (destination_folder);
            return;
          }

        /*Move the document on the server*/
        entry = g_vfs_gdocs_file_get_document_entry (source_file);
        tmp_entry = g_vfs_gdocs_file_get_document_entry (destination_folder);
        folder_entry = GDATA_DOCUMENTS_FOLDER (tmp_entry);

        g_message ("destination_folder: %s",
                 gdata_documents_entry_get_document_id (entry));
        new_entry = gdata_documents_service_move_document_to_folder (service,
                                                                     entry,
                                                                     folder_entry,
                                                                     cancellable,
                                                                     &error);
        if (error != NULL)
          {
            g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
            g_error_free (error);
            g_object_unref (destination_folder);
            return;
          }

      }
    g_free (destination_parent_id);
    if (destination_folder != NULL) /* TODO check what is wrong with it*/
      g_object_unref (destination_folder);

    if (move_to_root)
      {
        GDataDocumentsEntry *tmp_entry;
        g_message ("Is moving to root");
        /* we need to check for the error that could have
         * happend building the destination_folder*/
        containing_folder = g_vfs_gdocs_file_new_parent_from_gvfs (gdocs_backend,
                                                                   source,
                                                                   cancellable,
                                                                   &error);
        if (error != NULL)
          {
            g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
            g_error_free (error);
            return;
          }

        entry = g_vfs_gdocs_file_get_document_entry (source_file);
        tmp_entry = g_vfs_gdocs_file_get_document_entry (containing_folder);
        folder_entry = GDATA_DOCUMENTS_FOLDER (tmp_entry);
                    
        g_message ("Moving %s out of %s",
                    gdata_documents_entry_get_document_id (entry),
                    gdata_documents_entry_get_document_id (tmp_entry));

        new_entry = gdata_documents_service_remove_document_from_folder (service,
                                                                         entry,
                                                                         folder_entry,
                                                                         cancellable,
                                                                         &error);
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
        gdata_entry_set_title (GDATA_ENTRY (new_entry), new_filename);
        g_free (new_filename);

        renamed_document = gdata_documents_service_update_document (service,
                                                                    new_entry,
                                                                    NULL,
                                                                    NULL,
                                                                    &error);
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
do_set_display_name (GVfsBackend            *backend,
                     GVfsJobSetDisplayName  *job,
                     const char             *filename,
                     const char             *display_name)
{
    GVfsGDocsFile           *file;
    gchar                   *new_path, *dirname;
    GDataDocumentsEntry     *entry, *renamed_entry;

    GError                  *error = NULL;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    file = g_vfs_gdocs_file_new_from_gvfs (gdocs_backend,
                                           filename,
                                           cancellable,
                                           &error);
    if (g_vfs_gdocs_file_is_root (file))
        g_vfs_job_failed (G_VFS_JOB (job),
                          G_IO_ERROR,
                          G_IO_ERROR_NOT_SUPPORTED,
                          _("Can't rename the root directory"));
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    entry = g_vfs_gdocs_file_get_document_entry (file);
    gdata_entry_set_title (GDATA_ENTRY (entry), display_name);

    renamed_entry = gdata_documents_service_update_document (service,
                                                             entry,
                                                             NULL,
                                                             NULL,
                                                             &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        if (renamed_entry != NULL)
          g_object_unref (renamed_entry);
        return;
      }

    dirname = g_path_get_dirname (filename);
    new_path = g_build_filename (dirname,
                                 gdata_documents_entry_get_document_id (renamed_entry),
                                 NULL);
    g_free (dirname);
    g_object_unref (renamed_entry);
    g_vfs_job_set_display_name_set_new_path (job, new_path);
    g_vfs_job_succeeded (G_VFS_JOB (job));
    g_free (new_path);
}

static void
do_enumerate (GVfsBackend           *backend,
          GVfsJobEnumerate      *job,
          const char            *dirname,
          GFileAttributeMatcher *matcher,
          GFileQueryInfoFlags   query_flags)
{
    gchar                   *folder_id ;
    GList                   *list_entries; /*GDataDocumentsEntry*/
    GFileInfo               *info;
    GDataDocumentsFeed      *documents_feed;
    GDataDocumentsQuery     *query ;

    gboolean                in_folder =  FALSE;
    GError                  *error = NULL;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    /*Get documents properties*/
    query = gdata_documents_query_new (NULL);
    folder_id = g_path_get_basename (dirname);
    if (g_strcmp0 (dirname, "/") != 0)
      {
        /*Sets the query folder id*/
        gdata_documents_query_set_folder_id (query, folder_id);
        in_folder = TRUE;
        g_message ("Folder ID: %s\n", folder_id);
      }

    gdata_documents_query_set_show_folders (query, TRUE);
    documents_feed = gdata_documents_service_query_documents (service,
                                                              query,
                                                              cancellable,
                                                              NULL,
                                                              NULL,
                                                              &error);
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
    for (list_entries = gdata_feed_get_entries (GDATA_FEED (documents_feed));
         list_entries != NULL;
         list_entries = list_entries->next)
      {
        gchar *path, *parent_id;

        info = NULL;
        path = gdata_documents_entry_get_path (GDATA_DOCUMENTS_ENTRY (list_entries->data));
        parent_id = g_path_get_parent_basename (path);

        //g_message ("Path: %s folder ID %s, parent_id: %s", folder_id, parent_id);
        /*We check that the file is in the selected folder (not in a child of it)*/
        if (g_strcmp0 (folder_id, parent_id) == 0 || in_folder)
          {
            GVfsGDocsFile *file;

            GDataDocumentsEntry *tmp_entry = GDATA_DOCUMENTS_ENTRY (list_entries->data);

            file = g_vfs_gdocs_file_new_from_document_entry (gdocs_backend,
                                                             tmp_entry,
                                                             &error);
            if (error != NULL)
              {
                g_free (path);
                g_free (parent_id);
                g_clear_error (&error);
                if (file != NULL)
                  g_object_unref (file);
                continue;
              }

            /*We keep the GHashTable::entries up to date*/
            g_vfs_backend_gdocs_add_gdocs_file (gdocs_backend, file);

            info = g_vfs_gdocs_file_get_info (file, info, matcher, &error);
            if (error != NULL)
              {
                g_free (path);
                g_free (parent_id);
                g_clear_error (&error);
                g_object_unref (file);
                continue;
              }

            g_vfs_job_enumerate_add_info (job, info);
          }
        g_free (path);
        g_free (parent_id);

      }

    g_object_unref (documents_feed);
    g_free (folder_id);
    g_vfs_job_enumerate_done (job);
}

static void
do_make_directory (GVfsBackend          *backend,
               GVfsJobMakeDirectory *job,
               const char           *filename)
{
    gchar                   *title;
    GDataCategory           *folder_category;
    GDataDocumentsFolder    *folder, *tmp_folder_entry;
    GDataDocumentsEntry     *tmp_entry, *new_folder, *entry;
    GVfsGDocsFile           *destination_folder, *file;

    GError                  *error = NULL;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    title = g_path_get_basename (filename);
    if (g_strcmp0 (title, "/") == 0)
      {
        g_vfs_job_failed (G_VFS_JOB (job),
                    G_IO_ERROR,
                    G_IO_ERROR_NOT_SUPPORTED,
                    _("Can't create a root directory"));
        g_free (title);
        return;

      }

    folder = gdata_documents_folder_new (NULL);
    folder_category = gdata_category_new ("http://schemas.google.com/docs/2007#folder",
                                          "http://schemas.google.com/g/2005#kind",
                                          "folder");
    gdata_entry_add_category (GDATA_ENTRY (folder), folder_category);
    gdata_entry_set_title (GDATA_ENTRY (folder), title);
    g_object_unref (folder_category);
    g_free (title);

    destination_folder = g_vfs_gdocs_file_new_parent_from_gvfs (gdocs_backend,
                                                                filename,
                                                                cancellable,
                                                                &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        g_object_unref (folder);
        if (destination_folder != NULL)
          g_object_unref (destination_folder);
        return;
      }

    entry = g_vfs_gdocs_file_get_document_entry (destination_folder);
    tmp_folder_entry = GDATA_DOCUMENTS_FOLDER (entry);
    tmp_entry = GDATA_DOCUMENTS_ENTRY (folder);
    new_folder = gdata_documents_service_upload_document (service,
                                                          tmp_entry,                                                
                                                          NULL,
                                                          tmp_folder_entry,
                                                          cancellable,
                                                          &error);
    g_object_unref (folder);
    g_object_unref (destination_folder);

    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        if (new_folder != NULL)
            g_object_unref (new_folder);
        return;
      }

    /* We keep the #GHashTable::entries property up to date */
    file = g_vfs_gdocs_file_new_from_document_entry (gdocs_backend, new_folder, &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        if (new_folder != NULL)
            g_object_unref (new_folder);
        return;
      }
    g_vfs_backend_gdocs_add_gdocs_file (gdocs_backend, file);

    if (new_folder != NULL)
        g_object_unref (new_folder);

    g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_open_for_read (GVfsBackend           *backend,
                  GVfsJobOpenForRead    *job,
                  const char            *filename)
{
    gchar               *uri;
    GVfsGDocsFile       *file;
    GInputStream        *stream;

    GError                  *error = NULL;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    g_message ("OPEN READ: %s\n", filename);

    file = g_vfs_gdocs_file_new_from_gvfs (gdocs_backend,
                                           filename,
                                           cancellable,
                                           &error);
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

    /*Won't start downnloading until g_input_stream_read_* is called*/
    stream = gdata_download_stream_new (GDATA_SERVICE (service), uri);
    g_free (uri);

    g_vfs_job_open_for_read_set_handle (job, stream);
    g_vfs_job_open_for_read_set_can_seek (job, FALSE);
    g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
read_ready (GObject      *source_object,
            GAsyncResult *result,
            gpointer      user_data)
{
    GInputStream *stream;
    GVfsJob      *job;
    GError       *error;
    gssize        nread;

    stream = G_INPUT_STREAM (source_object); 
    error  = NULL;
    job    = G_VFS_JOB (user_data);

    nread = g_input_stream_read_finish (stream, result, &error);

    g_print ("read ready: %d\n", nread);

    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    g_vfs_job_read_set_size (G_VFS_JOB_READ (job), nread);
    g_vfs_job_succeeded (job);
}

static gboolean
try_read (GVfsBackend       *backend,
          GVfsJobRead       *job,
          GVfsBackendHandle handle,
          char              *buffer,
          gsize             bytes_requested)
{
    GInputStream        *stream = G_INPUT_STREAM (handle);
    GCancellable        *cancellable = G_VFS_JOB (job)->cancellable;

    g_message ("TRY READ");

    g_input_stream_read_async (stream,
                               buffer,
                               bytes_requested,
                               G_PRIORITY_DEFAULT,
                               cancellable,
                               read_ready,
                               job);
    return TRUE;
}

static void
close_read_ready (GObject      *source_object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
    gboolean        res;

    GError          *error = NULL;
    GVfsJob         *job = G_VFS_JOB (user_data);
    GInputStream    *stream = G_INPUT_STREAM (source_object);

    res = g_input_stream_close_finish (stream,
                                       result,
                                       &error);
    if (res == FALSE)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
      }
    else
      g_vfs_job_succeeded (job);

    g_object_unref (stream);
}

static gboolean 
try_close_read (GVfsBackend       *backend,
                GVfsJobCloseRead  *job,
                GVfsBackendHandle  handle)
{
    GInputStream    *stream;

    stream = G_INPUT_STREAM (handle);

    g_input_stream_close_async (stream,
                                G_PRIORITY_DEFAULT,
                                G_VFS_JOB (job)->cancellable,
                                close_read_ready,
                                job);
    return TRUE;
}

static void
write_ready (GObject      *source_object,
            GAsyncResult *result,
            gpointer      user_data)
{
    GOutputStream   *stream;
    GVfsJob         *job;
    GError          *error;
    gssize          nwrite;

    stream = G_OUTPUT_STREAM (source_object); 
    error  = NULL;
    job    = G_VFS_JOB (user_data);

    nwrite = g_output_stream_write_finish (stream, result, &error);

    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    g_vfs_job_write_set_written_size (G_VFS_JOB_WRITE (job), nwrite);
    g_vfs_job_succeeded (job);
}

static gboolean
try_write (GVfsBackend       *backend,
          GVfsJobWrite      *job,
          GVfsBackendHandle handle,
          char              *buffer,
          gsize             buffer_size)
{
    GCancellable        *cancellable = G_VFS_JOB (job)->cancellable;
    GOutputStream       *output_stream = G_OUTPUT_STREAM (handle);

    g_output_stream_write_async (output_stream,
                                 buffer,
                                 buffer_size,
                                 G_PRIORITY_DEFAULT,
                                 cancellable,
                                 write_ready,
                                 job);
     return TRUE;
}

static void
close_write_ready (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
    gboolean        res;

    GError          *error = NULL;
    GVfsJob         *job = G_VFS_JOB (user_data);
    GOutputStream   *stream = G_OUTPUT_STREAM (source_object);

    res = g_output_stream_close_finish (stream,
                                        result,
                                        &error);
    if (res == FALSE)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
      }
    else
      g_vfs_job_succeeded (job);

    g_object_unref (stream);
}

static gboolean
try_close_write (GVfsBackend         *backend,
                 GVfsJobCloseWrite   *job,
                 GVfsBackendHandle   handle)
{
    GOutputStream    *stream = G_OUTPUT_STREAM (handle);

    g_output_stream_close_async (stream,
                                 G_PRIORITY_DEFAULT,
                                 G_VFS_JOB (job)->cancellable,
                                 close_write_ready,
                                 job);
    return TRUE;
}

static void
do_delete (GVfsBackend      *backend,
           GVfsJobDelete    *job,
           const char       *filename)
{
    GVfsGDocsFile           *file;
    GDataDocumentsEntry     *entry;

    GError                  *error = NULL;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService            *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend),
                                           filename,
                                           cancellable,
                                           &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    entry = g_vfs_gdocs_file_get_document_entry (file);
    gdata_service_delete_entry (GDATA_SERVICE (service), 
                                GDATA_ENTRY (entry), 
                                cancellable, 
                                &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    g_message ("%s :deleted\n", gdata_entry_get_title (GDATA_ENTRY (entry)));

    /* We keep the #GHashTable::entries property up to date*/
    g_vfs_backend_gdocs_remove_gdocs_file (gdocs_backend, file);

    g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_query_info (GVfsBackend              *backend,
               GVfsJobQueryInfo         *job,
               const char               *filename,
               GFileQueryInfoFlags      flags,
               GFileInfo                *info,
               GFileAttributeMatcher    *matcher)
{
    GVfsGDocsFile   *file;

    GError          *error = NULL;
    GCancellable    *cancellable = G_VFS_JOB (job)->cancellable;

    file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend),
                                           filename,
                                           cancellable,
                                           &error);
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
do_replace (GVfsBackend         *backend,
            GVfsJobOpenForWrite *job,
            const char          *filename,
            const char          *etag,
            gboolean            make_backup,
            GFileCreateFlags    flags)
{
    GFile                   *local_file;
    GVfsGDocsFile           *file;
    GDataDocumentsEntry     *new_entry, *entry;

    GError                  *error = NULL;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    if (make_backup)
      {
        /* TODO: implement! */
        g_set_error_literal (&error,
                             G_IO_ERROR,
                             G_IO_ERROR_CANT_CREATE_BACKUP,
                             _("backups not supported yet"));
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    file = g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend),
                                           filename,
                                           cancellable,
                                           &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    local_file = g_file_new_for_path (filename);
    entry = GDATA_DOCUMENTS_ENTRY (g_vfs_gdocs_file_get_document_entry (file));
    new_entry = gdata_documents_service_update_document (service,
                                                         entry,
                                                         local_file,
                                                         cancellable,
                                                         &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        g_object_unref (local_file);
        return;
      }
}

static void
do_push (GVfsBackend *backend,
         GVfsJobPush *job,
         const char *destination,
         const char *local_path,
         GFileCopyFlags flags,
         gboolean remove_source,
         GFileProgressCallback progress_callback,
         gpointer progress_callback_data)
{
    gchar                   *destination_filename;
    const gchar             *entry_id;
    GDataDocumentsEntry     *entry, *new_entry;
    GFile                   *local_file;
    GVfsGDocsFile           *destination_folder, *file;

    GError                  *error = NULL;
    GDataDocumentsFolder    *folder_entry = NULL;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GDataDocumentsService   *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    destination_folder = g_vfs_gdocs_file_new_parent_from_gvfs (gdocs_backend,
                                                                destination,
                                                                cancellable,
                                                                &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    if (g_vfs_gdocs_file_is_root (destination_folder))
        destination_folder = NULL;
    else
      {
        GDataDocumentsEntry *tmp_entry;
        tmp_entry = g_vfs_gdocs_file_get_document_entry (destination_folder),
        folder_entry = GDATA_DOCUMENTS_FOLDER (tmp_entry);
      }

    entry = GDATA_DOCUMENTS_ENTRY (gdata_documents_spreadsheet_new (NULL));
    destination_filename = g_path_get_basename (destination);
    gdata_entry_set_title (GDATA_ENTRY (entry), destination_filename);
    g_free (destination_filename);

    g_message ("Destination name:local path %s", local_path);
    local_file = g_file_new_for_path (local_path);
    new_entry = gdata_documents_service_upload_document (service,
                                                         NULL,
                                                         local_file,
                                                         folder_entry,
                                                         cancellable,
                                                         &error);
    g_object_unref (entry);
    g_object_unref (local_file);

    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        if (new_entry != NULL)
            g_object_unref (new_entry);
        return;
      }

    /* We keep the #GHashTable::entries property up to date */
    entry_id = gdata_documents_entry_get_document_id (new_entry);
    file = g_vfs_gdocs_file_new_from_document_entry (gdocs_backend, new_entry, &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        if (new_entry != NULL)
            g_object_unref (new_entry);
        return;
      }
    g_vfs_backend_gdocs_add_gdocs_file (gdocs_backend, file);

    g_object_unref (entry);
    g_object_unref (new_entry);

    g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_pull (GVfsBackend            *backend,
         GVfsJobPull            *job,
         const char             *source,
         const char             *local_path,
         GFileCopyFlags         flags,
         gboolean               remove_source,
         GFileProgressCallback  progress_callback,
         gpointer               progress_callback_data)
{
    GVfsGDocsFile           *file;
    GFile                   *new_file;

    GError                  *error = NULL;
    gchar                   *content_type = NULL;
    gboolean                replace_if_exists = FALSE;
    GCancellable            *cancellable = G_VFS_JOB (job)->cancellable;

    file =  g_vfs_gdocs_file_new_from_gvfs (G_VFS_BACKEND_GDOCS (backend),
                                            source,
                                            cancellable,
                                            &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    if (flags & G_FILE_COPY_OVERWRITE)
        replace_if_exists = TRUE;

    new_file = g_vfs_gdocs_file_download_file (file,
                                               &content_type,
                                               local_path,
                                               replace_if_exists,
                                               TRUE,
                                               cancellable,
                                               &error);
    if (new_file != NULL)
        g_object_unref (new_file);

    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        return;
      }

    g_vfs_job_succeeded (G_VFS_JOB (job));
}

static void
do_create (GVfsBackend          *backend,
           GVfsJobOpenForWrite  *job,
           const char           *filename,
           GFileCreateFlags     flags)
{
    const gchar             *title, *content_type;
    gchar                   *upload_uri;
    GFile                   *file;
    GFileInfo               *file_info;
    GVfsGDocsFile           *parent_folder;
    GOutputStream           *output_stream;
    GDataDocumentsFolder    *folder_entry;

    GError                  *error = NULL;
    GDataDocumentsEntry     *entry = NULL;
    GVfsBackendGdocs        *gdocs_backend = G_VFS_BACKEND_GDOCS (backend);
    GCancellable *cancellable = G_VFS_JOB (job)->cancellable;
    GDataDocumentsService *service = g_vfs_backend_gdocs_get_service (gdocs_backend);

    /*  TODO 
     *  Figure out how the content_type and title should be found
     **/
    file = g_file_new_for_path (filename);
    file_info =  g_file_query_info (file,
                                    "standard::display-name,standard::content-type",
                                    G_FILE_QUERY_INFO_NONE,
                                    NULL,
                                    &error);
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

    parent_folder = g_vfs_gdocs_file_new_parent_from_gvfs (gdocs_backend,
                                                           filename,
                                                           cancellable,
                                                           &error);
    if (error != NULL)
      {
        g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
        g_error_free (error);
        if (file_info != NULL)
            g_object_unref (file_info);
        return;
      }

    entry = g_vfs_gdocs_file_get_document_entry (parent_folder);
    folder_entry = GDATA_DOCUMENTS_FOLDER (entry);
    upload_uri = gdata_documents_service_get_upload_uri (folder_entry);

    output_stream = gdata_upload_stream_new (GDATA_SERVICE (service),
                                             SOUP_METHOD_POST,
                                             upload_uri,
                                             NULL,
                                             title,
                                             content_type);
    g_free (upload_uri);
    g_object_unref (file_info);

    g_vfs_job_open_for_write_set_can_seek (job, FALSE);
    g_vfs_job_open_for_write_set_handle (job, output_stream);
    g_vfs_job_succeeded (G_VFS_JOB (job));
}

/* ********************************************************************************** */
/* Class handling functions */
static void
g_vfs_backend_gdocs_init (GVfsBackendGdocs *backend)
{
    backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (backend,
                                                 G_VFS_TYPE_BACKEND_GDOCS,
                                                 GVfsBackendGdocsPrivate);

    backend->priv->service = gdata_documents_service_new (CLIENT_ID);
    backend->priv->entries = g_hash_table_new_full (g_str_hash,
                                                    g_str_equal,
                                                    NULL,
                                                    g_object_unref);
}

static void
g_vfs_backend_gdocs_finalize (GObject *object)
{
    GVfsBackendGdocsPrivate *priv = G_VFS_BACKEND_GDOCS_GET_PRIVATE (object);

    g_hash_table_destroy (priv->entries);
    if (priv->service != NULL )
        g_object_unref (priv->service);

    /* Look up to the parent class */
    G_OBJECT_CLASS (g_vfs_backend_gdocs_parent_class)->finalize (object);
}

static void
g_vfs_backend_gdocs_class_init (GVfsBackendGdocsClass *klass)
{
    GVfsBackendClass *backend_class;
    GObjectClass     *gobject_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (GVfsBackendGdocsPrivate));

    gobject_class->finalize  = g_vfs_backend_gdocs_finalize;

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
    backend_class->try_read = try_read;
    backend_class->try_close_read = try_close_read;
    backend_class->try_close_write = try_close_write;
    backend_class->try_write = try_write;
    backend_class->query_info = do_query_info;
    backend_class->create = do_create;
    backend_class->set_display_name = do_set_display_name;
}
