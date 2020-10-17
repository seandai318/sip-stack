/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrPani.h
 ********************************************************/

#ifndef _SIPHDR_PANI_H
#define _SIPHDR_PANI_H


#include "osMBuf.h"
#include "osList.h"

typedef struct sipHdrPANI {
	osList_t paniList;	//each element contains 'osList_t genericParam' defined in sipHdrGenericNameParam_t
} sipHdrPani_t;


osStatus_e sipHdr_pani(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrPani_t* pPani);
void sipHdrPani_cleanup(void* data);
void* sipHdrPani_alloc();


#endif
