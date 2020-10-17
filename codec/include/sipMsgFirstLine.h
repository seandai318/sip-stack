/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipMsgFirstLine.h
 ********************************************************/

#ifndef _SIP_MSG_FIRST_LINE_H
#define _SIP_MSG_FIRST_LINE_H


#include "osPL.h"
#include "osMBuf.h"
#include "sipUri.h"


//Status-Line  =  SIP-Version SP Status-Code SP Reason-Phrase CRLF
//Request-Line  =  Method SP Request-URI SP SIP-Version CRLF


typedef enum {
    SIP_MSG_REQUEST,
    SIP_MSG_RESPONSE,
    SIP_MSG_ACK,
} sipMsgType_e;


typedef enum {
	SIP_METHOD_INVALID,
	SIP_METHOD_ACK,
	SIP_METHOD_BYE,
	SIP_METHOD_CANCEL,
	SIP_METHOD_INFO,
	SIP_METHOD_INVITE,
	SIP_METHOD_MESSAGE,
	SIP_METHOD_NOTIFY,
	SIP_METHOD_OPTION,
	SIP_METHOD_REGISTER,
	SIP_METHOD_SUBSCRIBE,
	SIP_METHOD_REFER,
	SIP_METHOD_UPDATE,
} sipRequest_e;


typedef enum {
	SIP_RESPONSE_INVALID=0,
	SIP_RESPONSE_100=100,  	//Trying
    SIP_RESPONSE_180=180,  	//Ringing
    SIP_RESPONSE_181=181,  	//Call Is Being Forwarded
    SIP_RESPONSE_182=182,  	//Queued
    SIP_RESPONSE_183=183,  	//Session Progress
	SIP_RESPONSE_200=200,
	SIP_RESPONSE_202=202,
	SIP_RESPONSE_300=300,
	SIP_RESPONSE_301=301,
	SIP_RESPONSE_302=302,
	SIP_RESPONSE_305=305,
	SIP_RESPONSE_380=380,
	SIP_RESPONSE_400=400,	//Bad Request
	SIP_RESPONSE_401=401,	//Unauthorized
    SIP_RESPONSE_402=402,  	//Payment Required
    SIP_RESPONSE_403=403,  	//Forbidden
    SIP_RESPONSE_404=404,  	//Not Found
    SIP_RESPONSE_405=405,  	//Method Not Allowed
    SIP_RESPONSE_406=406,  	//Not Acceptable
    SIP_RESPONSE_407=407,  	//Proxy Authentication Required
    SIP_RESPONSE_408=408,  	//Request Timeout
    SIP_RESPONSE_410=410,  	//Gone
    SIP_RESPONSE_413=413,  	//Request Entity Too Large
    SIP_RESPONSE_414=414,  	//Request-URI Too Large
    SIP_RESPONSE_415=415,  	//Unsupported Media Type
    SIP_RESPONSE_416=416,  	//Unsupported URI Scheme
    SIP_RESPONSE_420=420,  	//Bad Extension
    SIP_RESPONSE_421=421,  	//Extension Required
    SIP_RESPONSE_423=423,  	//Interval Too Brief
    SIP_RESPONSE_480=480,  	//Temporarily not available
    SIP_RESPONSE_481=481,  	//Call Leg/Transaction Does Not Exist
    SIP_RESPONSE_482=482,  	//Loop Detected
    SIP_RESPONSE_483=483,  	//Too Many Hops
    SIP_RESPONSE_484=484,  	//Address Incomplete
    SIP_RESPONSE_485=485,  	//Ambiguous
    SIP_RESPONSE_486=486,  	//Busy Here
    SIP_RESPONSE_487=487,  	//Request Terminated
    SIP_RESPONSE_488=488,  	//Not Acceptable Here
    SIP_RESPONSE_491=491,  	//Request Pending
    SIP_RESPONSE_493=493,  	//Undecipherable
	SIP_RESPONSE_500=500,  	//Internal Server Error
    SIP_RESPONSE_501=501,  	//Not Implemented
    SIP_RESPONSE_502=502,  	//Bad Gateway
    SIP_RESPONSE_503=503,  	//Service Unavailable
    SIP_RESPONSE_504=504,  	//Server Time-out
    SIP_RESPONSE_505=505,  	//SIP Version not supported
    SIP_RESPONSE_513=513,  	//Message Too Large
  	SIP_RESPONSE_600=600,  	//Busy Everywhere
    SIP_RESPONSE_603=603,  	//Decline
    SIP_RESPONSE_604=604,  	//Does not exist anywhere
    SIP_RESPONSE_606=606,  	//Not Acceptable
} sipResponse_e;


typedef struct sipReqLine {
	sipRequest_e sipReqCode;
	sipUri_t sipUri;
} sipReqLine_t;


typedef struct sipReqLinePT {
    sipRequest_e sipReqCode;
    sipUri_t* pSipUri;
} sipReqLinePT_t;


typedef struct sipRespLine {
	sipResponse_e sipStatusCode;
	osPointerLen_t reason;
} sipStatusLine_t;


typedef struct sipFirstline {
	bool isReqLine;
	union {
		sipReqLine_t sipReqLine;
		sipStatusLine_t sipStatusLine;
	} u;
} sipFirstline_t;


bool sipIsStatusCodeValid(int statusCode);
sipRequest_e sipMsg_method2Code(osPointerLen_t* pMethod);
osStatus_e sipMsg_code2Method(sipRequest_e code, osPointerLen_t* pMethod);
osStatus_e sipParser_firstLine(osMBuf_t* pSipMsg, sipFirstline_t* pFL, bool isParseUri);
osStatus_e sipHdrFirstline_encode(osMBuf_t* pSipBuf, void* pReqLine, void* other);
osStatus_e sipHdrFirstline_create(void* pReqLineDT, void* pUriDT, void* pReqTypeDT);
osStatus_e sipHdrFirstline_respEncode(osMBuf_t* pSipBuf, void* pRespCode, void* other);
const char* sipHdrFirstline_respCode2status(sipResponse_e respCode);
sipRequest_e sipMsg_getReqCode(osPointerLen_t* reqMethod);

//other than normal encode, also return sipFirstline_t
osStatus_e sipHdrFirstline_encode1(osMBuf_t* pSipBuf, void* pReqLineDT, void* other, sipFirstline_t* pFirstLine);
osStatus_e sipHdrFirstline_respEncode1(osMBuf_t* pSipBuf, void* pRespCode, void* other, sipFirstline_t* pFirstLine);

void sipFirstLine_cleanup(void* pData);
void* sipFirstLine_alloc();


#endif
