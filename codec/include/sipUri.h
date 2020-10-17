/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipUri.h
 ********************************************************/

#ifndef _SIP_URI_H
#define _SIP_URI_H

#include "osTypes.h"
#include "osMBuf.h"
#include "osList.h"

#include "sipHeaderData.h"
#include "sipParsing.h"
#include "sipUserinfo.h"
#include "sipHostport.h"
#include "sipUriparam.h"


typedef enum {
	URI_TYPE_SIP,
	URI_TYPE_SIPS,
	URI_TYPE_TEL,
} sipUriType_e;


typedef struct sipUri {
	osPointerLen_t sipUser;		//sipUser=sipUriType + userInfo + host, i.e., like sip:xyz@domain.com, port part is not included
    sipUriType_e sipUriType;
    sipUserinfo_t userInfo;
    sipHostport_t hostport;
	sipUriparam_t uriParam;
	osList_t headers;
} sipUri_t;


typedef struct sipUriExt {
	osPointerLen_t displayName;
	sipUri_t uri;
} sipUriExt_t;


osStatus_e sipParamUri_parse(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pParsingStatus);
osStatus_e sipParamUri_encode(osMBuf_t* pSipBuf, sipUri_t* pUri);
osStatus_e sipParamUri_create(sipUri_t* pUri);
osStatus_e sipParamUri_build(sipUri_t* pUri, sipUriType_e uriType, char* user, uint32_t userLen, char* password, uint32_t pwLen, char* host, uint32_t hostLen, uint32_t port);
osStatus_e sipParamUri_addDisplayName(sipUriExt_t* pUriExt, osPointerLen_t* displayName);
osStatus_e sipParamUri_addParam(sipUri_t* pUri, sipUriParam_e paramType, void* pParam);
bool sipUri_isTelSub(sipUri_t* pUri);
osStatus_e sipParamUri_code2name(sipUriType_e uriType, osPointerLen_t* uriStr);
osStatus_e sipParamUri_getUriFromRawHdrValue(osPointerLen_t* pHdrValue, osPointerLen_t* pSipUri);
osStatus_e sipParamUri_getUriFromSipMsg(osMBuf_t* pSipBuf, osPointerLen_t* pSipUri, sipHdrName_e hdrCode);
osPointerLen_t* sipUri_getUser(sipUri_t* pUri);
osPointerLen_t* sipUri_getPassword(sipUri_t* pUri);
osPointerLen_t* sipUri_getHost(sipUri_t* pUri);
osPointerLen_t* sipUri_getPort(sipUri_t* pUri);
osPointerLen_t* sipUri_getTransport(sipUri_t* pUri);
uint32_t sipUri_getUriparamMask(sipUri_t* pUri);
osList_t* sipUri_getOtherparam(sipUri_t* pUri);
//reallocate the oslist memory, include the memory of osListElement->data 
void sipUri_cleanup(void* data);
//reallocate the oslist memory, but the memory of osListElement->data is left as is
void sipParamUri_clear(sipUri_t* pUri);

#endif
