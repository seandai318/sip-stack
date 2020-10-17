/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrAcceptedContact.h
 ********************************************************/

#ifndef _SIPHDR_ACCEPTED_CONTACT_H
#define _SIPHDR_ACCEPTED_CONTACT_H


#include "osMBuf.h"
#include "osList.h"

typedef struct sipHdrAcceptedContact {
	osList_t acList;	//each element contains 'osList_t genericParam' defined in sipHdrGenericNameParam_t
} sipHdrAcceptedContact_t;


osStatus_e sipHdr_acceptedContact(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrAcceptedContact_t* pAC);
void sipHdrAC_cleanup(void* data);
void* sipHdrAC_alloc();

#endif
