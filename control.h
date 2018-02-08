#ifndef __CONTROL_H__
#define __CONTROL_H__

#define MAX_MGL_CMD_LEN 16
#define MAX_MGL_CMD_DESC 64

typedef int (*PFN_OnMGLCmd)( void* pParams);

typedef struct _SMGLCmd
{
    char* strCmdName;
    char* strCmdDesc;
    char* strCmdDetails;
    PFN_OnMGLCmd pfnOnCmd;
    int nReserve;
}SMGLCmd;

typedef enum
{
    EMGL_NOINIT,
    EMGL_NULL, 
    EMGL_READY,
    EMGL_PAUSE,
    EMGL_PLAYING,
    EMGL_MID
}EMGLStatus;

//#define mgl_inline __inline__
int install_termctrl();
int destroy_termctrl();
int analyze_cmdstr( char* strCmd, const SMGLCmd  CtrlCmdTbl[], int nTblSize);
void quit_watch();
void log_msg( const char* fmt, ...);
int active_watcher();
void update_prompt();

#endif /*!__CONTROL_H__*/
