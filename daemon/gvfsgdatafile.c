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

static void
convert_slashes (char *str)
{
  char *s;

  while ((s = strchr (str, '/')) != NULL)
    *s = '\\';
}

/**
 * g_vfs_gdata_file_get_document_id_from_gvfs
 * Gets the id of the document passed as @path.
 * @path a gvfs path
 *
 * Returns: the #GDataDocumentsEntry::id corresponding to @path, or "/" if it's the root directory
 * */
gchar *
g_vfs_gdata_file_get_document_id_from_gvfs (const gchar *path)
{
	gchar	**entries_id_array; 
	
	gchar	*entry_id = NULL;
	gint	i = 0;

	if (g_strcmp0 (path, "/") == 0)
		return g_strdup ("/");

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
 * Returns: the parent entry ID. or "/" if @path's parent is the root directory
 * */
gchar *
g_vfs_gdata_file_get_parent_id_from_gvfs (const gchar *path)
{
	gchar	**entries_id_array, *entry_id;

	if (strcmp (path, "/") == 0)
	   return g_strdup ("/");

	entries_id_array = g_strsplit (path, "/", 0);
	entry_id = g_strdup (entries_id_array [g_strv_length (entries_id_array)-2]);

	if (g_strcmp0 (entry_id, "") == 0)
	{
		g_free (entry_id);
		entry_id = g_strdup ("/");
	}
	g_strfreev (entries_id_array);

	return entry_id;
}

/**
 * g_vfs_gdata_file_new_folder_from_gvfs:
 * @backend: the gdata backend this file is to be used on
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
 * An GDataDocumentsServiceError can also be set
 **/
GVfsGDataFile *
g_vfs_gdata_file_new_folder_from_gvfs (GVfsBackendGdocs *backend, const gchar *gvfs_path, GCancellable *cancellable, GError **error)
{
	GVfsGDataFile *folder;
   
	folder = g_vfs_gdata_file_new_from_gvfs (backend, gvfs_path, cancellable, error);
	if (*error != NULL)
	{
		if (folder != NULL)
			g_object_unref (folder);
		return NULL;
	}

	if (!g_vfs_gdata_file_is_folder (folder))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, _("%s is not a directory"), gvfs_path);
		g_object_unref (folder);
		return NULL;
	}

	return folder;
}
/**
 * g_vfs_gdata_file_new_from_gvfs:
 * @backend: the gdata backend this file is to be used on
 * @gvfs_path: gvfs path to create the file from
 * @cancellable: a GCancellable or %NULL
 * @error: a GError or %NULL
 *
 * Constructs a new #GVfsGDataFile representing the given gvfs path.
 * If this is the root directory, the GVfsGDataFile::gdata-entry will be %NULL
 *
 * Returns: a new GVfsGDataFile, o %NULL
 * If the file doesn't exit, a G_IO_ERROR_NOT_FOUND is set.
 * If there was an error trying to get the feed, the server error is set.
 * An GDataDocumentsServiceError can also be set 
 **/
GVfsGDataFile *
g_vfs_gdata_file_new_from_gvfs (GVfsBackendGdocs *backend, const gchar *gvfs_path, GCancellable *cancellable, GError **error)
{
	gchar					*entry_id;
	GType					*document_type;
	GDataDocumentsEntry		*entry;

	gboolean				entry_build = FALSE;
	GDataDocumentsService	*service = G_VFS_BACKEND_GDOCS (backend)->service;

	g_return_val_if_fail (G_VFS_IS_BACKEND_GDOCS (backend), NULL);
	g_return_val_if_fail (gvfs_path != NULL, NULL);

	entry_id = g_vfs_gdata_file_get_document_id_from_gvfs (gvfs_path);
	if (g_strcmp0 (entry_id, "/") == 0)
	{
		return g_object_new (G_VFS_TYPE_GDATA_FILE,
							 "backend", backend,
							 "gdata-entry", NULL,
							 NULL);
	}
	g_debug ("New from GVFS entry_id: %s\n", entry_id);

	/* if the GHashTable which make the link between an entry-id and a type is empty, we build it*/
	if (g_hash_table_size (backend->entries_type) == 0)
	{
		g_vfs_backend_gdocs_rebuild_entries_type (backend, cancellable, error);
		if (*error != NULL)
		{
			g_free (entry_id);
			return NULL;
		}
		entry_build = TRUE;
	}

	document_type = g_hash_table_lookup (backend->entries_type, entry_id);

	/* If the entry hasn't been found, we rebuild the GHashTable*/
	if (document_type == NULL && entry_build == FALSE)
	{
		g_vfs_backend_gdocs_rebuild_entries_type (backend, cancellable, error);
		if (*error != NULL)
		{
			g_free (entry_id);
			return NULL;
		}
		document_type = g_hash_table_lookup (backend->entries_type, entry_id);
	}

	if (document_type == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("%s not found a document"), gvfs_path);
		return NULL;
	}


	entry = gdata_documents_service_query_single_document (GDATA_DOCUMENTS_SERVICE (service), document_type, entry_id, cancellable, error);

	if (*error != NULL)
	{
		if (entry != NULL)
			g_object_unref (entry);
		return NULL;
	}

	return g_object_new (G_VFS_TYPE_GDATA_FILE, "backend", backend, "gdata-entry", GDATA_ENTRY (entry), "gvfs-path", gvfs_path, NULL);
}

/**
 * g_vfs_gdata_file_new_from_gdata:
 * @backend: the gdata backend this file is to be used on
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
	g_return_val_if_fail (GDATA_IS_ENTRY (gdata_entry), NULL);

	if (GDATA_IS_DOCUMENTS_ENTRY (gdata_entry))
		gvfs_path = gdata_documents_entry_get_path (GDATA_DOCUMENTS_ENTRY (gdata_entry));
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Not found a document");
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
	GVfsGDataFile	*parent_folder;
	gchar			*parent_entry_id, *parent_entry_pseudo_path;

	g_return_val_if_fail (G_VFS_IS_BACKEND_GDOCS (backend), NULL);
	g_return_val_if_fail (gvfs_path != NULL, NULL);

	parent_entry_id = g_vfs_gdata_file_get_parent_id_from_gvfs (gvfs_path);

	if (g_strcmp0 (parent_entry_id , "/") == 0)
	{
		g_free (parent_entry_id);
		return g_object_new (G_VFS_TYPE_GDATA_FILE,
							 "backend", backend,
							 "gdata-entry", NULL,
							 NULL);
	}

	parent_entry_pseudo_path = g_strconcat ("/", parent_entry_id, NULL);
	g_free (parent_entry_id);

	parent_folder =  g_vfs_gdata_file_new_folder_from_gvfs (backend, parent_entry_pseudo_path, cancellable, error);
	g_free (parent_entry_pseudo_path);

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
  g_return_val_if_fail (G_VFS_IS_GDATA_FILE (file), FALSE);

  return file->priv->gvfs_path[0] == '/' && file->priv->gvfs_path[1] == 0;
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

GFileInfo *
g_vfs_gdata_file_get_info (GVfsGDataFile *file, GFileInfo *info, GFileAttributeMatcher *matcher, GError **error)
{
	GTimeVal	t;
	GIcon		*icon;
	gchar		*content_type;
	GString		*display_name;
	const gchar *filename;

	g_return_val_if_fail (G_VFS_IS_GDATA_FILE (file), NULL);

	if (!G_IS_FILE_INFO (info))
		info = g_file_info_new();

	if (GDATA_IS_DOCUMENTS_ENTRY (file->priv->gdata_entry))
	{
		filename = gdata_documents_entry_get_document_id (GDATA_DOCUMENTS_ENTRY (file->priv->gdata_entry));
		g_file_info_set_name (info, filename);
		g_file_info_set_modification_time (info, &t);
	}
	else if (!g_vfs_gdata_file_is_root (file))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("%s not supported"), file->priv->gvfs_path);
		return NULL;
	}

	/* We set the content file type and file size if necessary*/
	if (g_vfs_gdata_file_is_folder (file))
	{
		g_file_info_set_file_type (info, G_FILE_TYPE_DIRECTORY);

		content_type = g_strdup ("inode/directory");
		if (g_strcmp0 (file->priv->gvfs_path, "/") == 0)
			icon = g_themed_icon_new ("folder-remote");
		else
			icon = g_themed_icon_new ("folder");
	}
	else
	{ 
		g_file_info_set_file_type (info, G_FILE_TYPE_REGULAR);

		/*Create the content type*/
		if (GDATA_IS_DOCUMENTS_SPREADSHEET (file->priv->gdata_entry))
			content_type = g_strdup ("application/x-vnd.oasis.opendocument.spreadsheet");
		else if (GDATA_IS_DOCUMENTS_TEXT (file->priv->gdata_entry))
			content_type = g_strdup ("application/vnd.oasis.opendocument.text");
		else if (GDATA_IS_DOCUMENTS_PRESENTATION (file->priv->gdata_entry))
			content_type = g_strdup ("application/vnd.ms-powerpoint");
		else
			content_type = g_content_type_guess (display_name->str, NULL, 0, NULL);

		/*We set the size as the maximum size we can upload on the server*/
		g_file_info_set_size (info, 1000);
	}

	g_file_info_set_content_type (info, content_type);

	if (g_vfs_gdata_file_is_root (file))
		return info;

	/* Set the display name corresponding to the GDataEntry::title parameter*/
	display_name = g_string_new (gdata_entry_get_title (file->priv->gdata_entry));
	/*We can't name a file with Slashes*/
	convert_slashes (display_name->str);
	if (*display_name->str == '.')
		g_file_info_set_is_hidden (info, TRUE);

	if (g_strstr_len (display_name->str, strlen (display_name), "\357\277\275") != NULL)
		g_string_append (display_name, " (invalid encoding)");
	else
	{
		/*Set the extensions*/
		if (GDATA_IS_DOCUMENTS_SPREADSHEET (file->priv->gdata_entry))
		{
			if (!g_str_has_suffix (display_name->str, ".ods"))
				g_string_append (display_name, ".ods");
		}
		else if (GDATA_IS_DOCUMENTS_TEXT (file->priv->gdata_entry))
		{
			if (!g_str_has_suffix (display_name->str, ".odt"))
				g_string_append (display_name, ".odt");
		}
		else if (GDATA_IS_DOCUMENTS_PRESENTATION (file->priv->gdata_entry))
		{
			if (!g_str_has_suffix (display_name->str, ".ppt"))
				g_string_append (display_name, ".ppt");
		}
	}
	g_file_info_set_display_name (info, display_name->str);

	if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE) ||
			g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_STANDARD_ICON))
	{
		icon = NULL;

		if (content_type)
		{
			icon = g_content_type_get_icon (content_type);
			g_file_info_set_content_type (info, content_type);
		}
		if (icon == NULL)
			icon = g_themed_icon_new ("text-x-generic");

		g_file_info_set_icon (info, icon);
		g_object_unref (icon);
	}
	g_free (content_type);

	if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_ETAG_VALUE))
	{
		const gchar *etag = gdata_entry_get_etag (file->priv->gdata_entry);
		g_file_info_set_attribute_string (info, G_FILE_ATTRIBUTE_ETAG_VALUE, etag);
	}
	g_string_free (display_name, TRUE);

	return info;
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
	g_return_val_if_fail (G_VFS_IS_GDATA_FILE (b), FALSE);

	if (g_strcmp0 (a->priv->gvfs_path, b->priv->gvfs_path) == 0)
		return TRUE;
	else
		return FALSE;
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

/* g_vfs_gdata_file_get_download_uri
 *
 * @file: a GVfsGDataFile file
 * @cancellable: optional GCancellable object, or NULL
 * @error :	a GError, or NULL
 *
 * Return: the downloading URI of th file.
 */
gchar *
g_vfs_gdata_file_get_download_uri (GVfsGDataFile *file, GCancellable *cancellable, GError **error)
{
	
	GDataDocumentsEntry *entry;

	g_return_val_if_fail (G_VFS_IS_GDATA_FILE (file), NULL);

	entry = GDATA_DOCUMENTS_ENTRY (g_vfs_gdata_file_get_gdata_entry (file));

	if (g_vfs_gdata_file_is_folder (file))
	{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY,
					_("%s is a directory"), g_vfs_gdata_file_get_gvfs_path (file));
			return NULL;
	}
	else if (GDATA_IS_DOCUMENTS_SPREADSHEET (entry))
	{
		return gdata_documents_spreadsheet_get_download_uri (GDATA_DOCUMENTS_SPREADSHEET (entry), GDATA_DOCUMENTS_SPREADSHEET_ODS, -1);
	}
	else if (GDATA_IS_DOCUMENTS_TEXT (entry))
	{
		return gdata_documents_text_get_download_uri (GDATA_DOCUMENTS_TEXT (entry), GDATA_DOCUMENTS_TEXT_ODT);
	}
	else if (GDATA_IS_DOCUMENTS_PRESENTATION (entry))
	{
		return gdata_documents_presentation_get_download_uri (GDATA_DOCUMENTS_PRESENTATION (entry), GDATA_DOCUMENTS_PRESENTATION_PPT);
	}
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				_("%s is not a supported document type"), g_vfs_gdata_file_get_gvfs_path (file));
		g_object_unref (file);
		return NULL;
	}
}

/* g_vfs_gdata_file_download_file
 *
 * @file: a GVfsGDataFile file
 * @content_type: return location for the document's content type, or NULL; free with g_free()
 * @export_format: the format in which the presentation should be exported
 * @destination_directory :	the directory into which the presentation file should be saved
 * @replace_file_if_exists : %TRUE if the file should be replaced if it already exists, %FALSE otherwise
 * @download_folders: %TRUE if you want to download all the directory three, otherwise %FALSE
 * @cancellable: optional GCancellable object, or NULL
 * @error :	a GError, or NULL
 */
GFile *
g_vfs_gdata_file_download_file (GVfsGDataFile *file, gchar **content_type, const gchar *local_path,
								gboolean replace_file_if_exists, gboolean download_folders, GCancellable *cancellable,
								GError **error)
{
	GDataDocumentsFeed *tmp_feed = NULL;
	GDataDocumentsEntry *entry;
	GDataDocumentsService *service;
	GFile *new_file;

	g_return_val_if_fail (G_VFS_IS_GDATA_FILE (file), NULL);
	g_return_val_if_fail (local_path != NULL, NULL);

	entry = GDATA_DOCUMENTS_ENTRY (g_vfs_gdata_file_get_gdata_entry (file));
	service = G_VFS_BACKEND_GDOCS (file->priv->backend)->service;
	if (g_vfs_gdata_file_is_folder (file))
	{
		if (download_folders == FALSE)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY,
					_("%s is a directory"), g_vfs_gdata_file_get_gvfs_path (file));
			return NULL;
		}
		else
		{
			GList *element = NULL;
			const gchar *folder_id = gdata_documents_entry_get_document_id (GDATA_DOCUMENTS_ENTRY (entry));
			GDataDocumentsQuery *query;
			gboolean is_root = g_vfs_gdata_file_is_root (file);

			new_file = g_file_new_for_path (local_path);
			g_file_make_directory (new_file, cancellable, error);
			if (*error != NULL)
			{
				if (new_file != NULL)
					g_object_unref (new_file);
				return NULL;
			}

			/*We check if the folder_id is the root directory*/
			if (!is_root)
			{
				query  = gdata_documents_query_new (NULL);
				gdata_documents_query_set_folder_id (query, folder_id);
			}
			tmp_feed = gdata_documents_service_query_documents (service, query, cancellable, NULL, NULL, error);
			if (query != NULL)
				g_object_unref (query);
			if (*error != NULL)
			{
				if (tmp_feed != NULL)
					g_object_unref (tmp_feed);
				return NULL;
			}
			for (element = gdata_feed_get_entries (GDATA_FEED (tmp_feed)); element != NULL; element = element->next)
			{
				gchar *new_destination_directory = g_strconcat (local_path, gdata_documents_entry_get_document_id (element->data), NULL);
				/* If it's the root directory, we check that the document is not contained in another folder*/
				if (is_root)
				{
					gchar *path = gdata_documents_entry_get_path (GDATA_DOCUMENTS_ENTRY (element->data));
					GFile *test_file = g_file_new_for_path (path);
					GFile *parent = g_file_get_parent (test_file);
					if (parent != NULL)
						g_object_unref (parent);
					else
						g_vfs_gdata_file_download_file (file, content_type, new_destination_directory, replace_file_if_exists, TRUE, cancellable, error);
					g_object_unref (test_file);
					g_free (path);
				}
				else
					g_vfs_gdata_file_download_file (file, content_type, new_destination_directory, replace_file_if_exists, TRUE, cancellable, error);

				if (*error != NULL)
				{
					if (tmp_feed != NULL)
						g_object_unref (tmp_feed);
					return NULL;
				}
			}
		}
	}
	else if (GDATA_IS_DOCUMENTS_SPREADSHEET (entry))
	{
		new_file = g_file_new_for_path (local_path);
		new_file = gdata_documents_spreadsheet_download_document (GDATA_DOCUMENTS_SPREADSHEET (entry), service, content_type, GDATA_DOCUMENTS_SPREADSHEET_ODS,
																  -1, new_file, replace_file_if_exists, cancellable, error);
	}
	else if (GDATA_IS_DOCUMENTS_TEXT (entry))
	{
		new_file = g_file_new_for_path (local_path);
		new_file = gdata_documents_text_download_document (GDATA_DOCUMENTS_TEXT (entry), service, content_type, GDATA_DOCUMENTS_TEXT_ODT,
														   new_file, replace_file_if_exists, cancellable, error);
	}
	else if (GDATA_IS_DOCUMENTS_PRESENTATION (entry))
	{
		new_file = g_file_new_for_path (local_path);
		new_file = gdata_documents_presentation_download_document (GDATA_DOCUMENTS_PRESENTATION (entry), service, content_type,
																   GDATA_DOCUMENTS_PRESENTATION_PPT, new_file, replace_file_if_exists,
																   cancellable, error);
	}
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("%s is not a supported document type"),
					 g_vfs_gdata_file_get_gvfs_path (file));
		g_object_unref (file);
		return NULL;
	}

	return new_file;
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

	g_type_class_add_private (klass, sizeof (GVfsGDataFile));

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
														  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	/*
	 * GVfsGDataFile:gdata-entry
	 *
	 * The #GDataEntry corresponding.
	 **/
	g_object_class_install_property (gobject_class, PROP_GDATA_ENTRY,
									 g_param_spec_object ("gdata-entry",
														  "gdata-entry", "The #GDataEntry corresponding",
														  GDATA_TYPE_ENTRY,
														  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	/**
	 * GDataGDEmailAddress:label:
	 *
	 * A simple string corresponding to the gvfs path of the class.
	 **/
	g_object_class_install_property (gobject_class, PROP_GVFS_PATH,
				g_param_spec_string ("gvfs-path",
									 "Gvfs path", "A simple string corresponding to the gvfs path of the class.",
									 "/",
									 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
g_vfs_gdata_file_init (GVfsGDataFile *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, G_VFS_TYPE_GDATA_FILE, GVfsGDataFilePrivate);
}
