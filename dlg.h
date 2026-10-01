#ifndef DLG_H
#define DLG_H

#include <Elementary.h>
#include <stdlib.h>
#include <string.h>
#include "cp_backend.h"

/* done() is called after a successful change (used to refresh the main list) */
void dlg_add_open(Evas_Object *parent, void (*done)(void));
void dlg_props_open(Evas_Object *parent, const char *name, void (*done)(void));

/* ---- small UI helpers ---- */

/* Plain UTF-8 text of an entry; caller free()s. */
static inline char *
dlg_txt(Evas_Object *e)
{
   const char *m = elm_entry_entry_get(e);
   char *s = m ? elm_entry_markup_to_utf8(m) : NULL;
   return s ? s : strdup("");
}

static inline void
dlg_set_txt(Evas_Object *e, const char *s)
{
   char *m = elm_entry_utf8_to_markup(s ? s : "");
   elm_entry_entry_set(e, m ? m : "");
   free(m);
}

/* Set arbitrary (non-markup) text on a label/button/frame/hoversel. */
static inline void
dlg_label_set(Evas_Object *o, const char *s)
{
   char *m = elm_entry_utf8_to_markup(s ? s : "");
   elm_object_text_set(o, m ? m : "");
   free(m);
}

static inline Evas_Object *
dlg_entry(Evas_Object *parent)
{
   Evas_Object *e = elm_entry_add(parent);
   elm_entry_single_line_set(e, EINA_TRUE);
   elm_entry_scrollable_set(e, EINA_TRUE);
   elm_scroller_policy_set(e, ELM_SCROLLER_POLICY_OFF, ELM_SCROLLER_POLICY_OFF);
   evas_object_size_hint_weight_set(e, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(e, EVAS_HINT_FILL, 0.5);
   evas_object_show(e);
   return e;
}

static inline Evas_Object *
dlg_button(Evas_Object *parent, const char *label, Evas_Smart_Cb cb, void *data)
{
   Evas_Object *b = elm_button_add(parent);
   elm_object_text_set(b, label);
   evas_object_smart_callback_add(b, "clicked", cb, data);
   evas_object_show(b);
   return b;
}

static inline Evas_Object *
dlg_table(Evas_Object *parent)
{
   Evas_Object *t = elm_table_add(parent);
   elm_table_padding_set(t, 8, 6);
   evas_object_size_hint_weight_set(t, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(t, EVAS_HINT_FILL, 0.0);
   evas_object_show(t);
   return t;
}

/* form row: label in column 0 (may be NULL), widget in column 1 */
static inline void
dlg_row(Evas_Object *t, int row, const char *label, Evas_Object *w)
{
   if (label)
     {
        Evas_Object *l = elm_label_add(t);
        dlg_label_set(l, label);
        evas_object_size_hint_align_set(l, 0.0, 0.5);
        evas_object_show(l);
        elm_table_pack(t, l, 0, row, 1, 1);
     }
   elm_table_pack(t, w, 1, row, 1, 1);
   evas_object_show(w);
}

static inline Evas_Object *
dlg_frame(Evas_Object *parent, const char *title, Evas_Object *content,
          Eina_Bool expand)
{
   Evas_Object *f = elm_frame_add(parent);
   dlg_label_set(f, title);
   elm_object_content_set(f, content);
   evas_object_size_hint_weight_set(f, EVAS_HINT_EXPAND,
                                    expand ? EVAS_HINT_EXPAND : 0.0);
   evas_object_size_hint_align_set(f, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(f);
   return f;
}

#endif
