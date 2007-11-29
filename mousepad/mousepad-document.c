/* $Id$ */
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include <mousepad/mousepad-private.h>
#include <mousepad/mousepad-util.h>
#include <mousepad/mousepad-document.h>
#include <mousepad/mousepad-marshal.h>
#include <mousepad/mousepad-view.h>
#include <mousepad/mousepad-undo.h>
#include <mousepad/mousepad-preferences.h>
#include <mousepad/mousepad-window.h>



#define MOUSEPAD_DOCUMENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MOUSEPAD_TYPE_DOCUMENT, MousepadDocumentPrivate))



static void      mousepad_document_class_init              (MousepadDocumentClass  *klass);
static void      mousepad_document_init                    (MousepadDocument       *document);
static void      mousepad_document_finalize                (GObject                *object);
static void      mousepad_document_notify_cursor_position  (GtkTextBuffer          *buffer,
                                                            GParamSpec             *pspec,
                                                            MousepadDocument       *document);
static void      mousepad_document_toggle_overwrite        (GtkTextView            *textview,
                                                            GParamSpec             *pspec,
                                                            MousepadDocument       *document);
static void      mousepad_document_drag_data_received      (GtkWidget              *widget,
                                                            GdkDragContext         *context,
                                                            gint                    x,
                                                            gint                    y,
                                                            GtkSelectionData       *selection_data,
                                                            guint                   info,
                                                            guint                   time,
                                                            MousepadDocument       *document);
static void      mousepad_document_filename_changed        (MousepadDocument       *document,
                                                            const gchar            *filename);
static void      mousepad_document_tab_button_clicked      (GtkWidget              *widget,
                                                            MousepadDocument       *document);


enum
{
  CLOSE_TAB,
  CURSOR_CHANGED,
  OVERWRITE_CHANGED,
  LAST_SIGNAL,
};

struct _MousepadDocumentClass
{
  GtkScrolledWindowClass __parent__;
};

struct _MousepadDocumentPrivate
{
  GtkScrolledWindow __parent__;

  /* the tab label and ebox */
  GtkWidget           *ebox;
  GtkWidget           *label;

  /* utf-8 valid document names */
  gchar               *utf8_filename;
  gchar               *utf8_basename;

  /* settings */
  guint                word_wrap : 1;
};



static GObjectClass *mousepad_document_parent_class;
static guint         document_signals[LAST_SIGNAL];



MousepadDocument *
mousepad_document_new (void)
{
  return g_object_new (MOUSEPAD_TYPE_DOCUMENT, NULL);
}



GType
mousepad_document_get_type (void)
{
  static GType type = G_TYPE_INVALID;

  if (G_UNLIKELY (type == G_TYPE_INVALID))
    {
      type = g_type_register_static_simple (GTK_TYPE_SCROLLED_WINDOW,
                                            I_("MousepadDocument"),
                                            sizeof (MousepadDocumentClass),
                                            (GClassInitFunc) mousepad_document_class_init,
                                            sizeof (MousepadDocument),
                                            (GInstanceInitFunc) mousepad_document_init,
                                            0);
    }

  return type;
}



static void
mousepad_document_class_init (MousepadDocumentClass *klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (MousepadDocumentPrivate));

  mousepad_document_parent_class = g_type_class_peek_parent (klass);

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = mousepad_document_finalize;

  document_signals[CLOSE_TAB] =
    g_signal_new (I_("close-tab"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_NO_HOOKS,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  document_signals[CURSOR_CHANGED] =
    g_signal_new (I_("cursor-changed"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_NO_HOOKS,
                  0, NULL, NULL,
                  _mousepad_marshal_VOID__INT_INT_INT,
                  G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

  document_signals[OVERWRITE_CHANGED] =
    g_signal_new (I_("overwrite-changed"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_NO_HOOKS,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}



static void
mousepad_document_init (MousepadDocument *document)
{
  GtkTargetList       *target_list;
  gboolean             word_wrap, auto_indent, line_numbers, insert_spaces;
  gchar               *font_name;
  gint                 tab_size;
  MousepadPreferences *preferences;

  /* private structure */
  document->priv = MOUSEPAD_DOCUMENT_GET_PRIVATE (document);

  /* initialize the variables */
  document->priv->utf8_filename = NULL;
  document->priv->utf8_basename = NULL;

  /* setup the scolled window */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (document), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (document), GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (document), NULL);
  gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (document), NULL);

  /* create a textbuffer */
  document->buffer = gtk_text_buffer_new (NULL);

  /* initialize the file */
  document->file = mousepad_file_new (document->buffer);

  /* connect signals to the file */
  g_signal_connect_swapped (G_OBJECT (document->file), "filename-changed", G_CALLBACK (mousepad_document_filename_changed), document);

  /* initialize the undo manager */
  document->undo = mousepad_undo_new (document->buffer);

  /* create the highlight tag */
  document->tag = gtk_text_buffer_create_tag (document->buffer, NULL, "background", "#ffff78", NULL);

  /* setup the textview */
  document->textview = g_object_new (MOUSEPAD_TYPE_VIEW, "buffer", document->buffer, NULL);
  gtk_container_add (GTK_CONTAINER (document), GTK_WIDGET (document->textview));
  gtk_widget_show (GTK_WIDGET (document->textview));

  /* also allow dropping of uris and tabs in the textview */
  target_list = gtk_drag_dest_get_target_list (GTK_WIDGET (document->textview));
  gtk_target_list_add_table (target_list, drop_targets, G_N_ELEMENTS (drop_targets));

  /* preferences */
  preferences = mousepad_preferences_get ();

  /* read all the default settings */
  g_object_get (G_OBJECT (preferences),
                "view-word-wrap", &word_wrap,
                "view-line-numbers", &line_numbers,
                "view-auto-indent", &auto_indent,
                "view-font-name", &font_name,
                "view-tab-size", &tab_size,
                "view-insert-spaces", &insert_spaces,
                NULL);

  /* release the preferences */
  g_object_unref (G_OBJECT (preferences));

  /* set all the settings */
  mousepad_document_set_word_wrap (document, word_wrap);
  mousepad_document_set_font (document, font_name);
  mousepad_view_set_line_numbers (document->textview, line_numbers);
  mousepad_view_set_auto_indent (document->textview, auto_indent);
  mousepad_view_set_tab_size (document->textview, tab_size);
  mousepad_view_set_insert_spaces (document->textview, insert_spaces);

  /* cleanup */
  g_free (font_name);

  /* attach signals to the text view and buffer */
  g_signal_connect (G_OBJECT (document->buffer), "notify::cursor-position", G_CALLBACK (mousepad_document_notify_cursor_position), document);
  g_signal_connect (G_OBJECT (document->textview), "notify::overwrite", G_CALLBACK (mousepad_document_toggle_overwrite), document);
  g_signal_connect (G_OBJECT (document->textview), "drag-data-received", G_CALLBACK (mousepad_document_drag_data_received), document);
}



static void
mousepad_document_finalize (GObject *object)
{
  MousepadDocument *document = MOUSEPAD_DOCUMENT (object);

  /* cleanup */
  g_free (document->priv->utf8_filename);
  g_free (document->priv->utf8_basename);

  /* release the undo manager */
  g_object_unref (G_OBJECT (document->undo));

  /* release the file */
  g_object_unref (G_OBJECT (document->file));

  /* release the buffer reference */
  g_object_unref (G_OBJECT (document->buffer));

  (*G_OBJECT_CLASS (mousepad_document_parent_class)->finalize) (object);
}



static void
mousepad_document_notify_cursor_position (GtkTextBuffer    *buffer,
                                          GParamSpec       *pspec,
                                          MousepadDocument *document)
{
  GtkTextIter iter;
  guint       line, column, selection;
  gint        tab_size;

  _mousepad_return_if_fail (GTK_IS_TEXT_BUFFER (buffer));
  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));

  /* get the current iter position */
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, gtk_text_buffer_get_insert (buffer));

  /* get the current line number */
  line = gtk_text_iter_get_line (&iter) + 1;

  /* get the tab size */
  tab_size = mousepad_view_get_tab_size (document->textview);

  /* get the column */
  column = mousepad_util_get_real_line_offset (&iter, tab_size) + 1;

  /* get length of the selection */
  selection = mousepad_view_get_selection_length (document->textview);

  /* emit the signal */
  g_signal_emit (G_OBJECT (document), document_signals[CURSOR_CHANGED], 0, line, column, selection);
}



static void
mousepad_document_toggle_overwrite (GtkTextView      *textview,
                                    GParamSpec       *pspec,
                                    MousepadDocument *document)
{
  gboolean overwrite;

  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));
  _mousepad_return_if_fail (GTK_IS_TEXT_VIEW (textview));

  /* whether overwrite is enabled */
  overwrite = gtk_text_view_get_overwrite (textview);

  /* emit the signal */
  g_signal_emit (G_OBJECT (document), document_signals[OVERWRITE_CHANGED], 0, overwrite);
}



static void
mousepad_document_drag_data_received (GtkWidget        *widget,
                                      GdkDragContext   *context,
                                      gint              x,
                                      gint              y,
                                      GtkSelectionData *selection_data,
                                      guint             info,
                                      guint             time,
                                      MousepadDocument *document)
{
  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));

  /* emit the drag-data-received signal from the document when a tab or uri has been dropped */
  if (info == TARGET_TEXT_URI_LIST || info == TARGET_GTK_NOTEBOOK_TAB)
    g_signal_emit_by_name (G_OBJECT (document), "drag-data-received", context, x, y, selection_data, info, time);
}



static void
mousepad_document_filename_changed (MousepadDocument *document,
                                    const gchar      *filename)
{
  gchar *utf8_filename, *utf8_basename;

  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));
  _mousepad_return_if_fail (filename != NULL);

  /* convert the title into a utf-8 valid version for display */
  utf8_filename = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);

  if (G_LIKELY (utf8_filename))
    {
      /* create the display name */
      utf8_basename = g_filename_display_basename (utf8_filename);

      /* remove the old names */
      g_free (document->priv->utf8_filename);
      g_free (document->priv->utf8_basename);

      /* set the new names */
      document->priv->utf8_filename = utf8_filename;
      document->priv->utf8_basename = utf8_basename;

      /* update the tab label and tooltip */
      if (G_UNLIKELY (document->priv->label))
        {
          /* set the tab label */
          gtk_label_set_text (GTK_LABEL (document->priv->label), utf8_basename);

          /* set the tab tooltip */
          mousepad_util_set_tooltip (document->priv->ebox, utf8_filename);
        }
    }
}



void
mousepad_document_set_overwrite (MousepadDocument *document,
                                 gboolean          overwrite)
{
  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));

  gtk_text_view_set_overwrite (GTK_TEXT_VIEW (document->textview), overwrite);
}



void
mousepad_document_set_word_wrap (MousepadDocument *document,
                                 gboolean          word_wrap)
{
  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));

  /* store the setting */
  document->priv->word_wrap = word_wrap;

  /* set the wrapping mode */
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (document->textview),
                               word_wrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
}



void
mousepad_document_set_font (MousepadDocument *document,
                            const gchar      *font_name)
{
  PangoFontDescription *font_desc;

  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));

  if (G_LIKELY (font_name))
    {
      /* set the widget font */
      font_desc = pango_font_description_from_string (font_name);
      gtk_widget_modify_font (GTK_WIDGET (document->textview), font_desc);
      pango_font_description_free (font_desc);
    }
}



void
mousepad_document_focus_textview (MousepadDocument *document)
{
  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));

  /* focus the textview */
  gtk_widget_grab_focus (GTK_WIDGET (document->textview));
}



void
mousepad_document_go_to_line (MousepadDocument *document,
                              gint              line_number)
{
  GtkTextIter iter;

  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));
  _mousepad_return_if_fail (GTK_IS_TEXT_BUFFER (document->buffer));

  /* move the cursor */
  gtk_text_buffer_get_iter_at_line (document->buffer, &iter, line_number - 1);
  gtk_text_buffer_place_cursor (document->buffer, &iter);

  /* make sure the cursor is in the visible area */
  mousepad_view_put_cursor_on_screen (document->textview);
}



void
mousepad_document_send_statusbar_signals (MousepadDocument *document)
{
  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));

  /* re-send the cursor changed signal */
  mousepad_document_notify_cursor_position (document->buffer, NULL, document);

  /* re-send the overwrite signal */
  mousepad_document_toggle_overwrite (GTK_TEXT_VIEW (document->textview), NULL, document);
}



void
mousepad_document_line_numbers (MousepadDocument *document,
                                gint             *current_line,
                                gint             *last_line)
{
  GtkTextIter iter;

  _mousepad_return_if_fail (MOUSEPAD_IS_DOCUMENT (document));
  _mousepad_return_if_fail (GTK_IS_TEXT_BUFFER (document->buffer));

  /* get the current line number */
  gtk_text_buffer_get_iter_at_mark (document->buffer, &iter, gtk_text_buffer_get_insert (document->buffer));
  *current_line = gtk_text_iter_get_line (&iter) + 1;

  /* get the last line number */
  gtk_text_buffer_get_end_iter (document->buffer, &iter);
  *last_line = gtk_text_iter_get_line (&iter) + 1;
}



GtkWidget *
mousepad_document_get_tab_label (MousepadDocument *document)
{
  GtkWidget *hbox;
  GtkWidget *button, *image;

  /* create the box */
  hbox = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (hbox);

  /* the ebox */
  document->priv->ebox = g_object_new (GTK_TYPE_EVENT_BOX, "border-width", 2, "visible-window", FALSE, NULL);
  gtk_box_pack_start (GTK_BOX (hbox), document->priv->ebox, TRUE, TRUE, 0);
  mousepad_util_set_tooltip (document->priv->ebox, document->priv->utf8_filename);
  gtk_widget_show (document->priv->ebox);

  /* create the label */
  document->priv->label = gtk_label_new (mousepad_document_get_basename (document));
  gtk_container_add (GTK_CONTAINER (document->priv->ebox), document->priv->label);
  gtk_widget_show (document->priv->label);

  /* create the button */
  button = g_object_new (GTK_TYPE_BUTTON,
                         "relief", GTK_RELIEF_NONE,
                         "focus-on-click", FALSE,
                         "border-width", 0,
                         "can-default", FALSE,
                         "can-focus", FALSE, NULL);
  mousepad_util_set_tooltip (button, _("Close this tab"));
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (mousepad_document_tab_button_clicked), document);
  gtk_widget_show (button);

  /* button image */
  image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (button), image);
  gtk_widget_show (image);

  return hbox;
}



static void
mousepad_document_tab_button_clicked (GtkWidget        *widget,
                                      MousepadDocument *document)
{
  g_signal_emit (G_OBJECT (document), document_signals[CLOSE_TAB], 0);
}



const gchar *
mousepad_document_get_basename (MousepadDocument *document)
{
  static gint untitled_counter = 0;

  _mousepad_return_val_if_fail (MOUSEPAD_IS_DOCUMENT (document), NULL);

  /* check if there is a filename set */
  if (document->priv->utf8_basename == NULL)
    {
      /* create an unique untitled document name */
      document->priv->utf8_basename = g_strdup_printf ("%s %d", _("Untitled"), ++untitled_counter);
    }

  return document->priv->utf8_basename;
}



const gchar *
mousepad_document_get_filename (MousepadDocument *document)
{
  _mousepad_return_val_if_fail (MOUSEPAD_IS_DOCUMENT (document), NULL);

  return document->priv->utf8_filename;
}



gboolean
mousepad_document_get_word_wrap (MousepadDocument *document)
{
  _mousepad_return_val_if_fail (MOUSEPAD_IS_DOCUMENT (document), FALSE);

  return document->priv->word_wrap;
}
