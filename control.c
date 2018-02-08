/*
titank@2007.01.19
    command console implementation for mgst-launch: 
 A gstreamer pipeline test program.
*/

#include    <stdlib.h>
#include    <stdio.h>
#include    <string.h>
#include    <stdarg.h>
#include    <fcntl.h>
#include    <assert.h>
#include    <signal.h>
#include    <termios.h>
#include    "control.h"
#include    "glib.h"

extern int check_msg();
extern int send_ctrl_cmd(char* strCmd);
extern EMGLStatus get_pipeline_status();
extern void sig_pipeline(  char* strDesc);
    
#define CMDBUF_SIZE 128

static GMutex *s_pGMutex = NULL;
static char s_strCmdBuf[ CMDBUF_SIZE];
static FILE* s_fpTerm;
static struct termios s_ts, s_ots;
static sigset_t    s_sig, s_osig;
static int s_bExitWatch; 

void quit_watch()
{
    g_mutex_lock( s_pGMutex);
    s_bExitWatch = 1;
    g_mutex_unlock( s_pGMutex);
}

int analyze_cmdstr( gchar* strCmd, const SMGLCmd  CtrlCmdTbl[], int nTblSize)
{
    int len, i, index, ret;
    gchar **argv;

    if( strCmd == NULL || strcmp(strCmd, "") == 0 )
        return -1;

    g_strstrip(strCmd);

    argv = g_strsplit_set(strCmd, " ", 10);
    if (!argv)
        g_print ("no command\n");

#if 0
    /* only for debug */
    for (i = 0;; i++)
    {
        if (argv[i] == NULL)
            break;
        g_print ("arg %d: %s\n", i, argv[i]);
    }
#endif 

    /* Find cmd handler from CmdTable */
    index=-1;
    len = strlen(argv[0]);
    for( i=0; i<nTblSize; i++)
    {
        const SMGLCmd* e = &CtrlCmdTbl[i];
        int len2 = strlen( e->strCmdName);
        if( len2 < len)
            continue;
        if( len2 >len) 
            len2 = len;
        if( strncasecmp(argv[0], e->strCmdName, len2) == 0)
        {
            if( index>=0)
            {
                //ambiguous command
                index = -1;
                break;
            }
            index = i;  
        }
    }

    if( index < 0)
    {
        g_strfreev(argv);
        return 0;
    }

    /* cmd handler callback */
    if (g_strv_length (argv) > 1)
        ret = CtrlCmdTbl[ index].pfnOnCmd( argv+1 );
    else
        ret = CtrlCmdTbl[ index].pfnOnCmd( NULL );
    /*check the return value*/
    switch( ret)
    {
        case -2:  //wrong parameters
            ret = -2;
            break;
        case -1:
            ret = -1; //fatal error while executing the command
            break;
        default:
            ret = 1;
            break;
    }

    g_strfreev(argv);    
    return ret;
}

#define MAX_PROMPT_LEN 16 
void update_prompt()
{
    char strPromptBuf[ MAX_PROMPT_LEN];
    
    EMGLStatus s = get_pipeline_status();
    switch( s)
    {
        case EMGL_NOINIT:
            snprintf( strPromptBuf, MAX_PROMPT_LEN, "[    ]>");
            break;
        case EMGL_NULL:
            snprintf( strPromptBuf, MAX_PROMPT_LEN, "[NULL]>");
            break;
        case EMGL_READY:
            snprintf( strPromptBuf, MAX_PROMPT_LEN, "[READY]>");
            break;
        case EMGL_PAUSE:
            snprintf( strPromptBuf, MAX_PROMPT_LEN, "[PAUSE]>");
            break;
          case EMGL_PLAYING:
            snprintf( strPromptBuf, MAX_PROMPT_LEN, "[PLAYING]>");
            break;
        case EMGL_MID:
            snprintf( strPromptBuf, MAX_PROMPT_LEN, "[MID]>");
            break;
        default:
            assert(FALSE);
            snprintf( strPromptBuf, MAX_PROMPT_LEN, "[ERROR]>");
            break;
    }
    
    fprintf( s_fpTerm, "%s%s%s", "\033[31;02m", strPromptBuf, "\033[00m");    
    
    return;
}

void log_msg( const char* fmt, ...)
{
    va_list ap;
    g_mutex_lock( s_pGMutex);
    
    fprintf(s_fpTerm, "%s", "\033[33;01m");
    va_start( ap, fmt);
    vfprintf( s_fpTerm, fmt, ap);
    va_end( ap);
    fprintf(s_fpTerm, "%s", "\033[00m");
//    update_prompt();
    g_mutex_unlock( s_pGMutex);
    return;
}

int active_watcher()
{
    int n_ret;  
    char* ptr;
    struct timespec to_wait;
    to_wait.tv_sec = 0;
    to_wait.tv_nsec = 10000000; //10ms

    update_prompt(); 
    /* command input watcher*/
    while( !s_bExitWatch)
    {
        ptr = fgets( s_strCmdBuf, CMDBUF_SIZE, s_fpTerm);
/*        while ( (( c = getc( s_fpTerm)) != -1) && c != '\n')
        {
            if (ptr < &s_strCmdBuf[ CMDBUF_SIZE])
                *ptr++ = c;
        }*/
        if( ptr == NULL)
        {
            nanosleep( &to_wait, NULL);
            continue;
        }
        s_strCmdBuf[ strlen( s_strCmdBuf)-1] = '\0'; /*get rid of '\n' at tail*/
        //*ptr = 0;                 /* null terminate */
        n_ret = send_ctrl_cmd( s_strCmdBuf);
       
        if( n_ret == 0)
            log_msg( "unknow command!\n");
        update_prompt();
    }    

    g_print ( "\nexit watcher...\n");
    return 1;
}

int install_termctrl()
{    
    int nVal;
    
    /*mask SIGINT related*/
    sigemptyset(&s_sig);
    sigaddset(&s_sig, SIGINT);        /* block SIGINT */
    sigaddset(&s_sig, SIGTSTP);     /* block SIGTSTP */
    sigprocmask(SIG_BLOCK, &s_sig, &s_osig);    /* and save mask */

    /*configure term properties*/
    if ( (s_fpTerm = fopen( ctermid( NULL), "r+")) == NULL)
    {
        fprintf( stderr, "failed to open term\n");
        return -1; 
    }
    nVal = fcntl( fileno( s_fpTerm), F_GETFL, 0);
    if( nVal < 0)
    {
        fprintf( stderr, "fcntl erro for fd %d\n", fileno( s_fpTerm));
        return -1;
    }
    nVal |= O_NONBLOCK;
    if( fcntl( fileno( s_fpTerm), F_SETFL, nVal) < 0)
    {
        fprintf( stderr, "fcntl erro for fd %d\n", fileno( s_fpTerm));
        return -1;
    }
    setbuf( s_fpTerm, NULL);
    tcgetattr( fileno( s_fpTerm), &s_ts);     
    s_ots = s_ts;                        /*structure copy, save tty state*/
    s_ts.c_lflag |= ICANON;
    
    tcsetattr(fileno( s_fpTerm), TCSAFLUSH, &s_ts);
    
    if (!g_thread_supported ()) g_thread_init (NULL);
    s_pGMutex = g_mutex_new ();
    assert( s_pGMutex != NULL);  //FIXME: more exception handle
    
//    update_prompt();
    
//    active_watcher();
    return 1;
}

int destroy_termctrl()
{
    tcsetattr(fileno(s_fpTerm), TCSANOW, &s_ots); /* restore TTY state */
    sigprocmask(SIG_SETMASK, &s_osig, NULL);    /* restore mask */
    fclose(s_fpTerm);         /* done with /dev/tty */

    g_mutex_free( s_pGMutex);
    s_pGMutex = NULL;
    
    return 1;
}
