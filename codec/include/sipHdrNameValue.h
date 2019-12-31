#ifndef _SIP_HDR_NAME_VALUE_H
#define _SIP_HDR_NAME_VALUE_H

#include "osTypes.h"
#include "osList.h"
#include "osMBuf.h"

#include "sipParamNameValue.h"


typedef struct sipHdr_nameValue {
	osList_t nvParam;	//each element points to sipParamNameValue_t
} sipHdr_nameValue_t;


typedef struct sipHdr_name {
    osList_t nParam;   //each element points to sipParamName_t
} sipHdr_name_t;


osStatus_e sipHdrNameValue_build(sipHdr_nameValue_t* pHdr);
osStatus_e sipHdrNameValue_addParam(sipHdr_nameValue_t* pHdr, sipParamNameValue_t* pNameValue);
osStatus_e sipHdrNameValue_encode(osMBuf_t* pSipBuf, void* pHdrDT, void* pData);
osStatus_e sipHdrName_build(sipHdr_name_t* pHdr);
osStatus_e sipHdrName_addParam(sipHdr_name_t* pHdr, sipParamName_t* pName);
osStatus_e sipHdrName_encode(osMBuf_t* pSipBuf, void* pHdrDT, void* pData);


#endif
