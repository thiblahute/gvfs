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

/**
 * SECTION:gvfsgdatafile
 * @short_description: This class maps between GVfs paths and the actual entry google server.
 * @stability: Unstable
 * @include: daemon/gvfsdatafile.h
 *
 * TODO: handle picasaweb files
 */

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <gdata/gdata.h>
#include "gvfsgdatafile.h"

static void g_vfs_gdata_file_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void g_vfs_gdata_file_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void g_vfs_gdata_file_dispose (GObject *object);
static void g_vfs_gdata_file_finalize (GObject *object);


/*Private structure*/
struct _GVfsGDataFilePrivate {
  GVfsBackendGdocs *backend;
  gchar *gvfs_path;
  GDataEntry *gdata_entry;
};

enum {
	PROP_BACKEND = 1,
	PROP_GVFS_PATH,
	PROP_GDATA_ENTRY
};

G_DEFINE_TYPE (GVfsGDataFile, g_vfs_gdata_file, G_TYPE_OBJECT)
#define G_VFS_GDATA_FILE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), G_VFS_TYPE_GDATA_FILE, GVfsGDataFilePrivate))

/**
 * g_vfs_gdata_file_get_document_id_from_gvfs 
 * Gets the id of the document passed as @path.
 * @path a gvfs path
 * If @path is the root, return NULL
 *
 * Returns: the GDataDocuments::id corresponding to @path
 * */
gchar *
g_vfs_gdata_file_get_document_id_from_gvfs (const gchar *path)
{
	gchar **entries_id_array, *entry_id;
	gint i = 0;

	g_return_val_if_fail (g_strcmp0 (path, "/") != 0, NULL);
	entries_id_array = g_strsplit (path, "/", 0);

	while (entries_id_array[i] != NULL)
	{
		g_free (entry_id);
		entry_id = g_strdup (entries_id_array[i]);
		i++;
	}
	g_strfreev (entries_id_array);

	return entry_id;
}

/**
 * Gets the id of the document's direct parent.
 * @path: a gvfs path
 *
 * If @path's parent is the root, return NULL
 *
 * Returns: the parent entry ID.
 * */
gchar *
g_vfs_gdata_get_parent_id_from_gvfs (const gchar *path)
{
	gchar **entries_id_array, *entry_id;
	gint i = 0;
	
	g_return_val_if_fail (strcmp (path, "/") != 0, NULL);
	entries_id_array = g_strsplit (path, "/", 0);
	/*We get the before last entry ID*/
	while (1)
	{
		entry_id = entries_id_array[i];
		i++;
		if (entries_id_array[i] == NULL)
		{
			g_free (entry_id);
			return NULL;
		}
		if (entries_id_array[i+1] == NULL)
			break;
	}
	return entry_id;
}

/**
 * g_vfs_gdata_file_new_folder_from_gvfs:
 * @gdata: the gdata backend this file is to be used on
 * @gvfs_path: gvfs path to create the file from
 * @cancellable: a GCancellable or %NULL
 * @error: a GError or %NULL
 *
 * Constructs a new #GVfsGDataFile representing the given gvfs path.
 * If this is the root directory, the GVfsGDataFile::gdata-entry will be %NULL
 *
 * Returns: a new GVfsGDataFile, or %NULL 
 * If the file doesn't exit, a G_IO_ERROR_NOT_FOUND is set.
 * If there was an error trying to get the feed, the server error is set.
 * If the file isn't a folder a G_IO_ERROR_NOT_DIRECTORY error is set.
 **/
GVfsGDataFile *
g_vfs_gdata_file_new_folder_from_gvfs (GVfsBackendGdocs *backend, const gchar *gvfs_path, GCancellable *cancellable, GError **error)
{
	GVfsGDataFile *folder;
	folder = g_vfs_gdata_file_new_from_gvfs (backend, gvfs_path, cancellable, error);
	g_return_val_if_fail (error == NULL, NULL);
	
	if (!g_vfs_gdata_file_is_folder (folder))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY,
					 _("%s is not a directory"), gvfs_path);
		g_object_unref (folder);
		return NULL;
	}
	return folder;
}
/**
 * g_vfs_gdata_file_new_from_gvfs:
 * @gdata: the gdata backend this file is to be used on
 * @gvfs_path: gvfs path to create the file from
 * @cancellable: a GCancellable or %NULL
 * @error: a GError or %NULL
 *
 * Constructs a new #GVfsGDataFile representing the given gvfs path.
 * If this is the root directory, the GVfsGDataFile::gdata-entry will be %NULL
 *
 * Returns: a new GVfsGDataFile, or %NULL 
 * If the file doesn't exit, a G_IO_ERROR_NOT_FOUND is set.
 * If there was an error trying to get the feed, the server error is set.
 **/
GVfsGDataFile *
g_vfs_gdata_file_new_from_gvfs (GVfsBackendGdocs *backend, const gchar *gvfs_path, GCancellable *cancellable, GError **error)
{
	GVfsGDataFile *file;
	GDataDocumentsQuery *entry_query;
	GDataDocumentsFeed *tmp_feed;
	GList *entry_list=NULL;
	gchar *entry_id=NULL;
	GDataDocumentsService *service = g_object_ref (G_VFS_BACKEND_GDOCS (backend)->service);

	g_return_val_if_fail (G_VFS_IS_BACKEND_GDOCS (backend), NULL);
	g_return_val_if_fail (gvfs_path != NULL, NULL);
	
	entry_id = g_vfs_gdata_file_get_document_id_from_gvfs (gvfs_path);
	if (entry_id == NULL)
	{
		return g_object_new (G_VFS_TYPE_GDATA_FILE, 
							 "backend", backend,
							 NULL);
	}

	entry_query = gdata_documents_query_new (NULL);
	gdata_query_set_entry_id (GDATA_QUERY (entry_query), entry_id);
	g_free (entry_id);

	/*Get the entry (as feed) on the server*/
	tmp_feed = gdata_documents_service_query_documents (service, entry_query, cancellable, NULL, NULL, error);
	g_object_unref (entry_query);
	
	if (error != NULL)
	{
		if (tmp_feed != NULL)
			g_object_unref (tmp_feed);
		return NULL;
	}

	entry_list = gdata_feed_get_entries (GDATA_FEED (tmp_feed));
	if (entry_list == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("%s not found"), gvfs_path);
		if (tmp_feed != NULL)
			g_object_unref (tmp_feed);
		return NULL;
	}

	file = g_object_new (G_VFS_TYPE_GDATA_FILE, "backend", backend, "gdata-entry", GDATA_ENTRY (entry_list->data), "gvfs-path", gvfs_path, NULL);

	g_object_unref (tmp_feed);
	g_object_unref (service);
	g_list_free (entry_list);

	return file;	
}

/**
 * g_vfs_gdata_file_new_from_gdata:
 * @gdata: the gdata backend this file is to be used on
 * @gdata_entry: gdata entry to create the file from
 * @error: a #GError or %NULL
 *
 * Constructs a new #GVfsGDataFile representing the given gdata path.
 *
 * Returns: a new #GVfsGDataFile
 * if the gdata_entry is not an handled #GDataEntry, a G_IO_ERROR_NOT_FOUND error is set;
 **/
GVfsGDataFile *
g_vfs_gdata_file_new_from_gdata (GVfsBackendGdocs *backend, GDataEntry *gdata_entry, GError **error)
{
	gchar *gvfs_path;

	g_return_val_if_fail (G_VFS_IS_BACKEND_GDOCS (backend), NULL);
	g_return_val_if_fail (!GDATA_IS_ENTRY (gdata_entry), NULL);

	/*TODO handle picass albums*/
	if (GDATA_IS_DOCUMENTS_ENTRY (gdata_entry))
		gvfs_path = gdata_documents_entry_get_path (GDATA_DOCUMENTS_ENTRY (gdata_entry));
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("%s not found a document"), gdata_entry_get_title (gvfs_path));
		return NULL;
	}

	return g_object_new (G_VFS_TYPE_GDATA_FILE, "backend", backend, "gdata-entry", gdata_entry, "gvfs-path", gvfs_path, NULL);
}

/**
 * g_vfs_gdata_file_new_parent:
 * @file: file to get the parent directory from
 * @cancellable: a GCancellable or %NULL
 * @error: GError or %NULL
 *
 * Creates a new file to represent the parent directory of @file. If @file's parent is 
 * the root directory, the new file will also reference
 * the root.
 *
 * Returns: a new file representing the parent directory of @file
 **/
GVfsGDataFile *
g_vfs_gdata_file_new_parent (GVfsBackendGdocs *backend, const GVfsGDataFile *file, GCancellable *cancellable, GError **error)
{
	return g_vfs_gdata_file_new_parent_from_gvfs (backend, file->priv->gvfs_path, cancellable, error);
}

/**
 * g_vfs_gdata_file_new_parent_from_gvfs 
 * @gvfs_path: gvfs path of the file 
 * @cancellable: a GCancellable or %NULL
 * @error: GError or %NULL
 *
 * Creates a new file to represent the parent directory of the file represented by @gvfs_path.
 * If it's the root directory the root directory, the new file will also reference
 * the root.
 *
 * Returns: a new file representing the parent directory of @file, if it's the root directory, GVfsGDataFile::gdata-entry  will be %NULL
 * If the file doesn't exit, a G_IO_ERROR_NOT_FOUND is set.
 * If there was an error trying to get the feed, the server error is set.
 * If the newly created GVfsGDataFile is not a folder a G_IO_ERROR_NOT_DIRECTORY error is set.
 */
GVfsGDataFile *
g_vfs_gdata_file_new_parent_from_gvfs (GVfsBackendGdocs *backend, const gchar *gvfs_path, GCancellable *cancellable, GError **error)
{
	GVfsGDataFile *parent_folder;
	GDataDocumentsQuery *entry_query;
	GDataDocumentsFeed *tmp_feed;
	GList *entry_list;
	gchar *parent_entry_id;
	GDataDocumentsService *service = g_object_ref (G_VFS_BACKEND_GDOCS (backend)->service);
	
	g_return_val_if_fail (G_VFS_IS_BACKEND_GDOCS (backend), NULL);
	g_return_val_if_fail (gvfs_path != NULL, NULL);

	parent_entry_id = g_vfs_gdata_get_parent_id_from_gvfs (gvfs_path);

	if (parent_entry_id == NULL)
	{
		return g_object_new (G_VFS_TYPE_GDATA_FILE,
							 "backend", backend,
							 "gdata-entry", NULL,
							 NULL);
	}
	
	entry_query = gdata_documents_query_new (NULL);
	gdata_query_set_entry_id (GDATA_QUERY (entry_query), parent_entry_id);
	g_free (parent_entry_id);

	/*Get the entry (as feed) on the server*/
	tmp_feed = gdata_documents_service_query_documents (service, entry_query, cancellable, NULL, NULL, error);
	g_object_unref (entry_query);
	
	if (error != NULL)
	{
		if (tmp_feed != NULL)
			g_object_unref (tmp_feed);
		return NULL;
	}

	entry_list = gdata_feed_get_entries (GDATA_FEED (tmp_feed));
	if (entry_list == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("%s not found"), gvfs_path);
		if (tmp_feed != NULL)
			g_object_unref (tmp_feed);
		return NULL;
	}

	/*Construct the parent folder GVfsGDataFile object*/
	parent_folder = g_object_new (G_VFS_TYPE_GDATA_FILE,
								  "backend", backend,
								  "gvfs-path", gvfs_path,
								  "gdata-entry", GDATA_ENTRY (entry_list->data));
	g_object_unref (tmp_feed);
	g_object_unref (service);
	g_object_unref (backend);
	g_object_unref (entry_list->data);
	g_free (gvfs_path);
	g_list_free (entry_list);
	if (!g_vfs_gdata_file_is_folder (parent_folder))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY,
					 _("%s is not a directory"), parent_folder->priv->gvfs_path);
		return NULL;
	}

	return parent_folder;	
}

/**
 * g_vfs_gdata_file_is_root:
 * @file: the file to check
 *
 * Checks if the given file references the root directory.
 *
 * Returns: %TRUE if @file references the root directory
 **/
gboolean
g_vfs_gdata_file_is_root (const GVfsGDataFile *file)
{
  g_return_val_if_fail (g_vfs_gdata_file_is_folder (file), FALSE);

  return file->priv->gvfs_path[0] == '/' && file->priv->gvfs_path[1] == 0 && file->priv->gdata_entry == NULL;
}

/**
 * g_vfs_gdata_file_get_gdata_entry:
 * @file: a file
 *
 * Gets the GDataDocuments entry reprensenting the file on the google documents server.
 *
 * Returns: the path to refer to @file on the GDOCS server.
 **/
GDataEntry *
g_vfs_gdata_file_get_gdata_entry (const GVfsGDataFile *file)
{
  g_return_val_if_fail (G_VFS_IS_GDATA_FILE (file), NULL);

  return file->priv->gdata_entry;
}

/**
 * g_vfs_gdata_file_get_gvfs_path:
 * @file: a file
 *
 * Gets the GVfs path used to refer to @file.
 *
 * Returns: the GVfs path used to refer to @file.
 **/
const gchar *
g_vfs_gdata_file_get_gvfs_path (const GVfsGDataFile *file)
{
  g_return_val_if_fail (G_VFS_IS_GDATA_FILE (file), NULL);

  return file->priv->gvfs_path;
}

/**
 * g_vfs_gdata_file_equal:
 * @a: a #GVfsGDataFile
 * @b: a #GVfsGDataFile
 *
 * Compares @a and @b. If they reference the same file, %TRUE is returned.
 * This function uses #gconstpointer arguments to the #GEqualFunc type.
 *
 * Returns: %TRUE if @a and @b reference the same file.
 **/
gboolean
g_vfs_gdata_file_equal (const GVfsGDataFile *a, const GVfsGDataFile *b)
{
	g_return_val_if_fail (G_VFS_IS_GDATA_FILE (a), FALSE);
	g_return_val_if_fail (G_VFS_IS_GDATA_FILE (b) != NULL, FALSE);

	return g_str_equal (a->priv->gvfs_path, b->priv->gvfs_path);
}

/**
 * g_vfs_gdata_file_is_folder
 * @file: a #GVfsGDataFile file
 *
 * Return %TRUE if @file is a folder otherwise %FALSE
 */
gboolean
g_vfs_gdata_file_is_folder (const GVfsGDataFile *file)
{
	g_return_val_if_fail (G_VFS_IS_GDATA_FILE (file), FALSE);

	if (g_strcmp0 (file->priv->gvfs_path, "/") == 0)
		return TRUE;
	else if (GDATA_IS_DOCUMENTS_FOLDER (file->priv->gdata_entry))
		return TRUE;
	else
		return FALSE;
}

static void 
g_vfs_gdata_file_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	GVfsGDataFilePrivate *priv = G_VFS_GDATA_FILE_GET_PRIVATE (object);
	switch (property_id)
	{
		case PROP_BACKEND:
			g_value_set_object (value, priv->backend);
			break;
		case PROP_GVFS_PATH:
			g_value_set_string (value, priv->gvfs_path);
			break;
		case PROP_GDATA_ENTRY:
			g_value_set_object (value, priv->gdata_entry);
			break;
	}
}
static void 
g_vfs_gdata_file_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	GVfsGDataFilePrivate *priv = G_VFS_GDATA_FILE_GET_PRIVATE (object);
	switch (property_id) 
	{
		case PROP_BACKEND:
			priv->backend = g_value_dup_object (value);
			break;
		case PROP_GVFS_PATH:
			priv->gvfs_path = g_value_dup_string (value);
			break;
		case PROP_GDATA_ENTRY:
			priv->gdata_entry = g_value_dup_object (value);
			break;
	}
}
static void 
g_vfs_gdata_file_dispose (GObject *object)
{
	GVfsGDataFilePrivate *priv = G_VFS_GDATA_FILE_GET_PRIVATE (object);
	if (priv->backend != NULL)
		g_object_unref (priv->backend);
	if (priv->gdata_entry != NULL)
		g_object_unref (priv->gdata_entry);
}
	
static void 
g_vfs_gdata_file_finalize (GObject *object)
{
	GVfsGDataFilePrivate *priv = G_VFS_GDATA_FILE_GET_PRIVATE (object);

	g_free (priv->gvfs_path);
}

static void
g_vfs_gdata_file_class_init (GVfsGDataFileClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->get_property = g_vfs_gdata_file_get_property;
	gobject_class->set_property = g_vfs_gdata_file_set_property;
	gobject_class->dispose = g_vfs_gdata_file_dispose;
	gobject_class->finalize = g_vfs_gdata_file_finalize;

	/**
	 * GVfsGDataFile:backend:
	 *
	 * The #GDataEntry corresponding.
	 **/
	g_object_class_install_property (gobject_class, PROP_BACKEND,
									 g_param_spec_object ("backend",
														  "backend", "The backend related to this #GVfsGDataFile",
														  G_VFS_TYPE_BACKEND,
														  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
	/*
	 * GVfsGDataFile:gdata-entry
	 *
	 * The #GDataEntry corresponding.
	 **/
	g_object_class_install_property (gobject_class, PROP_GDATA_ENTRY,
									 g_param_spec_object ("gdata-entry",
														  "gdata-entry", "The #GDataEntry corresponding", 
														  GDATA_TYPE_ENTRY,
														  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
	/**
	 * GDataGDEmailAddress:label:
	 *
	 * A simple string corresponding to the gvfs path of the class.
	 **/
	g_object_class_install_property (gobject_class, PROP_GVFS_PATH,
				g_param_spec_string ("gvfs-path",
					"Gvfs path", "A simple string corresponding to the gvfs path of the class.",
					"/",
					G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

}

static void
g_vfs_gdata_file_init (GVfsGDataFile *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, G_VFS_TYPE_GDATA_FILE, GVfsGDataFilePrivate);
}
