#ifndef __GSTFUNCTION_H__
#define __GSTFUNCTION_H__

#include <gst/gst.h> 

gboolean parse_debug_category (gchar * str, const gchar ** category);
gboolean parse_debug_level (gchar * str, gint * level);
void gst_parse_element_set (gchar *value, GstElement *element);
gboolean print_elements(GstElement *element, gchar* pName);
GstElement* Find_element(GstBin *bin, gchar* pName);
void List_elements(GstBin *bin);


#endif /* !__GSTFUNCTION_H__ */

