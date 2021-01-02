/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipCodecUtil.h
 ********************************************************/

#ifndef __SIP_CODEC_UTIL_H
#define __SIP_CODEC_UTIL_H


#include "sipMsgRequest.h"


void sipMsgBuf_copy(sipMsgBuf_t* dest, sipMsgBuf_t* src);


static inline bool sip_isRspCode2xx(uint32_t rspCode)
{
	return rspCode > 199 && rspCode < 300;
}


#endif
