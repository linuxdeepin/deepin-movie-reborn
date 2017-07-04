/****************************************************************************
*
*    Copyright (c) 2005 - 2012 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************/



#ifndef __GUT_SYSTEM_H__
#define __GUT_SYSTEM_H__

/* system related functions */
void sysOutput(const char *format, ...);
const char * sysGetCmd();
gctHANDLE sysLoadModule(const gctSTRING modName);
gctBOOL sysUnloadModule(const gctHANDLE mod);
gctPOINTER sysGetProcAddress(const gctHANDLE mod, gctSTRING ProName);
gctUINT32 sysGetModuleName(const gctHANDLE mod, gctSTRING str, gctUINT32 size);
gctBOOL sysSetupLog(const gctSTRING CaseName);

/* default log path */
extern char g_logPath[MAX_BUFFER_SIZE + 1]; /* The path of the log file. */
extern char g_bmpPath[MAX_BUFFER_SIZE + 1]; /* The path of the dumpped bmp files. */
extern char g_resultPath[MAX_BUFFER_SIZE + 1]; /* The path of the dumpped data file. */
extern char g_errorPath[MAX_BUFFER_SIZE + 1]; /* The path of the error files. */

extern gctHANDLE   g_caseDll;

#define SURFFIX ".so"
#define PREFIX "lib"


#endif /* __GUT_SYSTEM_H__ */
