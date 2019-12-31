#ifndef _SIP_USERINFO_H
#define _SIP_USERINFO_H

#include "osPL.h"

#include "sipParsing.h"


typedef struct sipUser {
	bool isTelSub;
	osPointerLen_t user;
} sipUser_t;

typedef struct sipUserinfo {
	sipUser_t sipUser;
	osPointerLen_t password;
} sipUserinfo_t;


osStatus_e sipParser_userinfo(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pParsingStatus);
void sipUserinfo_cleanup(void* data);

#endif
