/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipParsing.h
 ********************************************************/

#ifndef _SIP_PARSING_H
#define _SIP_PARSING_H


#include "osTypes.h" 
#include "osMBuf.h"
#include "osList.h"
#include "osPL.h"


#define SIPP_MAX_PARAM_NUM	10
#define SIP_TOKEN_MAX_NUM	10
#define SIP_TOKEN_EOH	  0x03
#define SIP_TOKEN_INVALID 0x04

#define SIP_IS_USER_UNRESERVED(a) (!(a^'&') || !(a^'=') || !(a^'+') || !(a^'$') || !(a^',') || !(a^';') || !(a^'?') || !(a^'/'))
#define SIP_IS_MARK(a) (!(a^'-') || !(a^'_') || !(a^'.') || !(a^'!') || !(a^'~') || !(a^'*') || !(a^0x60) || !(a^'(') || !(a^')'))
#define SIP_IS_ALPHA(a) ((a >= 'A' && a <= 'Z') || (a >= 'a' && a <= 'z'))
#define SIP_IS_ALPHA_CAPS(a) (a >= 'A' && a <= 'Z')
#define SIP_IS_DIGIT(a) (a >= '0' && a <= '9')
#define SIP_IS_ESCAPED(a) (!(a^'%'))
#define SIP_IS_HEXDIG(a) (SIP_IS_DIGIT(a) || (a >= 'a' && (a <= 'f') || (a >= 'A' && a <= 'F'))
#define SIP_IS_ALPHANUM(a) (SIP_IS_ALPHA(a) || SIP_IS_DIGIT(a))
#define SIP_IS_UNRESERVED(a) (SIP_IS_ALPHANUM(a) || SIP_IS_MARK(a))
#define SIP_IS_PARAM_UNRESERVED(a) (!(a^'[') || !(a^']') || !(a^'/') || !(a^':') || !(a^'&') || !(a^'+') || !(a^'$'))
#define SIP_IS_TOKEN(a) ((SIP_IS_ALPHANUM(a) || (!(a^'-') || !(a^'.') || !(a^'!') || !(a^'%') || !(a^'*') || !(a^'_') || !(a^'+') || !(a^'`') || !(a^0x27) || !(a^'~')))
#define SIP_IS_PARAMCHAR(a) (SIP_IS_PARAM_UNRESERVED(a) || SIP_IS_UNRESERVED(a) || SIP_IS_ESCAPED(a))
#define SIP_IS_LWS(a) (!(a^0x20) || !(a^0x9) || !(a^0xa) || !(a^0xd))


//#define SIP_IS_MATCH_TOKEN(a, token, tokenStart, tokenStop) {int retval=0; for(int i=tokenStart; i<tokenStop; i++) {if (a==token[i]) {retval=1; break;}} retval; }
#define SIP_INIT_PARSINGINFO(a, num) ({for(int iwsx=0; iwsx<num; iwsx++) {a[iwsx].tokenNum=0; a[iwsx].extTokenNum=0;a[iwsx].arg=NULL;}})

typedef enum {
//	SIPP_PARAM_ACCEPTED_CONTACT,
	SIPP_PARAM_GENERIC_NAMEPARAM,	
	SIPP_PARAM_HEADERS,
	SIPP_PARAM_HOST,
	SIPP_PARAM_HOSTPORT,
//	SIPP_PARAM_PANI,
	SIPP_PARAM_MEDIA,	
	SIPP_PARAM_PASSWORD,
	SIPP_PARAM_PORT,
	SIPP_PARAM_SIP,
	SIPP_PARAM_STR_GENPARAM,
	SIPP_PARAM_SUBMEDIA,
	SIPP_PARAM_URI,
	SIPP_PARAM_URIINFO,
	SIPP_PARAM_URIPARAM,
	SIPP_PARAM_USER,
} sipParsing_param_e;


typedef enum {
	SIPP_STATUS_OK,
	SIPP_STATUS_EMPTY,
	SIPP_STATUS_OTHER_VALUE,
	SIPP_STATUS_TOKEN_NOT_MATCH,
	SIPP_STATUS_DUPLICATE_PARAM,
} sipParsing_status_e;


typedef struct sipParsingInfo {
	uint8_t token[SIP_TOKEN_MAX_NUM];
	uint8_t tokenNum;	//locally added token in sipParsing_setParsingInfo() + token passed in from parentParsing
	uint8_t extTokenNum;	//the number of token passed in from parentParsing (pParentParsingInfo in sipParsing_setParsingInfo function). This is the token number for next parameters not belong to the setParsingInfo of this parameter.  For example, for sip URI, after hostport, ';' and '?' are 2 possible tokens for uri-parameter and headers, extTokenNum=2 for uir-param sipParsing_getHdrValue.  The extToken must be put at the end of the token list
	void* arg;
} sipParsingInfo_t;


typedef struct sipParsingStatus {
	uint8_t tokenMatched;
	bool isEOH;		//End Of Hdr
	sipParsing_status_e status;
} sipParsingStatus_t;

#if 0
typedef struct sipParsing_param {
	osPointerLen_t name;
	osPointerLen_t value;
} sipParsing_param_t;
#endif

//"/r/n/" is not passed in as token, the function has to detect on its own, but actually the first pass already excluded /r/n, so no need to detect it, except for /r/n in the middle of a hdr
typedef osStatus_e (*sipParsing_h)( osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pStatus);
typedef void (*sipParsing_cleanup_h)(void* arg);

typedef struct sipParsingABNF {
	uint8_t a;		//minimum occurance of a matched pattern
	uint8_t b;		//maximum occurane of a matched pattern
	uint8_t extToken;	//external token for the starting of next parameter
	bool isInternalMatchContinue;	//if a token matchs a local token (not the one from the parent token), shall break and go to next ABNF? or continue the current ABNF?
	sipParsing_param_e paramName;
	sipParsing_h parsingFunc;
	sipParsing_cleanup_h cleanup;	//only relevant when the current parameter is optional and there is mandatory parameter afterwards, particular for the first parameter in ABNF since there is no way to determine when this optional parameter shall be parsed until it is parsed
} sipParsingABNF_t;



osStatus_e sipParsing_getHdrValue(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingABNF_t* sipABNF, sipParsingInfo_t* sippInfo, uint8_t sbnfNum, sipParsingStatus_t* pStatus);


typedef osStatus_e (*sipParsing_setUniqueParsingInfo_h)(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg);

void sipParsing_setParsingInfo(sipParsingABNF_t sipABNF[], int abnfNum, sipParsingInfo_t* pParentParsingInfo, sipParsingInfo_t sippParsingInfo[], sipParsing_setUniqueParsingInfo_h setUniqueParsingInfo_handler);
osStatus_e sipParsing_plGetParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParsingInfo, sipParsingStatus_t* pStatus);
osStatus_e sipParsing_listPLGetParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParsingInfo, sipParsingStatus_t* pStatus);
osStatus_e sipParsing_listAddParam(osList_t *pList, char* nameParam, size_t nameLen, char* valueParam, size_t valueLen);
bool sipParsing_isParamExist(osList_t *pList, char* param, int len);


bool SIP_IS_MATCH_TOKEN(char a, char token[], int tokenStart, int tokenStop);
bool SIP_HAS_TOKEN_EOH(char token[], int tokenNum);


#endif
