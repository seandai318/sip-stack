#ifndef _SIP_REGISTRAR_H
#define _SIP_REGISTRAR_H

#include "osTypes.h"
#include "osPL.h"
#include "osMBuf.h"

#include "sipHeader.h"

typedef enum {
	SIP_REGSTATE_REGISTERED,
	SIP_REGSTATE_NOT_REGISTERED,
} sipRegState_e;


typedef struct sipRegistrar {
	sipRegState_e regState;
	osMBuf_t* sipRegMsg;
	osPointerLen_t user;
	sipHdrDecoded_t contact;	//for now we only allow one contact per user	
	uint64_t expiryTimerId;
} sipRegistrar_t;

#endif
