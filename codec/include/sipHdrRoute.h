/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrRoute.h
 ********************************************************/

#ifndef _SIP_HDR_ROUTE_H
#define _SIP_HDR_ROUTE_H

#include "osList.h"
#include "osMemory.h"
#include "osMBuf.h"
#include "sipGenericNameParam.h"


/* this is defined in sipGenericNameParam.h, copy here to help understand the sipHdrRouteElement_t data structure 
typedef struct sipHdr_genericNameParam {
    osPointerLen_t displayName;
    sipUri_t uri;
    osList_t genericParam;      //for both decode and encode, each element is sipHdrParamNameValue_t
} sipHdrGenericNameParam_t;
*/

#define sipHdrRouteElement_t	sipHdrGenericNameParam_t
#define sipHdrRouteElementPT_t	sipHdrGenericNameParamPt_t

#define sipHdrRoute_t sipHdrMultiGenericNameParam_t


// in sipParserHdr_route, isNameaddr=true
extern sipParserHdr_multiNameParam_h sipParserHdr_route;

osStatus_e sipHdrRoute_create(void* pRoute, void* uri, void* other);
osStatus_e sipHdrRoute_encode(osMBuf_t* pSipBuf, void* pRouteDT, void* pData);
osStatus_e sipHdrRoute_build(sipHdrRouteElementPT_t* pRoute, sipUri_t* pUri, osPointerLen_t* displayname);
osStatus_e sipHdrRoute_addParam(sipHdrRouteElementPT_t* pRoute, sipParamNameValue_t* pNameValue);
void sipHdrRoute_cleanup(void* data);
void* sipHdrRoute_alloc();


#endif
