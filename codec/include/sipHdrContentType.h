/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrContentType.h
 ********************************************************/

#ifndef _SIP_HDR_CONENT_TYPE_H
#define _SIP_HDR_CONENT_TYPE_H


#include "osPL.h"
#include "osList.h"


typedef enum {
	SIP_MEDIA_TYPE_TEXT,
	SIP_MEDIA_TYPE_IMAGE,
	SIP_MEDIA_TYPE_AUDIO,
	SIP_MEDIA_TYPE_VIDEO,
	SIP_MEDIA_TYPE_APPLICATION,
	SIP_MEDIA_TYPE_EXTENSION,
	SIP_MEDIA_TYPE_MESSAGE,
	SIP_MEDIA_TYPE_MULTIPART,
} sipMediaType_e;


typedef enum {
	SIP_MEDIA_SUBTYPE_SDP,
	SIP_MEDIA_SUBTYPE_OTHER,
} sipMSubType_e;


typedef struct sipHdrContentType {
	sipMediaType_e mType;
	sipMSubType_e mSubType;
	osPointerLen_t media;
	osPointerLen_t subMedia;
	osList_t mParamList;	//listElement is: m-attribute=m-value
} sipHdrContentType_t;


osStatus_e sipParserHdr_contentType(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrContentType_t* pContentType);
void* sipHdrContentType_alloc();
void sipHdrContentType_cleanup(void* data);


#endif
