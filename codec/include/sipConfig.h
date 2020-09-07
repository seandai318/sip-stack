/* Copyright (c) 2019, 2020, Sean Dai
 */

#ifndef _SIP_CONFIG_H
#define _SIP_CONFIG_H

#include <sys/socket.h>
#include <netinet/in.h>

#include "osPL.h"
#include "sipUriparam.h"


#define SIP_CONFIG_MAX_FILE_NAME_SIZE   160
#define SIP_CONFIG_XSD_FILE_NAME        "sipConfig.xsd"
#define SIP_CONFIG_XML_FILE_NAME       	"sipConfig.xml"


//except for DIA_XML_MAX_DATA_NAME_NUM, the order must be sorted based on the data name length.  for the data name with the same len, their orders do not matter
typedef enum {
	SIP_XML_TIMER_C,
	SIP_XML_TIMER_D,
	SIP_XML_TIMER_T1,
	SIP_XML_TIMER_T2,
	SIP_XML_TIMER_T4,
	SIP_XML_REG_MIN_EXPIRE,
	SIP_XML_REG_MAX_EXPIRE,
    SIP_XML_CONFIG_LOCAL_IP,
	SIP_XML_REG_DEFAULT_EXPIRE,
	SIP_XML_CONFIG_LISTEN_PORT,
    SIP_XML_CONFIG_USE_IMS_CLIENT,
	SIP_XML_CONFIG_TIMEOUT_MULTIPLE,
	SIP_XML_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE,
	SIP_XML_CONFIG_TRANSACTION_HASH_BUCKET_SIZE,
	SIP_XML_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME,
    SIP_XML_MAX_DATA_NAME_NUM,
} sipConfig_xmlDataName_e;	
	

#define SIP_MAX_MSG_SIZE			4000
#define SIP_MAX_SAME_HDR_NUM		0xff		//the max number of headers that have the same name
#define SIP_MAX_VIA_BRANCH_ID_LEN	100
#define SIP_MAX_TAG_ID_LENGTH	32
#define SIP_MAX_CALL_ID_LEN		100
#define SIP_MAX_HDR_MODIFY_NUM	25
#define SIP_HDR_MAX_SIZE		500				//used to reserve memory for a header when needed
#define SIP_HDR_EOF 			0xFFFFFFFF		//used for the hdr insertion.  insert a header to the end of a sip msg (excluding SDP)
#define SIP_HDR_BOTTOM			0xFF			//the bottom header value for a header

#define SIP_REG_DEFAULT_EXPIRE		(*(uint64_t*)sipConfig_getConfig(SIP_XML_REG_DEFAULT_EXPIRE))	//default 3600
#define SIP_REG_MIN_EXPIRE			(*(uint64_t*)sipConfig_getConfig(SIP_XML_REG_MIN_EXPIRE))		//default 600
#define SIP_REG_MAX_EXPIRE			(*(uint64_t*)sipConfig_getConfig(SIP_XML_REG_MAX_EXPIRE))		//36000
#define SIP_UE_REG_SEND_SMS_DELAY	30000	//30 sec, move to xml.  if send the stored SMS right after a IMS client registers, the IMS client will respond 200 OK, but does not display to user

#define SIP_TIMER_T1				(*(uint64_t*)sipConfig_getConfig(SIP_XML_TIMER_T1))			//default 500
#define SIP_TIMER_T2				(*(uint64_t*)sipConfig_getConfig(SIP_XML_TIMER_T2))			//default 4000
#define SIP_TIMER_T4				(*(uint64_t*)sipConfig_getConfig(SIP_XML_TIMER_T4))			//default 5000
#define SIP_TIMER_B					(64*SIP_TIMER_T1)
#define SIP_TIMER_C					(*(uint64_t*)sipConfig_getConfig(SIP_XML_TIMER_C))			//default 180000
#define SIP_TIMER_D					(*(uint64_t*)sipConfig_getConfig(SIP_XML_TIMER_D))			//default 32000
#define SIP_TIMER_F					(64*SIP_TIMER_T1)
#define SIP_TIMER_H					(64*SIP_TIMER_T1)
#define SIP_TIMER_I					SIP_TIMER_T4
#define SIP_TIMER_J					(64*SIP_TIMER_T1)
#define SIP_TIMER_K					SIP_TIMER_T4
#define SIP_TIMER_WAIT_ACK			SIP_TIMER_F


#define SIP_CONFIG_TIMEOUT_MULTIPLE						(*(uint64_t*)sipConfig_getConfig(SIP_XML_CONFIG_TIMEOUT_MULTIPLE))	//default 2

#define SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE			5000
#define SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM			100			//it is fixed now, may consider to make it configurable in the future
#define SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE			(*(uint64_t*)sipConfig_getConfig(SIP_XML_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE))	//default 12000
#define SIP_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME	(*(uint64_t*)sipConfig_getConfig(SIP_XML_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME))	//default 30000
#define SIP_CONFIG_TRANSACTION_THREAD_NUM				1			//for sip client, it is fixed now, may consider to make it configurable in the future
#define SIP_CONFIG_LISTENER_THREAD_NUM					1			//for sip server, it is fixed now, may consider to make it configurable in the future
#define SIP_CONFIG_LB_HASH_BUCKET_SIZE					1024		//not used now, may consider to make it configurable in the future when it is used
#define SIP_CONFIG_TRANSACTION_HASH_BUCKET_SIZE			(*(uint64_t*)sipConfig_getConfig(SIP_XML_CONFIG_TRANSACTION_HASH_BUCKET_SIZE))	//default 1024
#define SIP_CONFIG_USE_IMS_CLIENT						(*(bool*)sipConfig_getConfig(SIP_XML_CONFIG_USE_IMS_CLIENT))	//default true

#define SIP_CONFIG_LOCAL_IP			(*(osPointerLen_t*)sipConfig_getConfig(SIP_XML_CONFIG_LOCAL_IP))	//"192.168.1.90"
#define SIP_CONFIG_LISTEN_PORT		(*(uint64_t*)sipConfig_getConfig(SIP_XML_CONFIG_LISTEN_PORT))		//5061



void sipConfig_init(char* configFolder);
void* sipConfig_getConfig(sipConfig_xmlDataName_e dataName);

osPointerLen_t* sipConfig_getHostIP();
int sipConfig_getHostPort();
void sipConfig_getHost(osPointerLen_t* host, int* port);
void sipConfig_getHost1(struct sockaddr_in* pHost);
sipTransport_e sipConfig_getTransport(osPointerLen_t* ip, int port);

#endif
