/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrFromto.h
 ********************************************************/

#ifndef _SIP_HDR_FROMTO_H
#define _SIP_HDR_FROMTO_H

#include "osMBuf.h"
#include "sipGenericNameParam.h"


typedef struct sipHdr_fromto {
	sipHdrGenericNameParam_t fromto;
} sipHdr_fromto_t;



osStatus_e sipParserHdr_fromto(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdr_fromto_t* pNameParam);
//sipUriExt_t* pFrom, osPointerLen_t* pTag
osStatus_e sipHdrFrom_encode(osMBuf_t* pSipBuf, void* pFrom, void* pTag);
osStatus_e sipHdrFrom_create(void* pFromHdrDT, void* pFromUriExtDT, void* pFromTagDT);
osStatus_e sipHdrTo_encode(osMBuf_t* pSipBuf, void* pToHdr, void* other);
osStatus_e sipHdrTo_create(void* pToHdrDT, void* pToUriExtDT, void* other);
osStatus_e sipHdrFromto_generateTagId(osPointerLen_t* pTagId, bool isTagLabel);
void sipHdrFromto_cleanup(void* data);
void* sipHdrFromto_alloc();


#endif
