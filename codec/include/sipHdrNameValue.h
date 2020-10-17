/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrNameValue.h
 ********************************************************/

#ifndef _SIP_HDR_NAME_VALUE_H
#define _SIP_HDR_NAME_VALUE_H

#include "osTypes.h"
#include "osList.h"
#include "osMBuf.h"

#include "sipParamNameValue.h"
#include "sipHdrTypes.h"

#if 0
typedef struct sipHdr_nameValue {
	osList_t nvParam;	//each element points to sipParamNameValue_t
} sipHdr_nameValue_t;
#endif

#if 0
typedef struct sipHdr_name {
    osList_t nParam;   //each element points to sipParamName_t
} sipHdr_name_t;
#endif

osStatus_e sipParserHdr_nameList(osMBuf_t* pSipMsg, size_t hdrEndPos, bool isCaps, sipHdrNameList_t* pStrList);
osStatus_e sipParserHdr_nameValueList(osMBuf_t* pSipMsg, size_t hdrEndPos, bool is4Multi, sipHdrNameValueList_t* pNVList);
osStatus_e sipParserHdr_multiNameValueList(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiNameValueList_t* pMultiNVList);
osStatus_e sipParserHdr_MethodParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMethodParam_t* pMP);
osStatus_e sipParserHdr_valueParam(osMBuf_t* pSipMsg, size_t hdrEndPos, bool is4Multi, sipHdrValueParam_t* pVP);
osStatus_e sipParserHdr_multiValueParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiValueParam_t* pMultiVP);
osStatus_e sipParserHdr_slashValueParam(osMBuf_t* pSipMsg, size_t hdrEndPos, bool is4Multi, sipHdrSlashValueParam_t* pSVP);
osStatus_e sipParserHdr_multiSlashValueParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiSlashValueParam_t* pMultiSVP);

osStatus_e sipHdrNameValue_build(sipHdrNameValueList_t* pHdr);
osStatus_e sipHdrNameValue_addParam(sipHdrNameValueList_t* pHdr, sipParamNameValue_t* pNameValue);
osStatus_e sipHdrNameValue_encode(osMBuf_t* pSipBuf, void* pHdrDT, void* pData);
osStatus_e sipHdrName_build(sipHdrNameList_t* pHdr);
osStatus_e sipHdrName_addParam(sipHdrNameList_t* pHdr, sipParamName_t* pName);
osStatus_e sipHdrName_encode(osMBuf_t* pSipBuf, void* pHdrDT, void* pData);


#endif
