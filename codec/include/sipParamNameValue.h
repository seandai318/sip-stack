#ifndef _SIP_PARAM_NAME_VALUE_H
#define _SIP_PARAM_NAME_VALUE_H


#include "osTypes.h"
#include "osPL.h"
#include "osList.h"


typedef struct sipHdrParamNameValue {
    osPointerLen_t name;
    osPointerLen_t value;
} sipParamNameValue_t;


#define sipParamName_t osPointerLen_t


bool sipParamNV_isNameExist(osList_t* pParam, osPointerLen_t* pName);
osPointerLen_t* sipParamNV_getValuefromList(osList_t* pParam, osPointerLen_t* pName);
//remove the top NV element from a list, and return the removed NV.
sipParamNameValue_t* sipParamNV_takeTopNVfromList(osList_t* pParamList, uint8_t* newListCount);
//remove a NV element from a list, and return the removed NV.
sipParamNameValue_t* sipParamNV_takeNVfromList(osList_t* pParam, osPointerLen_t* pName);


#endif
