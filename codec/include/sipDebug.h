/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipDebug.h
 ********************************************************/

#ifndef _SIP_DEBUG_H
#define _SIP_DEBUG_H


#define DEBUG_SIP_PRINT_TOKEN(token, tokenNum) ({for(int iqaz=0; iqaz<tokenNum; iqaz++) {debug("token debug display, tokenNum=%d, token[%d]=%c", tokenNum, iqaz, token[iqaz]);}})


#endif


