/*******************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipCodecUtil.c
 * this function provides some util functions that the sip codec can provide for application
 *******************************************************************************************/


#include "osMemory.h"

#include "sipMsgRequest.h"



//copy src sipMsgBuf_t to the dest.  sipMsgBuf_t.pSipMsg is referred
void sipMsgBuf_copy(sipMsgBuf_t* dest, sipMsgBuf_t* src)
{
	if(!dest || !src)
	{
		return;
	}

	*dest = *src;
	dest->pSipMsg = osmemref(src->pSipMsg);
}
