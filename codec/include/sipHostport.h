/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHostport.h
 ********************************************************/

#ifndef _SIP_HOST_PORT_H
#define _SIP_HOST_PORT_H

#include "osTypes.h"
#include "osPL.h"
#include "sipParsing.h"


typedef struct sipHostport {
	osPointerLen_t host;
	//osPointerLen_t port;
	uint32_t portValue;	//if the value is 0, the port shall not be encoded
	osPointerLen_t port;	//this needs to be at the bottom of the data structure(to make possible of direct initialization)
} sipHostport_t;



osStatus_e sipParser_hostport(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pParsingStatus);
void sipHostport_cleanup(void* data);


#endif
