/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHeaderMisc.h
 ********************************************************/

#ifndef _SIP_HEADER_MISC_H
#define _SIP_HEADER_MISC_H

#include "sipHeader.h"

#if 0
osStatus_e sipHdrDecoded_dup(sipHdrDecoded_t* dst, sipHdrDecoded_t* src);
//only free the spaces pointed by data structures inside pData, does not free pData data structure itself
void sipHdrDecoded_delete(sipHdrDecoded_t* pData);
#endif

#endif
