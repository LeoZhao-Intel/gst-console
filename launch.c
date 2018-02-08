 
/* 
  Zhao Liang @2008.10.20 
  enchance mgst-launch.  
*/ 
#include <stdio.h>
#include <stdlib.h> 
#include <string.h> 
#include <signal.h> 
#include <unistd.h> 
#include <assert.h>
#include <sys/wait.h> 
#include <locale.h>             /* for LC_ALL */ 
#include <gst/gst.h> 
#include <gst/gstmessage.h> 
#include "control.h" 
#include "gstfunction.h"

static gboolean quiet = FALSE;

/* convenience macro so we don't have to litter the code with if(!quiet) */
#define PRINT if(!quiet)g_print

/* cmd handlers */
static int OnPlay( void* pParams); 
static int OnSeek( void* pParams); 
static int OnPause( void* pParams); 
static int OnDuration( void* pParams); 
static int OnToNull( void* pParams); 
static int OnToReady( void* pParams); 
static int OnSetProperty( void* pParams);
static int OnList( void* pParams);
static int OnOpen( void* pParams);
static int OnClose( void* pParams);
static int OnDebug( void* pParams);
static int OnRedirect( void* pParam);
static int OnQuit( void* pParam);
static int OnHelp( void* pParam);

const static SMGLCmd s_CtrlCmdTbl[]=
{ 
    { "Null", "Set state NULL", "null", OnToNull, 0}, 
    { "Ready", "Set state READY", "ready", OnToReady, 0}, 
    { "Play", "Set state PLAYING", "play", OnPlay, 0},  
    { "Seek", "Seek to time/percent", "seek to 10000ms: seek t=10000\n" \
                                      "seek to percent 30%: seek p=30", OnSeek, 0},  
    { "Pause", "Set state PAUSED", "pause", OnPause, 0},  
    { "Dur", "Get duration", "dur", OnDuration, 0}, 
    { "Set", "Set element property", "set filesrc location=xxxx", OnSetProperty, 0},
    { "Ls", "List pipeline/element", "list pipeline: ls \n" \
                                     "list filesrc: ls filesrc", OnList, 0},
    { "Open", "Open new pipeline", "example : open fakesrc ! fakesink", OnOpen, 0},
    { "Close", "Close pipeline", "close", OnClose, 0},
    { "Debug", "Set debug level", "open log: debug xxx:5\n"\
                                  "close log: debug xxx:0 ", OnDebug, 0},
    { "Quit", "Quit app", "quit app", OnQuit, 0},
    { ">>", "Redirect log to file", "start log redir: >> a.log \n" \
                                    "stop log: >> NULL ", OnRedirect, 0},
    { "Help", "List all commands or special cmd", "help ls", OnHelp, 0}
}; 
 
static const int CMD_COUNT = sizeof( s_CtrlCmdTbl)/sizeof( SMGLCmd); 
 
 
/* the global s_pPipeLine*/ 
static GstElement *s_pPipeLine; 
static GThread* s_pThdGMsgW = NULL; 
extern volatile gboolean glib_on_error_halt; 
static int s_bExitWatchMsg;
static FILE* g_pFile = NULL;
 
/*global setting options*/ 
/*...*/ 
 
/*------------------------------------------------------------*/ 
static void fault_restore (void); 
static void fault_spin (void); 
 
/* USE_SIGINFO */ 
static void fault_handler_sigaction (int signum, siginfo_t * si, void *misc) 
{ 
    fault_restore (); 
      switch (si->si_signo) 
       { 
        case SIGSEGV: 
            printf ( "Caught SIGSEGV accessing address %p\n", si->si_addr); 
            break; 
        case SIGQUIT: 
              log_msg ( "Caught SIGQUIT\n"); 
              break; 
        default: 
              log_msg( " signo:  %d\n errno:  %d\n code:   %d\n",  
                    si->si_signo, si->si_errno, si->si_code); 
              break; 
      } 
      fault_spin (); 
} 
 
static void fault_spin (void) 
{ 
    int spinning = TRUE; 
   
    glib_on_error_halt = FALSE; 
      g_on_error_stack_trace ("gst-launch"); 
   
    wait (NULL); 
 
    /* FIXME how do we know if we were run by libtool? */ 
    log_msg( "Spinning.  Please run 'gdb gst-launch %d' to continue debugging, " 
              "Ctrl-C to quit, or Ctrl-\\ to dump core.\n", (gint) getpid ()); 
    while (spinning) 
        g_usleep (1000000);  /*One second*/ 
} 
 
static void fault_restore (void) 
{ 
    struct sigaction action; 
    memset (&action, 0, sizeof (action)); 
      action.sa_handler = SIG_DFL; 
   
    sigaction (SIGSEGV, &action, NULL); 
      sigaction (SIGQUIT, &action, NULL); 
} 
 
static void fault_setup (void) 
{ 
    struct sigaction action; 
      memset (&action, 0, sizeof (action)); 
   
    action.sa_sigaction = fault_handler_sigaction; 
    action.sa_flags = SA_SIGINFO; 
   
    sigaction (SIGSEGV, &action, NULL); 
      sigaction (SIGQUIT, &action, NULL); 
     
    return; 
} 
 
static void print_tag (const GstTagList * list, const gchar * tag, gpointer unused) 
{
  gint i, count;

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str = NULL;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      if (!gst_tag_list_get_string_index (list, tag, i, &str)) {
        g_warning ("Couldn't fetch string for %s tag", tag);
        g_assert_not_reached ();
      }
    } else if (gst_tag_get_type (tag) == GST_TYPE_SAMPLE) {
      GstSample *sample = NULL;

      if (gst_tag_list_get_sample_index (list, tag, i, &sample)) {
        GstBuffer *img = gst_sample_get_buffer (sample);
        GstCaps *caps = gst_sample_get_caps (sample);

        if (img) {
          if (caps) {
            gchar *caps_str;

            caps_str = gst_caps_to_string (caps);
            str = g_strdup_printf ("buffer of %" G_GSIZE_FORMAT " bytes, "
                "type: %s", gst_buffer_get_size (img), caps_str);
            g_free (caps_str);
          } else {
            str = g_strdup_printf ("buffer of %" G_GSIZE_FORMAT " bytes",
                gst_buffer_get_size (img));
          }
        } else {
          str = g_strdup ("NULL buffer");
        }
      } else {
        g_warning ("Couldn't fetch sample for %s tag", tag);
        g_assert_not_reached ();
      }
      gst_sample_unref (sample);
    } else if (gst_tag_get_type (tag) == GST_TYPE_DATE_TIME) {
      GstDateTime *dt = NULL;

      gst_tag_list_get_date_time_index (list, tag, i, &dt);
      if (!gst_date_time_has_time (dt)) {
        str = gst_date_time_to_iso8601_string (dt);
      } else {
        gdouble tz_offset = gst_date_time_get_time_zone_offset (dt);
        gchar tz_str[32];

        if (tz_offset != 0.0) {
          g_snprintf (tz_str, sizeof (tz_str), "(UTC %s%gh)",
              (tz_offset > 0.0) ? "+" : "", tz_offset);
        } else {
          g_snprintf (tz_str, sizeof (tz_str), "(UTC)");
        }

        str = g_strdup_printf ("%04u-%02u-%02u %02u:%02u:%02u %s",
            gst_date_time_get_year (dt), gst_date_time_get_month (dt),
            gst_date_time_get_day (dt), gst_date_time_get_hour (dt),
            gst_date_time_get_minute (dt), gst_date_time_get_second (dt),
            tz_str);
      }
      gst_date_time_unref (dt);
    } else {
      str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (str) {
      PRINT ("%16s: %s\n", i == 0 ? gst_tag_get_nick (tag) : "", str);
      g_free (str);
    }
  } 
} 
 
void sig_pipeline(  char* strDesc) 
{ 
    /* post an application specific message */ 
    gst_element_post_message (GST_ELEMENT (s_pPipeLine), 
    gst_message_new_application (GST_OBJECT (s_pPipeLine), 
            gst_structure_new ("AppInterrupt", strDesc, G_TYPE_STRING, "Pipeline interrupted", NULL)));     
    return; 
} 

/* print to file */ 
void print_log_to_file(gchar* str)
{
    if (g_pFile)
        fwrite(str, strlen(str), sizeof(gchar), g_pFile);
}

#define MAX_MSG_LEN 1024 
 
int check_msg() 
{ 
//    char strMsgBuf[ MAX_MSG_LEN]; 
    GstBus *bus;     
    GstMessage *pMsg= NULL; 
    int nRet = 1; 
//    gboolean bBuffering = FALSE; 
    const GstStructure *pStructInfo; 
 
    bus = gst_element_get_bus (GST_ELEMENT (s_pPipeLine)); 
    pMsg= gst_bus_pop( bus); 
 
    if ( pMsg == NULL) /*no messages exist in the bus*/ 
        return -1; 
 
/*    g_print( ("Got Message from element \"%s\" (%s): "), 
                      GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (pMsg))), 
                      gst_message_type_get_name (GST_MESSAGE_TYPE (pMsg))); 
         
    pStructInfo = gst_message_get_structure ( pMsg); 
     
    if (pStructInfo) 
    { 
        gchar *sstr; 
        sstr = gst_structure_to_string (pStructInfo); 
        g_print( "%s\n", sstr); 
        g_free (sstr); 
    } else 
    { 
        g_print ("no message details\n"); 
     } 
*/ 
    switch (GST_MESSAGE_TYPE (pMsg)) 
    { 
          case GST_MESSAGE_NEW_CLOCK: 
              { 
                  GstClock *clock; 
                  gst_message_parse_new_clock ( pMsg, &clock); 
                  g_print ("New clock: %s\n", GST_OBJECT_NAME (clock)); 
                  break; 
              } 
          case GST_MESSAGE_EOS: 
              g_print( ("Got EOS from element \"%s\".\n"), 
                      GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC ( pMsg)))); 
              OnPause( NULL);  /*FIXME: titank, check if it's necessary*/ 
              break; 
           case GST_MESSAGE_TAG: 
              { 
                  GstTagList *tags;     
                  gst_message_parse_tag ( pMsg, &tags); 
                  g_print ( ("FOUND TAG      : found by element \"%s\".\n"), 
                          GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC ( pMsg)))); 
                  gst_tag_list_foreach (tags, print_tag, NULL); 
                  gst_tag_list_free (tags); 
              } 
            break; 
        case GST_MESSAGE_WARNING: 
            { 
                GError *gerror; 
                gchar *debug; 
                gst_message_parse_warning( pMsg, &gerror, &debug); 
                g_print ("WARNING: Element \"%s\" warns: %s\n", 
                          GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC ( pMsg))), 
                          debug); 
                if (gerror) 
                    g_error_free (gerror); 
                g_free (debug); 
            } 
            break; 
        case GST_MESSAGE_ERROR: 
            { 
                GError *gerror; 
                gchar *debug; 
                gchar *name; 
                 
                gst_message_parse_error ( pMsg, &gerror, &debug); 
                name = gst_object_get_path_string (GST_MESSAGE_SRC ( pMsg)); 
                g_print ( ( "ERROR: from element %s: %s\n Additional debug info:\n%s\n"), name, gerror->message, debug); 
                g_free (name); 
     
                if (gerror) 
                    g_error_free (gerror); 
                g_free (debug);
                gst_message_unref( pMsg);                
                /* we have an error */ 
                nRet = -1; 
            //    quit_watch();  //quit 
              } 
        case GST_MESSAGE_STATE_CHANGED: 
            { 
                GstState old, new, pending; 
                gst_message_parse_state_changed ( pMsg, &old, &new, &pending); 
            //    update_prompt();
                break;
            } 
 
/*!!!!!there is buffering message in 10.10, but exists in 10.11!!!             
        case GST_MESSAGE_BUFFERING: 
            { 
                gint percent; 
                gst_message_parse_buffering( pMsg, &percent); 
                g_print ( "buffering... %d\r", percent); 
            } 
            break; 
*/             
        case GST_MESSAGE_APPLICATION: 
            if( pStructInfo) 
            { 
                if (gst_structure_has_name ( pStructInfo, "AppInterrupt")) 
                { 
                    /* this application message is posted when we caught an interrupt and 
                     *                  *                      *                    * we need to stop the pipeline. */ 
                    g_print (("Application message ...\n")); 
                    if( gst_structure_has_field( pStructInfo,  "quit")) 
                        nRet = 0; 
                } 
            } 
        default: 
            /* just be quiet by default */ 
            break; 
    } /* end of switch*/ 
     
    if ( pMsg) 
        gst_message_unref ( pMsg); 
    /* echo the message*/ 
//    g_print( "%s", strMsgBuf); 
    gst_object_unref (bus); 
 
    return nRet; 
} 

static gpointer msg_watch_process( gpointer pParam)
{
    struct timespec to_wait;
    to_wait.tv_sec = 0;
    to_wait.tv_nsec = 10000000; //10ms
    s_bExitWatchMsg = FALSE;
    
    while( !s_bExitWatchMsg)
    {
        if( !check_msg())
            break;
        nanosleep( &to_wait, NULL);
    }
    
    return (gpointer) 1;
}
 
int send_ctrl_cmd(char* strCmd) 
{ 
    int n_ret; 
    n_ret = analyze_cmdstr( strCmd, s_CtrlCmdTbl, CMD_COUNT); 
    return n_ret; 
} 
 
EMGLStatus get_pipeline_status() 
{ 
    EMGLStatus es = EMGL_NOINIT; 
    if (s_pPipeLine)
    {
        GstState gs = GST_STATE( s_pPipeLine); 
        switch( gs) 
        { 
        case GST_STATE_VOID_PENDING: 
            es = EMGL_MID; 
            break; 
        case  GST_STATE_NULL: 
            es = EMGL_NULL; 
            break; 
        case  GST_STATE_READY: 
            es = EMGL_READY; 
            break; 
        case   GST_STATE_PAUSED: 
            es = EMGL_PAUSE; 
            break; 
        case  GST_STATE_PLAYING: 
            es = EMGL_PLAYING; 
            break; 
        } 
    }
    return es; 
} 
 
int main (int argc, char *argv[]) 
{ 
    gchar **argvn; 
    
    /* parsing args */

        
    /* gst initialize*/ 
    gst_init( &argc, &argv);     

    if (argc > 1)
    {
        /* launch pipeline */
        argvn = g_new0 (char *, argc); 
        memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1)); 

        /* Initialize pipeline */
   
        OnOpen(argvn);
        g_free (argvn);     
    }
    /*setup control*/ 
    fault_setup ();     
    install_termctrl(); 

    /* Start Watching*/
    active_watcher(); 

    /* Shutdown */
    destroy_termctrl(); 
    OnClose(NULL);
    gst_deinit (); 

    return 1; 
} 
 
static void PrintStateChangeRet( GstStateChangeReturn ret) 
{ 
    switch( ret) 
    { 
        case GST_STATE_CHANGE_FAILURE: 
            log_msg( "%s\n", "state change failed"); 
            g_print ("\n");
            break; 
        case GST_STATE_CHANGE_NO_PREROLL:     
            log_msg( "%s\n", "state change preroll"); 
            g_print ("\n");
            break; 
        case GST_STATE_CHANGE_ASYNC: 
            log_msg( "%s\n", "state change async"); 
            break; 
        default: 
            break; 
    } 
    return; 
} 
 
static int OnPlay( void* pParams) 
{ 
    GstStateChangeReturn ret; 
    
    log_msg( "begin to play!\n"); 
    if (s_pPipeLine)
    {
        ret = gst_element_set_state( s_pPipeLine, GST_STATE_PLAYING); 
        if( ret != GST_STATE_CHANGE_SUCCESS) 
            PrintStateChangeRet( ret); 
    }
    return 1; 
} 
 
static int OnToNull( void* pParams) 
{  
    GstStateChangeReturn ret; 
    log_msg( "switch to NULL!\n"); 
    if (s_pPipeLine)
    {
      ret = gst_element_set_state( s_pPipeLine, GST_STATE_NULL); 
        if( ret != GST_STATE_CHANGE_SUCCESS) 
            PrintStateChangeRet( ret); 
    }
    return 1; 
} 
 
static int OnToReady( void* pParams) 
{    
    GstStateChangeReturn ret; 
    g_print( "switch to Ready!\n"); 
    if (s_pPipeLine)
    {
        ret = gst_element_set_state( s_pPipeLine, GST_STATE_READY); 
        if( ret != GST_STATE_CHANGE_SUCCESS) 
            PrintStateChangeRet( ret); 
    }
    return 1; 
} 

static int OnSeek( void* pParams) 
{ 
    gchar **argv = (gchar **) pParams;
    gchar *seek = NULL, *endstr = NULL;
    GstFormat fmt; 
    gint64 i64SeekVal; 
    gboolean bRet;  
        
    if (!s_pPipeLine) 
    {
        g_print ("pipeline is not ready\n");
        return 1;
    } 
    
    if (!argv || g_strv_length (argv) < 1)
    {
        g_print ("argument is not enough\n");
        return 1;
    }
    
    seek = argv[0];
    g_strstrip(seek);
    
    if ((seek[0] == 't' || seek[0] == 'T') && seek[1] == '=')
    {
        fmt = GST_FORMAT_TIME;
    }
    else if ((seek[0] == 'p' || seek[0] == 'P') && seek[1] == '=')
    {
        fmt = GST_FORMAT_PERCENT;
    }
    else
    {
        g_print ("Invalid argument\n");
        return 1;
    }
    
    i64SeekVal = strtoll(seek+2, &endstr, 10);
    if (endstr != NULL && endstr[0] != '\0')
    {
        g_print ("Invalid argument\n");
        return 1;
    }   
    
    switch( fmt) 
    { 
        case GST_FORMAT_TIME: 
        { 
             gint64 i64Duration; 
             GstFormat gf = GST_FORMAT_TIME; 
             i64SeekVal *= GST_MSECOND; 
             bRet = gst_element_query_duration( s_pPipeLine, gf, &i64Duration); 
             if( !bRet || ( i64SeekVal<0LL || i64SeekVal >= i64Duration)) 
             { 
                 log_msg( " error parameters, out of range!\n"); 
                 return 1; 
             } 
         } 
             break; 
         case GST_FORMAT_PERCENT: 
         { 
             if( i64SeekVal < 0 || i64SeekVal>100) 
             { 
                 log_msg( " error parameters!, out of range\n"); 
                 return 1; 
              } 
          } 
              break; 
          default: 
              return 1; 
    }     
       
    bRet = gst_element_seek_simple( s_pPipeLine, fmt, GST_SEEK_FLAG_FLUSH, i64SeekVal); 
    if( !bRet) 
        log_msg( "Seek failed!\n"); 
            
    return 1;  
} 
 
static int OnPause( void* pParams) 
{ 
    GstStateChangeReturn ret; 
    log_msg( "begin to Pause!\n"); 
    if (s_pPipeLine)
    {
        ret = gst_element_set_state( s_pPipeLine, GST_STATE_PAUSED); 
        if( ret != GST_STATE_CHANGE_SUCCESS) 
            PrintStateChangeRet( ret); 
    }
    return 1; 
} 
 
/*FIXME: titank 
 * To support different formats. Currently, only time format is supported 
 * */ 
static int OnDuration( void* pParams) 
{ 
    gint64 i64Pos, i64Duration; 
    gboolean bRet; 
    GstFormat fmt;      
    char strBuf[ 128]; 
    int nPos = 0; 
     
    if (s_pPipeLine)
    {
      fmt = GST_FORMAT_TIME; 
      bRet = gst_element_query_duration( s_pPipeLine, fmt, &i64Duration); 
      if( bRet)  /*FIXME: check if the return format is time!!!*/ 
          nPos += snprintf( strBuf, 128, "duration:%lld(ms)", i64Duration/1000000); 
      else 
          nPos += snprintf( strBuf, 128, "duration:UNKOWN"); 
   
      fmt = GST_FORMAT_TIME; 
   
      bRet = gst_element_query_position( s_pPipeLine, fmt, &i64Pos); 
      if( bRet)  /*FIXME: check if the return format is time!!!*/ 
          snprintf( strBuf  + nPos, 128-nPos, "current pos:%lld(ms)", i64Pos/1000000); 
      else 
          snprintf( strBuf + nPos, 128-nPos, "current pos:UNKOWN"); 
   
      log_msg( "%s\n", strBuf); 
  }
    return 1; 
}

static int OnList( void* pParams)
{
    gchar *pName = NULL;
    gchar ** argvn = (gchar **) pParams;
    
    if (argvn)
    {
        int i;
        
        for (i=0;;i++)
        {
            if (argvn[i] == NULL)
              break;
            pName = argvn[i];
            print_elements(s_pPipeLine, pName);
        }                
    }
    else
        print_elements(s_pPipeLine, NULL);
    return 1;
}

static int OnSetProperty( void* pParams)
{
    gchar **argv = (gchar **) pParams;
    gchar *name, *property;
    GstElement *pElement = NULL;
        
    if (!s_pPipeLine) 
    {
        g_print ("pipeline is not ready\n");
        return 1;
    } 
    
    if (g_strv_length (argv) < 2)
    {
        g_print ("argument is not enough\n");
        return 1;
    }
    
    name = argv[0];
    property = argv[1];
    
    /* to find if the element with specified name */
    pElement = Find_element(GST_BIN(s_pPipeLine), name);
    if (pElement)
        gst_parse_element_set(property, pElement);  
    else
        g_print ("no such element\n");
    
    return 1;
}

static int OnOpen( void* pParams)
{
    const gchar ** argvn = (const gchar **) pParams;
    GError *err = NULL; 
    
    if (s_pPipeLine) 
    {
        g_print ("pipeline is already ready\n");
        return 1;
    }

#if 0    
    /* only for debug */
    for (i = 0;; i++)
    {
        if (argvn[i] == NULL)
            break;
        g_print ("onOpen arg %d: %s\n", i, argvn[i]);
    }
#endif
    
    s_pPipeLine = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &err); 
    
    if (!s_pPipeLine || err) 
    { 
        if (err) 
        {
            g_print ("ERROR: pipeline could not be constructed: %s.\n", GST_STR_NULL (err->message)); 
            g_error_free (err); 
        } 
        else 
        { 
            g_print ("ERROR: pipeline could not be constructed.\n"); 
        }
        gst_object_unref ( s_pPipeLine); 
        s_pPipeLine = NULL;
    }

    if (s_pPipeLine) 
    {
        /* add message watcher*/    
        s_pThdGMsgW = g_thread_new( "watcher thread", msg_watch_process, NULL);
        assert( s_pThdGMsgW != NULL);
    }
    return 1;
}

static int OnClose( void* pParams)
{
    if (s_pPipeLine)
    {
        g_print ("FREEING pipeline ...\n"); 
        gst_element_set_state( s_pPipeLine, GST_STATE_NULL); 
        /*wait message watcher exit*/
        s_bExitWatchMsg = TRUE;
        g_thread_join( s_pThdGMsgW);
        gst_object_unref ( s_pPipeLine); 
        s_pPipeLine = NULL;
        g_print ("FREED pipeline ...\n"); 
    }
    return 1;
}





static int OnDebug( void* pParams)
{   
    if (s_pPipeLine)
    {
        gchar **argv = (gchar **) pParams;
        gchar **split;
        gchar **walk;
     
        if (!argv)
            return 1;
            
        split = g_strsplit (argv[0], ",", 0);
  
        for (walk = split; *walk; walk++) {
            if (strchr (*walk, ':')) {
                gchar **values = g_strsplit (*walk, ":", 2);

                if (values[0] && values[1]) {
                    gint level;
                    const gchar *category;
  
                    if (parse_debug_category (values[0], &category)
                       && parse_debug_level (values[1], &level))
                        gst_debug_set_threshold_for_name (category, level);
                }
  
                g_strfreev (values);
            }else {
                gint level;
  
                if (parse_debug_level (*walk, &level))
                    gst_debug_set_default_threshold (level);
            }
        }
  
        g_strfreev (split);
    }
    
  return 1;
}


static int OnRedirect( void* pParams)
{
    gchar **argv = (gchar **) pParams;
    
    if ( !argv || g_strv_length (argv) < 1)
        return 1;
    
    /* fopen log file */    
    if (strcasecmp(argv[0], "NULL") == 0)
    {
        if (g_pFile)
            fclose(g_pFile);
        g_pFile = NULL;    
        g_set_printerr_handler(NULL);
    }
    else
    {
        g_pFile = fopen(argv[0], "w+");
        if (g_pFile)
        {    
            g_set_printerr_handler((gpointer)print_log_to_file);
        }
        else
            g_print ("can not open log file\n");
    }
    return 1;
}

static int OnQuit( void* pParams)
{
    g_print ( "quit...\n");
    quit_watch();    
    return 1;
}

static int OnHelp( void* pParams)
{
    int i;
    gchar **argv = (gchar **) pParams;
    gchar *name = NULL;
    
    if ( argv )
        name = argv[0];
    else    	
        g_print ("The most commonly used Commands are:\n");
        
    for (i=0; i<CMD_COUNT;i++)
    {
        const SMGLCmd* e = &s_CtrlCmdTbl[i];
        if (name)
        {
            if (strcasecmp(name, e->strCmdName) ==0)
            {
                g_print ( "  %s:\n%s\n", e->strCmdName, e->strCmdDetails);
                return 1;
            }
        }
        else
            g_print ( "  %-10s:  %s\n", e->strCmdName, e->strCmdDesc);
       
    }
    
    if (name)
        g_print ("no this command\n");
    return 1;
}
