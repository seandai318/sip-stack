#include <string.h>
#include <arpa/inet.h>

#include "osXmlParser.h"
#include "osSockAddr.h"

#include "sipConfig.h"
#include "sipConfigPriv.h"

#if 0
typedef struct {
    sipConfig_xmlDataName_e eDataName;
    osPointerLen_t dataName;
    osXmlDataType_e dataType;
    union {
        bool xmlIsTrue;
        uint64_t xmlInt;
        osPointerLen_t xmlStr;
    };
} sipConfig_xmlData_t;
#endif

osXmlData_t sipConfig_xmlData[SIP_XML_MAX_DATA_NAME_NUM] = {
    {SIP_XML_TIMER_C, 					{"SIP_TIMER_C", strlen("SIP_TIMER_C")},	OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_TIMER_D,					{"SIP_TIMER_D", strlen("SIP_TIMER_D")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_TIMER_T1,					{"SIP_TIMER_T1", strlen("SIP_TIMER_T1")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_TIMER_T2,					{"SIP_TIMER_T2", strlen("SIP_TIMER_T2")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_TIMER_T4,					{"SIP_TIMER_T4", strlen("SIP_TIMER_T4")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_REG_MIN_EXPIRE,			{"SIP_REG_MIN_EXPIRE", strlen("SIP_REG_MIN_EXPIRE")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_REG_MAX_EXPIRE,			{"SIP_REG_MAX_EXPIRE", strlen("SIP_REG_MAX_EXPIRE")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_CONFIG_LOCAL_IP,			{"SIP_CONFIG_LOCAL_IP", strlen("SIP_CONFIG_LOCAL_IP")}, OS_XML_DATA_TYPE_XS_STRING, 0},
    {SIP_XML_REG_DEFAULT_EXPIRE,		{"SIP_REG_DEFAULT_EXPIRE", strlen("SIP_REG_DEFAULT_EXPIRE")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_CONFIG_LISTEN_PORT,		{"SIP_CONFIG_LISTEN_PORT", strlen("SIP_CONFIG_LISTEN_PORT")}, OS_XML_DATA_TYPE_XS_SHORT, 0},
    {SIP_XML_CONFIG_USE_IMS_CLIENT,		{"SIP_CONFIG_USE_IMS_CLIENT", strlen("SIP_CONFIG_USE_IMS_CLIENT")}, OS_XML_DATA_TYPE_XS_BOOLEAN, 0},
    {SIP_XML_CONFIG_TIMEOUT_MULTIPLE,	{"SIP_CONFIG_TIMEOUT_MULTIPLE", strlen("SIP_CONFIG_TIMEOUT_MULTIPLE")}, OS_XML_DATA_TYPE_XS_SHORT, 0},
    {SIP_XML_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE,		{"SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE", strlen("SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_CONFIG_TRANSACTION_HASH_BUCKET_SIZE,		{"SIP_CONFIG_TRANSACTION_HASH_BUCKET_SIZE", strlen("SIP_CONFIG_TRANSACTION_HASH_BUCKET_SIZE")}, OS_XML_DATA_TYPE_XS_LONG, 0},
    {SIP_XML_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME,	{"SIP_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME", strlen("SIP_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME")}, OS_XML_DATA_TYPE_XS_LONG, 0}};



void sipConfig_init(char* configFolder)
{
	osXmlDataCallbackInfo_t cbInfo={sipConfig_xmlData, SIP_XML_MAX_DATA_NAME_NUM}; 
    if(osXml_getLeafValue(configFolder, SIP_CONFIG_XSD_FILE_NAME, SIP_CONFIG_XML_FILE_NAME, osXml_xmlCallback, &cbInfo) != OS_STATUS_OK)
    {
        logError("fails to sipConfig_getXmlConfig.");
        return;
    }
}


void* sipConfig_getConfig(sipConfig_xmlDataName_e dataName)
{
	switch(dataName)
	{
		case SIP_XML_TIMER_C:
    	case SIP_XML_TIMER_D:
    	case SIP_XML_TIMER_T1:
    	case SIP_XML_TIMER_T2:
    	case SIP_XML_TIMER_T4:
    	case SIP_XML_REG_MIN_EXPIRE:
    	case SIP_XML_REG_MAX_EXPIRE:
    	case SIP_XML_REG_DEFAULT_EXPIRE:
    	case SIP_XML_CONFIG_LISTEN_PORT:
    	case SIP_XML_CONFIG_TIMEOUT_MULTIPLE:
    	case SIP_XML_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE:
    	case SIP_XML_CONFIG_TRANSACTION_HASH_BUCKET_SIZE:
    	case SIP_XML_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME:
			return &sipConfig_xmlData[dataName].xmlInt;
			break;
		case SIP_XML_CONFIG_USE_IMS_CLIENT:
			return &sipConfig_xmlData[dataName].xmlIsTrue;
			break;
		case SIP_XML_CONFIG_LOCAL_IP:
			return &sipConfig_xmlData[dataName].xmlStr;
			break;
        default:
            logError("dataName is not defined(%d).", dataName);
            break;
    }

    return NULL;
}




osPointerLen_t* sipConfig_getHostIP()
{
	return &SIP_CONFIG_LOCAL_IP;
}


int sipConfig_getHostPort()
{
	return SIP_CONFIG_LISTEN_PORT;
}


void sipConfig_getHost(osPointerLen_t* host, int* port)
{
	*host = SIP_CONFIG_LOCAL_IP;
	*port = SIP_CONFIG_LISTEN_PORT;
}


void sipConfig_getHost1(struct sockaddr_in* pHost)
{
	osIpPort_t ipPort = {{SIP_CONFIG_LOCAL_IP}, SIP_CONFIG_LISTEN_PORT};
	osConvertPLton(&ipPort, true, pHost);
}



sipTransport_e sipConfig_getTransport(osPointerLen_t* ip, int port)
{
	size_t n = sizeof(sipConfig_peerTpType) / sizeof(sipConfig_peerTpType[0]);
	for(int i=0; i<n; i++)
	{
		if(sipConfig_peerTpType[i].port == port && osPL_strcmp(ip, sipConfig_peerTpType[i].ip)== 0)
		{
			return sipConfig_peerTpType[i].transportType;
		}
	}

	return sipConfig_defaultTpType;
}	
