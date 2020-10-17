/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipUriparam.h
 ********************************************************/

#ifndef _SIP_URI_PARAM_H
#define _SIP_URI_PARAM_H

#include "osPL.h"
#include "osList.h"

#include "sipParsing.h"


typedef enum {
	SIP_URI_PARAM_INVALID,
	SIP_URI_PARAM_TRANSPORT,
	SIP_URI_PARAM_USER,
	SIP_URI_PARAM_METHOD,
	SIP_URI_PARAM_TTL,
	SIP_URI_PARAM_MADDR,
	SIP_URI_PARAM_LR,
	SIP_URI_PARAM_OTHER,
} sipUriParam_e;



typedef enum {
    SIP_TRANSPORT_TYPE_UDP,
    SIP_TRANSPORT_TYPE_TCP,
    SIP_TRANSPORT_TYPE_SCTP,
    SIP_TRANSPORT_TYPE_TLS,
    SIP_TRANSPORT_TYPE_ANY,		//unknown transport type
} sipTransport_e;


typedef enum {
	SIP_URI_PARAM_METHOD_INVITE,
	SIP_URI_PARAM_METHOD_ACK,
	SIP_URI_PARAM_METHOD_OPTIONS,
	SIP_URI_PARAM_METHOD_BYE,
	SIP_URI_PARAM_METHOD_CANCEL,
	SIP_URI_PARAM_METHOD_REGISTER,
	SIP_URI_PARAM_METHOD_OTHER,
} sipUriParamMethod_e;
	


typedef struct sipUriparam {
	uint32_t uriParamMask;
	osPointerLen_t  transport;
	osPointerLen_t	user;
	osPointerLen_t	method;
	osPointerLen_t	ttl;
	osPointerLen_t	maddr;
	osList_t other;
} sipUriparam_t;


#if 0
typedef struct sipUriParamOther {
	osPointerLen_t name;
	osPointerLen_t	value;
} sipUriparamOther_t;

note: the above data structure is replaced by sipParamNameValue_t
#endif



osStatus_e sipParser_uriparam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParsingInfo, sipParsingStatus_t* pParsingStatus);
void sipUriparam_cleanup(void* data);

#endif
