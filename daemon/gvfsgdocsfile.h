/* GIO - GLib Input, Output and Streaming Library
 *
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

#ifndef __G_VFS_GDOCS_FILE_H__
#define __G_VFS_GDOCS_FILE_H__

#include <glib.h>
#include <glib-object.h>
#include <gvfsbackendgdocs.h>

G_BEGIN_DECLS

#define G_VFS_TYPE_GDOCS_FILE        (g_vfs_gdocs_file_get_type ())
#define G_VFS_GDOCS_FILE(o)        (G_TYPE_CHECK_INSTANCE_CAST ((o), G_VFS_TYPE_GDOCS_FILE, GVfsGDocsFile))
#define G_VFS_GDOCS_FILE_CLASS(k)        (G_TYPE_CHECK_CLASS_CAST((k), G_VFS_TYPE_GDOCS_FILE, GVfsGDocsFileClass))
#define G_VFS_IS_GDOCS_FILE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), G_VFS_TYPE_GDOCS_FILE))
#define G_VFS_IS_GDOCS_FILE_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k),G_VFS_TYPE_GDOCS_FILE))
#define G_VFS_GDOCS_FILE_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), G_VFS_TYPE_GDOCS_FILE, GVfsGDocsFileClass))

typedef struct _GVfsGDocsFilePrivate GVfsGDocsFilePrivate;

typedef struct{
    GObject parent;
    GVfsGDocsFilePrivate *priv;
}GVfsGDocsFile;

typedef struct {
    GObjectClass parent;
}GVfsGDocsFileClass;

GType g_vfs_gdocs_file_get_type (void) G_GNUC_CONST;

gchar                    *g_path_get_parent_basename (const gchar *filename);

GVfsGDocsFile            *g_vfs_gdocs_file_new_from_gvfs (GVfsBackendGdocs *backend,
                                                         const gchar *gvfs_path,
                                                         GCancellable *cancellable,
                                                         GError **error);

GVfsGDocsFile            *g_vfs_gdocs_file_new_folder_from_gvfs (GVfsBackendGdocs *backend,
                                                                const gchar *gvfs_path,
                                                                GCancellable *cancellable,
                                                                GError **error);

GVfsGDocsFile            *g_vfs_gdocs_file_new_from_document_entry (GVfsBackendGdocs *backend,
                                                                   GDataDocumentsEntry *document_entry,
                                                                   GError **error);

GVfsGDocsFile            *g_vfs_gdocs_file_new_parent (GVfsBackendGdocs *backend,
                                                      const GVfsGDocsFile *file,
                                                      GCancellable *cancellable,
                                                      GError **error);

GVfsGDocsFile            *g_vfs_gdocs_file_new_parent_from_gvfs (GVfsBackendGdocs *backend,
                                                                const gchar *gvfs_path,
                                                                GCancellable *cancellable,
                                                                GError **error);

gboolean                g_vfs_gdocs_file_is_root (const GVfsGDocsFile *file);

GDataDocumentsEntry        *g_vfs_gdocs_file_get_document_entry (const GVfsGDocsFile *file);

const gchar                *g_vfs_gdocs_file_get_gvfs_path (const GVfsGDocsFile *file);

GVfsBackendGdocs        *g_vfs_gdocs_file_get_backend (const GVfsGDocsFile *file);

gboolean                g_vfs_gdocs_file_equal (const GVfsGDocsFile *a,
                                                const GVfsGDocsFile *b);

GFileInfo                *g_vfs_gdocs_file_get_info (GVfsGDocsFile *file,
                                                    GFileInfo *info,
                                                    GFileAttributeMatcher *matcher,
                                                    GError **error);

gboolean                g_vfs_gdocs_file_is_folder (const GVfsGDocsFile *file);

GFile                    *g_vfs_gdocs_file_download_file (GVfsGDocsFile *file,
                                                         gchar **content_type,
                                                         const gchar *local_path,
                                                         gboolean replace_file_if_exists,
                                                         gboolean download_folders,
                                                         GCancellable *cancellable,
                                                         GError **error);

gchar                    *g_vfs_gdocs_file_get_download_uri (GVfsGDocsFile *file,
                                                            GCancellable *cancellable,
                                                            GError **error);

G_END_DECLS

#endif /* __G_VFS_GDOCS_FILE_H__ */
