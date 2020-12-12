/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <mousepad/mousepad-private.h>
#include <mousepad/mousepad-file.h>
#include <mousepad/mousepad-util.h>



enum
{
  /* EXTERNALLY_MODIFIED, */
  LOCATION_CHANGED,
  READONLY_CHANGED,
  LAST_SIGNAL
};


/* GObject virtual functions */
static void mousepad_file_finalize      (GObject      *object);

/* MousepadFile own functions */
static void mousepad_file_set_read_only (MousepadFile *file,
                                         gboolean      readonly);



struct _MousepadFileClass
{
  GObjectClass __parent__;
};

struct _MousepadFile
{
  GObject             __parent__;

  /* the text buffer this file belongs to */
  GtkTextBuffer      *buffer;

  /* location */
  GFile              *location;
  gboolean            temporary;

  /* encoding of the file */
  MousepadEncoding    encoding;

  /* line ending of the file */
  MousepadLineEnding  line_ending;

  /* our last modification time */
  gchar              *etag;

  /* if file is read-only */
  gboolean            readonly;

  /* whether we write the bom at the start of the file */
  gboolean            write_bom;

  /* whether the filetype has been set by user or we should guess it */
  gboolean            user_set_language;
};



static guint file_signals[LAST_SIGNAL];



G_DEFINE_TYPE (MousepadFile, mousepad_file, G_TYPE_OBJECT)



static void
mousepad_file_class_init (MousepadFileClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = mousepad_file_finalize;

#if 0
  /* TODO implement this signal */
  file_signals[EXTERNALLY_MODIFIED] =
    g_signal_new (I_("externally-modified"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
#endif

  file_signals[READONLY_CHANGED] =
    g_signal_new (I_("readonly-changed"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  file_signals[LOCATION_CHANGED] =
    g_signal_new (I_("location-changed"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, G_TYPE_FILE);
}



static void
mousepad_file_init (MousepadFile *file)
{
  /* initialize */
  file->location          = NULL;
  file->temporary         = FALSE;
  file->encoding          = MOUSEPAD_ENCODING_UTF_8;
#ifdef G_OS_WIN32
  file->line_ending       = MOUSEPAD_EOL_DOS;
#else
  file->line_ending       = MOUSEPAD_EOL_UNIX;
#endif
  file->readonly          = TRUE;
  file->etag              = NULL;
  file->write_bom         = FALSE;
  file->user_set_language = FALSE;
}



static void
mousepad_file_finalize (GObject *object)
{
  MousepadFile *file = MOUSEPAD_FILE (object);

  /* cleanup */
  g_free (file->etag);

  if (G_IS_FILE (file->location))
    g_object_unref (file->location);

  if (GTK_IS_TEXT_BUFFER (file->buffer))
    g_object_unref (file->buffer);

  (*G_OBJECT_CLASS (mousepad_file_parent_class)->finalize) (object);
}



MousepadFile *
mousepad_file_new (GtkTextBuffer *buffer)
{
  MousepadFile *file;

  g_return_val_if_fail (GTK_IS_TEXT_BUFFER (buffer), NULL);

  file = g_object_new (MOUSEPAD_TYPE_FILE, NULL);

  /* set the buffer */
  file->buffer = GTK_TEXT_BUFFER (g_object_ref (buffer));

  return file;
}



void
mousepad_file_set_location (MousepadFile *file,
                            GFile        *location,
                            gboolean      real)
{
  g_return_if_fail (MOUSEPAD_IS_FILE (file));

  /* set location */
  if (file->location == NULL && location != NULL)
    file->location = g_object_ref (location);
  /* reset location */
  else if (file->location != NULL && location == NULL)
    {
      g_object_unref (file->location);
      file->location = NULL;
    }
  /* update location */
  else if (file->location != NULL && location != NULL
           && ! g_file_equal (file->location, location))
    {
      g_object_unref (file->location);
      file->location = g_object_ref (location);
    }

  /* not a virtual change, such as when trying to save as */
  if (real)
    {
      /* this is a definitve location */
      file->temporary = FALSE;

      /* send a signal that the name has been changed */
      g_signal_emit (file, file_signals[LOCATION_CHANGED], 0, file->location);
    }
  /* toggle location state */
  else
    file->temporary = ! file->temporary;
}



GFile *
mousepad_file_get_location (MousepadFile *file)
{
  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), NULL);

  return file->location;
}



gboolean
mousepad_file_location_is_set (MousepadFile *file)
{
  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), FALSE);

  return G_IS_FILE (file->location);
}



const gchar *
mousepad_file_get_path (MousepadFile *file)
{
  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), NULL);

  return mousepad_util_get_path (file->location);
}



gchar *
mousepad_file_get_uri (MousepadFile *file)
{
  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), NULL);

  return g_file_get_uri (file->location);
}



static void
mousepad_file_set_read_only (MousepadFile *file,
                             gboolean      readonly)
{
  g_return_if_fail (MOUSEPAD_IS_FILE (file));

  if (G_LIKELY (file->readonly != readonly))
    {
      /* store new value */
      file->readonly = readonly;

      /* emit signal */
      g_signal_emit (file, file_signals[READONLY_CHANGED], 0, readonly);
    }
}



gboolean
mousepad_file_get_read_only (MousepadFile *file)
{
  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), FALSE);

  return file->location ? file->readonly : FALSE;
}



void
mousepad_file_set_encoding (MousepadFile     *file,
                            MousepadEncoding  encoding)
{
  g_return_if_fail (MOUSEPAD_IS_FILE (file));

  /* set new encoding */
  file->encoding = encoding;
}



MousepadEncoding
mousepad_file_get_encoding (MousepadFile *file)
{
  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), MOUSEPAD_ENCODING_NONE);

  return file->encoding;
}



void
mousepad_file_set_write_bom (MousepadFile *file,
                             gboolean      write_bom)
{
  g_return_if_fail (MOUSEPAD_IS_FILE (file));
  g_return_if_fail (mousepad_encoding_is_unicode (file->encoding));

  /* set new value */
  file->write_bom = write_bom;
}



gboolean
mousepad_file_get_write_bom (MousepadFile *file,
                             gboolean     *sensitive)
{
  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), FALSE);

  /* return if we can write a bom */
  if (G_LIKELY (sensitive))
    *sensitive = mousepad_encoding_is_unicode (file->encoding);

  return file->write_bom;
}



void
mousepad_file_set_line_ending (MousepadFile       *file,
                               MousepadLineEnding  line_ending)
{
  g_return_if_fail (MOUSEPAD_IS_FILE (file));

  file->line_ending = line_ending;
}



MousepadLineEnding
mousepad_file_get_line_ending (MousepadFile *file)
{
  return file->line_ending;
}



static void
mousepad_file_set_language (MousepadFile *file)
{
  GtkSourceLanguage *language;
  GtkTextIter        start, end;
  gchar             *data, *content_type, *basename;
  gboolean           result_uncertain;

  g_return_if_fail (file->location != NULL);

  gtk_text_buffer_get_start_iter (file->buffer, &start);
  end = start;
  gtk_text_iter_forward_chars (&end, 255);
  data = gtk_text_buffer_get_text (file->buffer, &start, &end, TRUE);

  content_type = g_content_type_guess (mousepad_file_get_path (file), (const guchar *) data,
                                       strlen (data), &result_uncertain);
  basename = g_file_get_basename (file->location);
  language = gtk_source_language_manager_guess_language (gtk_source_language_manager_get_default (),
                                                         basename,
                                                         result_uncertain ? NULL : content_type);

  gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (file->buffer), language);

  g_free (data);
  g_free (basename);
  g_free (content_type);
}



void
mousepad_file_set_user_set_language (MousepadFile *file,
                                     gboolean      set_by_user)
{
  g_return_if_fail (MOUSEPAD_IS_FILE (file));

  file->user_set_language = set_by_user;
}



static MousepadEncoding
mousepad_file_encoding_read_bom (const gchar *contents,
                                 gsize        length,
                                 gsize       *bom_length)
{
  const guchar     *bom = (const guchar *) contents;
  MousepadEncoding  encoding = MOUSEPAD_ENCODING_NONE;
  gsize             bytes = 0;

  g_return_val_if_fail (contents != NULL && length > 0, MOUSEPAD_ENCODING_NONE);
  g_return_val_if_fail (contents != NULL && length > 0, MOUSEPAD_ENCODING_NONE);

  switch (bom[0])
    {
      case 0xef:
        if (length >= 3 && bom[1] == 0xbb && bom[2] == 0xbf)
          {
            bytes = 3;
            encoding = MOUSEPAD_ENCODING_UTF_8;
          }
        break;

      case 0x00:
        if (length >= 4 && bom[1] == 0x00 && bom[2] == 0xfe && bom[3] == 0xff)
          {
            bytes = 4;
            encoding = MOUSEPAD_ENCODING_UTF_32BE;
          }
        break;

      case 0xff:
        if (length >= 4 && bom[1] == 0xfe && bom[2] == 0x00 && bom[3] == 0x00)
          {
            bytes = 4;
            encoding = MOUSEPAD_ENCODING_UTF_32LE;
          }
        else if (length >= 2 && bom[1] == 0xfe)
          {
            bytes = 2;
            encoding = MOUSEPAD_ENCODING_UTF_16LE;
          }
        break;

      case 0x2b:
        if (length >= 4 && (bom[0] == 0x2b && bom[1] == 0x2f && bom[2] == 0x76) &&
            (bom[3] == 0x38 || bom[3] == 0x39 || bom[3] == 0x2b || bom[3] == 0x2f))
          {
            bytes = 4;
            encoding = MOUSEPAD_ENCODING_UTF_7;
          }
        break;

      case 0xfe:
        if (length >= 2 && bom[1] == 0xff)
          {
            bytes = 2;
            encoding = MOUSEPAD_ENCODING_UTF_16BE;
          }
        break;
    }

  if (bom_length)
    *bom_length = bytes;

  return encoding;
}



gint
mousepad_file_open (MousepadFile  *file,
                    gboolean       must_exist,
                    GError       **error)
{
  MousepadEncoding  bom_encoding;
  GtkTextIter       start, end;
  GFileInfo        *fileinfo;
  const gchar      *charset, *endc, *n, *m;
  gchar            *contents = NULL, *temp;
  gsize             file_size, written, bom_length;
  gint              retval = ERROR_READING_FAILED;

  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), FALSE);
  g_return_val_if_fail (GTK_IS_TEXT_BUFFER (file->buffer), FALSE);
  g_return_val_if_fail (file->location != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* if the file does not exist and this is allowed, no problem */
  if (! g_file_load_contents (file->location, NULL, &contents, &file_size, &(file->etag), error)
      && g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) && ! must_exist)
    {
      mousepad_file_set_read_only (file, FALSE);
      g_clear_error (error);

      return 0;
    }
  /* the file was sucessfully loaded */
  else if (G_LIKELY (error == NULL || *error == NULL))
    {
      /* make sure the buffer is empty, in particular for reloading */
      gtk_text_buffer_get_bounds (file->buffer, &start, &end);
      gtk_text_buffer_delete (file->buffer, &start, &end);

      if (G_LIKELY (file_size > 0))
        {
          /* detect if there is a bom with the encoding type */
          bom_encoding = mousepad_file_encoding_read_bom (contents, file_size, &bom_length);
          if (G_UNLIKELY (bom_encoding != MOUSEPAD_ENCODING_NONE))
            {
              /* we've found a valid bom at the start of the contents */
              file->write_bom = TRUE;

              /* advance the contents offset and decrease size */
              temp = g_strdup (contents + bom_length);
              g_free (contents);
              contents = temp;
              file_size -= bom_length;

              /* set the detected encoding */
              file->encoding = bom_encoding;
            }

          /* leave when the contents is not utf-8 valid */
          if (G_LIKELY (file->encoding == MOUSEPAD_ENCODING_UTF_8)
              && g_utf8_validate (contents, file_size, &endc) == FALSE)
            {
              /* set return value */
              retval = ERROR_NOT_UTF8_VALID;

              /* set an error */
              g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                           _("Invalid byte sequence in conversion input"));

              goto failed;
            }
          else
            {
              if (file->encoding != MOUSEPAD_ENCODING_UTF_8_FORCED)
                {
                  /* get the encoding charset */
                  charset = mousepad_encoding_get_charset (file->encoding);

                  /* convert the contents */
                  temp = g_convert (contents, file_size, "UTF-8", charset, NULL, &written, error);

                  /* check if the encoding succeed */
                  if (G_UNLIKELY (temp == NULL))
                    {
                      /* set return value */
                      retval = ERROR_CONVERTING_FAILED;

                      goto failed;
                    }

                  /* set new values */
                  g_free (contents);
                  contents = temp;
                  file_size = written;
                }

              /* force UTF-8 encoding validity and update location for end of valid data */
              temp = g_utf8_make_valid (contents, file_size);
              g_free (contents);
              contents = temp;
              g_utf8_validate (contents, -1, &endc);
            }

          /* detect the line ending, based on the first eol we match */
          for (n = contents; n < endc; n = g_utf8_next_char (n))
            {
              if (G_LIKELY (*n == '\n'))
                {
                  /* set unix line ending */
                  file->line_ending = MOUSEPAD_EOL_UNIX;

                  break;
                }
              else if (*n == '\r')
                {
                  /* get next character */
                  n = g_utf8_next_char (n);

                  /* set dos or mac line ending */
                  file->line_ending = (*n == '\n') ? MOUSEPAD_EOL_DOS : MOUSEPAD_EOL_MAC;

                  break;
                }
            }

          /* text view doesn't expect a line ending at end of last line, but Unix and Mac files do */
          if ((n = g_utf8_find_prev_char (contents, endc))
              && (*n == '\r' || (*n == '\n' && (! (n = g_utf8_find_prev_char (contents, n))
                                                || *n != '\r'))))
            endc--;

          /* get the iter at the beginning of the document */
          gtk_text_buffer_get_start_iter (file->buffer, &start);

          /* insert the file contents in the buffer (for documents with cr line ending) */
          for (n = m = contents; n < endc; n = g_utf8_next_char (n))
            {
              if (G_UNLIKELY (*n == '\r'))
                {
                  /* insert the text in the buffer */
                  if (G_LIKELY (n - m > 0))
                    gtk_text_buffer_insert (file->buffer, &start, m, n - m);

                  /* advance the offset */
                  m = g_utf8_next_char (n);

                  /* insert a new line when the document is not cr+lf */
                  if (*m != '\n')
                    gtk_text_buffer_insert (file->buffer, &start, "\n", 1);
                }
            }

          /* insert the remaining part, or everything for lf line ending */
          if (G_LIKELY (n - m > 0))
            gtk_text_buffer_insert (file->buffer, &start, m, n - m);

          /* get the start iter */
          gtk_text_buffer_get_start_iter (file->buffer, &start);

          /* set the cursor to the beginning of the document */
          gtk_text_buffer_place_cursor (file->buffer, &start);
        }

      /* assume everything when file */
      retval = 0;

      /* store the file status */
      if (G_LIKELY (! file->temporary))
        if (G_LIKELY (fileinfo = g_file_query_info (file->location, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE,
                                                    G_FILE_QUERY_INFO_NONE, NULL, error)))
          {
            mousepad_file_set_read_only (file,
              ! g_file_info_get_attribute_boolean (fileinfo, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE));
            g_object_unref (fileinfo);
          }
        /* set return value */
        else
          retval = ERROR_FILE_STATUS_FAILED;
      /* this is a new document with content from a template */
      else
        {
          g_free (file->etag);
          file->etag = NULL;
          mousepad_file_set_read_only (file, FALSE);
        }

      failed:

      /* make sure the buffer is empty if we did not succeed */
      if (G_UNLIKELY (retval != 0))
        {
          gtk_text_buffer_get_bounds (file->buffer, &start, &end);
          gtk_text_buffer_delete (file->buffer, &start, &end);
        }

      /* cleanup */
      g_free (contents);

      /* guess and set the file's filetype/language */
      mousepad_file_set_language (file);

      /* this does not count as a modified buffer */
      gtk_text_buffer_set_modified (file->buffer, FALSE);
    }

  return retval;
}



gboolean
mousepad_file_save (MousepadFile  *file,
                    gboolean       forced,
                    GError       **error)
{
  GtkTextIter   start, end;
  const gchar  *charset;
  gchar        *contents, *p, *encoded, *etag = NULL;
  gchar       **chunks;
  gint          length;
  gsize         written;
  gboolean      succeed = FALSE;

  g_return_val_if_fail (MOUSEPAD_IS_FILE (file), FALSE);
  g_return_val_if_fail (GTK_IS_TEXT_BUFFER (file->buffer), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (file->location != NULL, FALSE);

  /* get the buffer bounds */
  gtk_text_buffer_get_bounds (file->buffer, &start, &end);

  /* get the buffer contents */
  contents = gtk_text_buffer_get_slice (file->buffer, &start, &end, TRUE);

  if (G_LIKELY (contents))
    {
      /* get the content length */
      length = strlen (contents);

      /* handle line endings */
      if (file->line_ending == MOUSEPAD_EOL_MAC)
        {
          /* replace the unix with a mac line ending */
          for (p = contents; *p != '\0'; p++)
            if (G_UNLIKELY (*p == '\n'))
              *p = '\r';
        }
      else if (file->line_ending == MOUSEPAD_EOL_DOS)
        {
          /* split the contents into chunks */
          chunks = g_strsplit (contents, "\n", -1);

          /* cleanup */
          g_free (contents);

          /* join the chunks with dos line endings in between */
          contents = g_strjoinv ("\r\n", chunks);

          /* cleanup */
          g_strfreev (chunks);

          /* new contents length */
          length = strlen (contents);
        }

      /* add and utf-8 bom at the start of the contents if needed */
      if (file->write_bom && mousepad_encoding_is_unicode (file->encoding))
        {
          /* realloc the contents string. +1 in case we append extra line ending */
          contents = g_realloc (contents, length + 5);

          /* move the existing contents 3 bytes */
          memmove (contents + 3, contents, length + 1);

          /* write an utf-8 bom at the start of the contents */
          contents[0] = 0xef;
          contents[1] = 0xbb;
          contents[2] = 0xbf;

          /* increase the length */
          length += 3;
        }

      /* text view doesn't expect a line ending at end of last line, but Unix and Mac files do */
      if (file->line_ending != MOUSEPAD_EOL_DOS && 0 < length)
        {
          /* realloc contents. does nothing if realloc above already resized */
          contents = g_realloc (contents, length + 2);

          contents[length] = file->line_ending == MOUSEPAD_EOL_MAC ? '\r' : '\n';
          contents[length + 1] = '\0';

          length++;
        }

      /* convert to the encoding if set */
      if (G_UNLIKELY (file->encoding != MOUSEPAD_ENCODING_UTF_8))
        {
          /* get the charset */
          charset = mousepad_encoding_get_charset (file->encoding);
          if (G_UNLIKELY (charset == NULL))
            {
              /* set an error */
              g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_NO_CONVERSION,
                           _("Unsupported character set"));

              goto failed;
            }

          /* convert the content to the user encoding */
          encoded = g_convert (contents, length, charset, "UTF-8", NULL, &written, error);

          /* return if nothing was encoded */
          if (G_UNLIKELY (encoded == NULL))
            goto failed;

          /* cleanup */
          g_free (contents);

          /* set the new contents */
          contents = encoded;
          length = written;
        }

      /* write the buffer to the file */
      if (g_file_replace_contents (file->location, contents, length,
                                   (file->temporary || forced) ? NULL : file->etag,
                                   FALSE, G_FILE_CREATE_NONE, &etag, NULL, error))
        {
          g_free (file->etag);
          file->etag = etag;
        }
      else
        goto failed;

      /* everything has been saved */
      gtk_text_buffer_set_modified (file->buffer, FALSE);

      /* we saved succesfully */
      mousepad_file_set_read_only (file, FALSE);

      /* if the user hasn't set the filetype, try and re-guess it now
       * that we have a new location to go by */
      if (! file->user_set_language)
        mousepad_file_set_language (file);

      /* everything went file */
      succeed = TRUE;

      failed:

      /* cleanup */
      g_free (contents);
    }

  return succeed;
}
