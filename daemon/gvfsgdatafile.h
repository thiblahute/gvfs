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

#ifndef __G_VFS_GDATA_FILE_H__
#define __G_VFS_GDATA_FILE_H__

#include <glib.h>
#include <glib-object.h>
#include <gvfsbackendgdocs.h>

G_BEGIN_DECLS

#define G_VFS_TYPE_GDATA_FILE		(g_vfs_gdata_file_get_type ())
#define G_VFS_GDATA_FILE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), G_VFS_TYPE_GDATA_FILE, GVfsGDataFile))
#define G_VFS_GDATA_FILE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), G_VFS_TYPE_GDATA_FILE, GVfsGDataFileClass))
#define G_VFS_IS_GDATA_FILE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), G_VFS_TYPE_GDATA_FILE))
#define G_VFS_IS_GDATA_FILE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k),G_VFS_TYPE_GDATA_FILE))
#define G_VFS_GDATA_FILE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), G_VFS_TYPE_GDATA_FILE, GVfsGDataFileClass))

typedef struct _GVfsGDataFilePrivate GVfsGDataFilePrivate;

typedef struct{
	GObject parent;
	GVfsGDataFilePrivate *priv;
}GVfsGDataFile;

typedef struct {
	GObjectClass parent;
}GVfsGDataFileClass;

GType g_vfs_gdata_file_get_type (void) G_GNUC_CONST;

gchar *g_vfs_gdata_file_get_document_id_from_gvfs (const gchar *path);
gchar *g_vfs_gdata_file_get_document_id_from_gvfs (const gchar *path);
gchar *g_vfs_gdata_get_parent_id_from_gvfs (const gchar *path);
GVfsGDataFile *g_vfs_gdata_file_new_from_gvfs (GVfsBackendGdocs *backend, const gchar *gvfs_path, GCancellable *cancellable, GError **error);
GVfsGDataFile *g_vfs_gdata_file_new_folder_from_gvfs (GVfsBackendGdocs *backend, const gchar *gvfs_path, GCancellable *cancellable, GError **error);
GVfsGDataFile *g_vfs_gdata_file_new_from_gdata (GVfsBackendGdocs *backend, GDataEntry *gdata_entry, GError **error);
GVfsGDataFile *g_vfs_gdata_file_new_parent (GVfsBackendGdocs *backend, const GVfsGDataFile *file, GCancellable *cancellable, GError **error);
GVfsGDataFile *g_vfs_gdata_file_new_parent_from_gvfs (GVfsBackendGdocs *backend, const gchar *gvfs_path, GCancellable *cancellable, GError **error);
gboolean g_vfs_gdata_file_is_root (const GVfsGDataFile *file);
GDataEntry *g_vfs_gdata_file_get_gdata_entry (const GVfsGDataFile *file);
const gchar *g_vfs_gdata_file_get_gvfs_path (const GVfsGDataFile *file);
GVfsBackendGdocs *g_vfs_gdata_file_get_backend (const GVfsGDataFile *file);
gboolean g_vfs_gdata_file_equal (const GVfsGDataFile *a, const GVfsGDataFile *b);
gboolean g_vfs_gdata_file_is_folder (const GVfsGDataFile *file);

G_END_DECLS

#endif /* __G_VFS_GDATA_FILE_H__ */
