/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipParamHeaders.h
 ********************************************************/

#ifndef _SIP_PARAM_HEADERS_H
#define _SIP_PARAM_HEADERS_H


#include "sipParsing.h"




osStatus_e sipParser_headers(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParsingInfo, sipParsingStatus_t* pParsingStatus)
{
	return OS_STATUS_OK;
}

void sipParser_headersCleanup(void* arg)
{
}

#endif
