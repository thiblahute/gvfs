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

#ifndef __G_VFS_BACKEND_GDOCS_H__
#define __G_VFS_BACKEND_GDOCS_H__

#include <gvfsbackend.h>
#include <gmountspec.h>
#include <gdata/gdata.h>


G_BEGIN_DECLS

#define G_VFS_TYPE_BACKEND_GDOCS         (g_vfs_backend_gdocs_get_type ())
#define G_VFS_BACKEND_GDOCS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), G_VFS_TYPE_BACKEND_GDOCS, GVfsBackendGdocs))
#define G_VFS_BACKEND_GDOCS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), G_VFS_TYPE_BACKEND_GDOCS, GVfsBackendGdocsClass))
#define G_VFS_IS_BACKEND_GDOCS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), G_VFS_TYPE_BACKEND_GDOCS))
#define G_VFS_IS_BACKEND_GDOCS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), G_VFS_TYPE_BACKEND_GDOCS))
#define G_VFS_BACKEND_GDOCS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), G_VFS_TYPE_BACKEND_GDOCS, GVfsBackendGdocsClass))

typedef struct _GVfsBackendGdocs        GVfsBackendGdocs;
typedef struct _GVfsBackendGdocsClass   GVfsBackendGdocsClass;

GType g_vfs_backend_gdocs_get_type (void) G_GNUC_CONST;

struct _GVfsBackendGdocsClass
{
    GVfsBackendClass parent_class;
};

struct _GVfsBackendGdocs
{
    GVfsBackend				parent_instance;
    GDataDocumentsService	*service;
    GHashTable				*entries;
};

void g_vfs_backend_gdocs_rebuild_entries (GVfsBackendGdocs  *backend,
                                          GCancellable      *cancellable,
                                          GError            **error);

G_END_DECLS

#endif /* __G_VFS_BACKEND_GDOCS_H__ */
