#ifndef _SIP_CONFIG_H
#define _SIP_CONFIG_H


#include "osPL.h"

#define SIP_MAX_MSG_SIZE	5000
#define SIP_MAX_SAME_HDR_NUM	0xff	//the max number of headers that have the same name
#define SIP_MAX_VIA_BRANCH_ID_LEN	100
#define SIP_MAX_TAG_ID_LENGTH	32
#define SIP_MAX_CALL_ID_LEN		100
#define SIP_MAX_HDR_MODIFY_NUM	25
#define SIP_HDR_EOF 0xFFFFFFFF		//used for the hdr insertion.  insert a header to the end of a sip msg (excluding SDP)
#define SIP_HDR_BOTTOM	0xFF		//the bottom header value for a header

#define SIP_TIMER_T1	500
#define SIP_TIMER_T2	4000
#define SIP_TIMER_T4	5000
#define SIP_TIMER_B		(64*SIP_TIMER_T1)
#define SIP_TIMER_C		180000
#define SIP_TIMER_D		32000
#define SIP_TIMER_F		(64*SIP_TIMER_T1)
#define SIP_TIMER_H		(64*SIP_TIMER_T1)
#define SIP_TIMER_I		SIP_TIMER_T4
#define SIP_TIMER_J		(64*SIP_TIMER_T1)
#define SIP_TIMER_K		SIP_TIMER_T4

char* sipConfig_getHostIP();
int sipConfig_getHostPort();
void sipConfig_getHost(osPointerLen_t* host, int* port);


#endif
