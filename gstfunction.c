#include <stdio.h>
#include "gstfunction.h"
#include <string.h>
#include <gst/controller/controller.h> 
static void print_element_properties_info (GstElement * element);
static char *_name = NULL;

gboolean
parse_debug_category (gchar * str, const gchar ** category)
{
    if (!str)
        return FALSE;

    /* works in place */
    g_strstrip (str);

    if (str[0] != 0) 
    {
        *category = str;
        return TRUE;
    }

    return FALSE;
}

gboolean
parse_debug_level (gchar * str, gint * level)
{
    if (!str)
        return FALSE;

    /* works in place */
    g_strstrip (str);

    if (str[0] != 0 && str[1] == 0
        && str[0] >= '0' && str[0] < '0' + GST_LEVEL_COUNT) {
        *level = str[0] - '0';
        return TRUE;
    }

    return FALSE;
}

#define n_print g_print

static gboolean
print_field (GQuark field, const GValue * value, gpointer pfx)
{
  gchar *str = gst_value_serialize (value);

  n_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}


static void
print_caps (const GstCaps * caps, const gchar * pfx)
{
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    n_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    n_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
    GstCapsFeatures *features = gst_caps_get_features (caps, i);

    if (features && (gst_caps_features_is_any (features) ||
            !gst_caps_features_is_equal (features,
                GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY))) {
      gchar *features_string = gst_caps_features_to_string (features);

      n_print ("%s%s(%s)\n", pfx, gst_structure_get_name (structure),
          features_string);
      g_free (features_string);
    } else {
      n_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    }
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

static gchar *
flags_to_string (GFlagsValue * vals, guint flags)
{
  GString *s = NULL;
  guint flags_left, i;

  /* first look for an exact match and count the number of values */
  for (i = 0; vals[i].value_name != NULL; ++i) {
    if (vals[i].value == flags)
      return g_strdup (vals[i].value_nick);
  }

  s = g_string_new (NULL);

  /* we assume the values are sorted from lowest to highest value */
  flags_left = flags;
  while (i > 0) {
    --i;
    if (vals[i].value != 0 && (flags_left & vals[i].value) == vals[i].value) {
      if (s->len > 0)
        g_string_append_c (s, '+');
      g_string_append (s, vals[i].value_nick);
      flags_left -= vals[i].value;
      if (flags_left == 0)
        break;
    }
  }

  if (s->len == 0)
    g_string_assign (s, "(none)");

  return g_string_free (s, FALSE);
}

#define KNOWN_PARAM_FLAGS \
  (G_PARAM_CONSTRUCT | G_PARAM_CONSTRUCT_ONLY | \
  G_PARAM_LAX_VALIDATION |  G_PARAM_STATIC_STRINGS | \
  G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_DEPRECATED | \
  GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | \
  GST_PARAM_MUTABLE_PAUSED | GST_PARAM_MUTABLE_READY)

void
print_element_properties_info (GstElement * element)
{
  GParamSpec **property_specs;
  guint num_properties, i;
  gboolean readable;
  gboolean first_flag;

  property_specs = g_object_class_list_properties
      (G_OBJECT_GET_CLASS (element), &num_properties);
  n_print ("\n");
  n_print ("Element Properties:\n");

  for (i = 0; i < num_properties; i++) {
    GValue value = { 0, };
    GParamSpec *param = property_specs[i];

    readable = FALSE;

    g_value_init (&value, param->value_type);

    n_print ("  %-20s: %s\n", g_param_spec_get_name (param),
        g_param_spec_get_blurb (param));

    first_flag = TRUE;
    n_print ("%-23.23s flags: ", "");
    if (param->flags & G_PARAM_READABLE) {
      g_object_get_property (G_OBJECT (element), param->name, &value);
      readable = TRUE;
      g_print ("%s%s", (first_flag) ? "" : ", ", "readable");
      first_flag = FALSE;
    } else {
      /* if we can't read the property value, assume it's set to the default
       * (which might not be entirely true for sub-classes, but that's an
       * unlikely corner-case anyway) */
      g_param_value_set_default (param, &value);
    }
    if (param->flags & G_PARAM_WRITABLE) {
      g_print ("%s%s", (first_flag) ? "" : ", ", "writable");
      first_flag = FALSE;
    }
    if (param->flags & G_PARAM_DEPRECATED) {
      g_print ("%s%s", (first_flag) ? "" : ", ", "deprecated");
      first_flag = FALSE;
    }
    if (param->flags & GST_PARAM_CONTROLLABLE) {
      g_print (", %s", "controllable");
      first_flag = FALSE;
    }
    if (param->flags & GST_PARAM_MUTABLE_PLAYING) {
      g_print (", %s", "changeable in NULL, READY, PAUSED or PLAYING state");
    } else if (param->flags & GST_PARAM_MUTABLE_PAUSED) {
      g_print (", %s", "changeable only in NULL, READY or PAUSED state");
    } else if (param->flags & GST_PARAM_MUTABLE_READY) {
      g_print (", %s", "changeable only in NULL or READY state");
    }
    if (param->flags & ~KNOWN_PARAM_FLAGS) {
      g_print ("%s0x%0x", (first_flag) ? "" : ", ",
          param->flags & ~KNOWN_PARAM_FLAGS);
    }
    n_print ("\n");

    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING:
      {
        const char *string_val = g_value_get_string (&value);

        n_print ("%-23.23s String. ", "");

        if (string_val == NULL)
          g_print ("Default: null");
        else
          g_print ("Default: \"%s\"", string_val);
        break;
      }
      case G_TYPE_BOOLEAN:
      {
        gboolean bool_val = g_value_get_boolean (&value);

        n_print ("%-23.23s Boolean. ", "");

        g_print ("Default: %s", bool_val ? "true" : "false");
        break;
      }
      case G_TYPE_ULONG:
      {
        GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (param);

        n_print ("%-23.23s Unsigned Long. ", "");
        g_print ("Range: %lu - %lu Default: %lu ",
            pulong->minimum, pulong->maximum, g_value_get_ulong (&value));

        GST_ERROR ("%s: property '%s' of type ulong: consider changing to "
            "uint/uint64", GST_OBJECT_NAME (element),
            g_param_spec_get_name (param));
        break;
      }
      case G_TYPE_LONG:
      {
        GParamSpecLong *plong = G_PARAM_SPEC_LONG (param);

        n_print ("%-23.23s Long. ", "");
        g_print ("Range: %ld - %ld Default: %ld ",
            plong->minimum, plong->maximum, g_value_get_long (&value));

        GST_ERROR ("%s: property '%s' of type long: consider changing to "
            "int/int64", GST_OBJECT_NAME (element),
            g_param_spec_get_name (param));
        break;
      }
      case G_TYPE_UINT:
      {
        GParamSpecUInt *puint = G_PARAM_SPEC_UINT (param);

        n_print ("%-23.23s Unsigned Integer. ", "");
        g_print ("Range: %u - %u Default: %u ",
            puint->minimum, puint->maximum, g_value_get_uint (&value));
        break;
      }
      case G_TYPE_INT:
      {
        GParamSpecInt *pint = G_PARAM_SPEC_INT (param);

        n_print ("%-23.23s Integer. ", "");
        g_print ("Range: %d - %d Default: %d ",
            pint->minimum, pint->maximum, g_value_get_int (&value));
        break;
      }
      case G_TYPE_UINT64:
      {
        GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (param);

        n_print ("%-23.23s Unsigned Integer64. ", "");
        g_print ("Range: %" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT
            " Default: %" G_GUINT64_FORMAT " ",
            puint64->minimum, puint64->maximum, g_value_get_uint64 (&value));
        break;
      }
      case G_TYPE_INT64:
      {
        GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (param);

        n_print ("%-23.23s Integer64. ", "");
        g_print ("Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT
            " Default: %" G_GINT64_FORMAT " ",
            pint64->minimum, pint64->maximum, g_value_get_int64 (&value));
        break;
      }
      case G_TYPE_FLOAT:
      {
        GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (param);

        n_print ("%-23.23s Float. ", "");
        g_print ("Range: %15.7g - %15.7g Default: %15.7g ",
            pfloat->minimum, pfloat->maximum, g_value_get_float (&value));
        break;
      }
      case G_TYPE_DOUBLE:
      {
        GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (param);

        n_print ("%-23.23s Double. ", "");
        g_print ("Range: %15.7g - %15.7g Default: %15.7g ",
            pdouble->minimum, pdouble->maximum, g_value_get_double (&value));
        break;
      }
      case G_TYPE_CHAR:
      case G_TYPE_UCHAR:
        GST_ERROR ("%s: property '%s' of type char: consider changing to "
            "int/string", GST_OBJECT_NAME (element),
            g_param_spec_get_name (param));
        /* fall through */
      default:
        if (param->value_type == GST_TYPE_CAPS) {
          const GstCaps *caps = gst_value_get_caps (&value);

          if (!caps)
            n_print ("%-23.23s Caps (NULL)", "");
          else {
            print_caps (caps, "                           ");
          }
        } else if (G_IS_PARAM_SPEC_ENUM (param)) {
          GEnumValue *values;
          guint j = 0;
          gint enum_value;
          const gchar *value_nick = "";

          values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
          enum_value = g_value_get_enum (&value);

          while (values[j].value_name) {
            if (values[j].value == enum_value)
              value_nick = values[j].value_nick;
            j++;
          }

          n_print ("%-23.23s Enum \"%s\" Default: %d, \"%s\"", "",
              g_type_name (G_VALUE_TYPE (&value)), enum_value, value_nick);

          j = 0;
          while (values[j].value_name) {
            g_print ("\n");
            if (_name)
              g_print ("%s", _name);
            g_print ("%-23.23s    (%d): %-16s - %s", "",
                values[j].value, values[j].value_nick, values[j].value_name);
            j++;
          }
          /* g_type_class_unref (ec); */
        } else if (G_IS_PARAM_SPEC_FLAGS (param)) {
          GParamSpecFlags *pflags = G_PARAM_SPEC_FLAGS (param);
          GFlagsValue *vals;
          gchar *cur;

          vals = pflags->flags_class->values;

          cur = flags_to_string (vals, g_value_get_flags (&value));

          n_print ("%-23.23s Flags \"%s\" Default: 0x%08x, \"%s\"", "",
              g_type_name (G_VALUE_TYPE (&value)),
              g_value_get_flags (&value), cur);

          while (vals[0].value_name) {
            g_print ("\n");
            if (_name)
              g_print ("%s", _name);
            g_print ("%-23.23s    (0x%08x): %-16s - %s", "",
                vals[0].value, vals[0].value_nick, vals[0].value_name);
            ++vals;
          }

          g_free (cur);
        } else if (G_IS_PARAM_SPEC_OBJECT (param)) {
          n_print ("%-23.23s Object of type \"%s\"", "",
              g_type_name (param->value_type));
        } else if (G_IS_PARAM_SPEC_BOXED (param)) {
          n_print ("%-23.23s Boxed pointer of type \"%s\"", "",
              g_type_name (param->value_type));
          if (param->value_type == GST_TYPE_STRUCTURE) {
            const GstStructure *s = gst_value_get_structure (&value);
            if (s)
              gst_structure_foreach (s, print_field,
                  (gpointer) "                           ");
          }
        } else if (G_IS_PARAM_SPEC_POINTER (param)) {
          if (param->value_type != G_TYPE_POINTER) {
            n_print ("%-23.23s Pointer of type \"%s\".", "",
                g_type_name (param->value_type));
          } else {
            n_print ("%-23.23s Pointer.", "");
          }
        } else if (param->value_type == G_TYPE_VALUE_ARRAY) {
          GParamSpecValueArray *pvarray = G_PARAM_SPEC_VALUE_ARRAY (param);

          if (pvarray->element_spec) {
            n_print ("%-23.23s Array of GValues of type \"%s\"", "",
                g_type_name (pvarray->element_spec->value_type));
          } else {
            n_print ("%-23.23s Array of GValues", "");
          }
        } else if (GST_IS_PARAM_SPEC_FRACTION (param)) {
          GstParamSpecFraction *pfraction = GST_PARAM_SPEC_FRACTION (param);

          n_print ("%-23.23s Fraction. ", "");

          g_print ("Range: %d/%d - %d/%d Default: %d/%d ",
              pfraction->min_num, pfraction->min_den,
              pfraction->max_num, pfraction->max_den,
              gst_value_get_fraction_numerator (&value),
              gst_value_get_fraction_denominator (&value));
        } else {
          n_print ("%-23.23s Unknown type %ld \"%s\"", "",
              (glong) param->value_type, g_type_name (param->value_type));
        }
        break;
    }
    if (!readable)
      g_print (" Write only\n");
    else
      g_print ("\n");

    g_value_reset (&value);
  }
  if (num_properties == 0)
    n_print ("  none\n");

  g_free (property_specs);
}

/* copy from gst/parse/grammar.y */
static inline void
gst_parse_unescape (gchar *str)
{
    gchar *walk;
    
    g_return_if_fail (str != NULL);
    walk = str;
    
    while (*walk) {
        if (*walk == '\\')
            walk++;
        *str = *walk;
        str++;
        walk++;
    }
    *str = '\0';
}

void
gst_parse_element_set (gchar *prop_str, GstElement *element)
{
    GParamSpec *pspec;
    GType value_type;
    GValue v = { 0, };
    gchar *pos = prop_str;
    gchar *name = prop_str;
    gchar *value;

    g_return_if_fail (G_IS_OBJECT (element));
    g_return_if_fail (name != NULL);
    g_return_if_fail (prop_str != NULL);

    /* parse the string, so the property name is null-terminated an pos points
       to the beginning of the value */
    while (!g_ascii_isspace (*pos) && (*pos != '=')) pos++; 
    if (*pos == '=') { 
      *pos = '\0'; 
    } else { 
      *pos = '\0'; 
      pos++;
      while (g_ascii_isspace (*pos)) pos++; 
    } 
    pos++; 
    while (g_ascii_isspace (*pos)) pos++; 
    if (*pos == '"') {
      pos++;
      pos[strlen (pos) - 1] = '\0';
    }
    gst_parse_unescape (pos);
    value = pos;

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (element), name);
    if (!pspec)
      return;

    value_type = pspec->value_type;

    GST_DEBUG ("pspec->flags is %d, pspec->value_type is %s",
        pspec->flags, g_type_name (value_type));

    if (!(pspec->flags & G_PARAM_WRITABLE))
      return;

    g_value_init (&v, value_type);

    /* special case for element <-> xml (de)serialisation */
    if (value_type == GST_TYPE_STRUCTURE && strcmp (value, "NULL") == 0) {
      g_value_set_boxed (&v, NULL);
      goto done;
    }

    if (!gst_value_deserialize (&v, value))
      return;

done:

    g_object_set_property (element, pspec->name, &v);
    g_value_unset (&v);

    return;
}

gboolean print_elements(GstElement *element, gchar* pName)
{
    GstElement *pFindElement = NULL;
  
    if (pName) /* Search element/bin */
    {
        pFindElement = Find_element (GST_BIN(element), pName); 
    }
    else if ( GST_IS_BIN(element))
    {
        pFindElement = element;
    }
    
    if (pFindElement)
    {
        print_element_properties_info(pFindElement);
        if (GST_IS_BIN(pFindElement))
        {
            List_elements(GST_BIN(pFindElement));
        }
        return TRUE;
    }
        
    return FALSE;
}

GstElement* Find_element(GstBin *bin, gchar* pName)
{
    GValue value = { 0 };
    GstIterator* it;
    GstObject *pElement = NULL;
    GstElement *pFindElement = NULL;
    gboolean done = FALSE;
    gint len1, len2;
    
    if (!pName || !bin || strlen(pName) == 0)
        return NULL;
        
    if (GST_IS_BIN(bin) == FALSE)
        return NULL;
    
    /* check if bin is you wanted */
    len1 = strlen(gst_object_get_name(GST_OBJECT(bin)));
    len2 = strlen (pName);
    if (len1 == len2)
    {
        if (strncasecmp(pName, gst_object_get_name(GST_OBJECT(bin)), len1) == 0) 
        {
            return GST_ELEMENT(bin);
        }
    }                
    
    /* search from bin */
    it = gst_bin_iterate_elements( bin );
    while (!done) 
    {
        switch (gst_iterator_next (it, &value)) 
        {
            case GST_ITERATOR_OK:
                pElement = (GstObject *) g_value_get_object (&value);
                len1 = strlen(gst_object_get_name(pElement));
                len2 = strlen (pName);
                                    
                if ( (len1 == len2) && (strncasecmp(pName, gst_object_get_name(pElement), len1) == 0) ) 
                {
                    pFindElement = GST_ELEMENT(pElement);
                    done = TRUE;
                }
                else
                {
                    if (GST_IS_BIN (pElement)) 
                    {
                        pFindElement = Find_element(GST_BIN(pElement), pName);
                        if (pFindElement)
                            done = TRUE;
                    }
                }                
                gst_object_unref( pElement);
                break;
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync (it);
                break;
            case GST_ITERATOR_ERROR:                        
                done = TRUE;
                break;
            case GST_ITERATOR_DONE:
                done = TRUE;
                break;
        }
    }    
    
    return pFindElement;
}

void List_elements(GstBin *bin)
{
	GList *children;
    char tmp[1024];
    int nPos = 0;

	if (!GST_IS_BIN (bin))
	  return;

    if( nPos < 1024) 
        nPos += snprintf( tmp+nPos, 1024 - nPos, "Elements in %s:\n", GST_ELEMENT_NAME (GST_ELEMENT (bin))); 
		
	children = (GList *) GST_BIN (bin)->children;
	if (children) {
	  n_print ("\n");
	  n_print ("Children:\n");
	}
	
	while (children) {
	  /* list all elements */
	  if( nPos < 1024) 
	  {
		 if (GST_IS_BIN(GST_ELEMENT (children->data)))
			  nPos += snprintf( tmp+nPos, 1024 - nPos, "%s(+) ", GST_ELEMENT_NAME (GST_ELEMENT (children->data)));
		  else
			  nPos += snprintf( tmp+nPos, 1024 - nPos, "%s	", GST_ELEMENT_NAME (GST_ELEMENT (children->data)));
	  }
	  children = g_list_next (children);
	}

    g_print ( "%s\n", tmp);

    return;
}
