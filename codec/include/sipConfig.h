#ifndef _SIP_CONFIG_H
#define _SIP_CONFIG_H


#include "osPL.h"
#include "sipUriparam.h"


#define SIP_MAX_MSG_SIZE	4000
#define SIP_MAX_SAME_HDR_NUM	0xff	//the max number of headers that have the same name
#define SIP_MAX_VIA_BRANCH_ID_LEN	100
#define SIP_MAX_TAG_ID_LENGTH	32
#define SIP_MAX_CALL_ID_LEN		100
#define SIP_MAX_HDR_MODIFY_NUM	25
#define SIP_HDR_MAX_SIZE		500	//used to reserve memory for a header when needed
#define SIP_HDR_EOF 0xFFFFFFFF		//used for the hdr insertion.  insert a header to the end of a sip msg (excluding SDP)
#define SIP_HDR_BOTTOM	0xFF		//the bottom header value for a header


#define SIP_REG_DEFAULT_EXPIRE	3600
#define SIP_REG_MIN_EXPIRE		600
#define SIP_REG_MAX_EXPIRE	36000

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
#define SIP_TIMER_WAIT_ACK	SIP_TIMER_F


#define SIP_CONFIG_TIMEOUT_MULTIPLE	2

#define SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE			4000
#define SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM			100
#define SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE			12000
#define SIP_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME	30000
#define SIP_CONFIG_TRANSACTION_THREAD_NUM				1			//for sip client
#define SIP_CONFIG_LISTENER_THREAD_NUM					1			//for sip server
#define SIP_CONFIG_LB_HASH_BUCKET_SIZE					1024
#define SIP_CONFIG_TRANSACTION_HASH_BUCKET_SIZE			1024
#define SIP_CONFIG_USE_IMS_CLIENT						true

//#define SIP_CONFIG_LOCAL_IP		"192.168.56.101"
#define SIP_CONFIG_LOCAL_IP			"192.168.1.83"
#define SIP_CONFIG_LISTEN_PORT	5061


char* sipConfig_getHostIP();
int sipConfig_getHostPort();
void sipConfig_getHostStr(char** ppHost, int* port);
void sipConfig_getHost(osPointerLen_t* host, int* port);
sipTransport_e sipConfig_getTransport(osPointerLen_t* ip, int port);

#endif
