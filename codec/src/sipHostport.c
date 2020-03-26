#include "osDebug.h"

#include "sipParsing.h"
#include "sipHostport.h"


static sipParsingABNF_t sipHostportABNF[]={ \
    {1, 1, SIP_TOKEN_INVALID,   0, SIPP_PARAM_HOST, sipParsing_plGetParam, NULL}, \
    {0, 1, ':',                 0, SIPP_PARAM_PORT, sipParsing_plGetParam, NULL}, \
    {0, 1, SIP_TOKEN_EOH,       0, SIPP_PARAM_PORT, sipParsing_plGetParam, NULL}};


static osStatus_e sipParsing_setHostportParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg);
static int sipP_hostportNum = sizeof(sipHostportABNF) / sizeof(sipParsingABNF_t);


osStatus_e sipParser_hostport(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pParsingStatus)
{
	osStatus_e status = OS_STATUS_OK;
    sipParsingInfo_t sippParsingInfo[sipP_hostportNum];
	SIP_INIT_PARSINGINFO(sippParsingInfo, sipP_hostportNum);

	if(!pSipMsg || !pParsingStatus || !pParentParsingInfo)
	{
		logError("NULL pointer is passed in, pSipMsg=%p, pParsingStatus=%p, pParentParsingInfo=%p.", pSipMsg, pParsingStatus, pParentParsingInfo);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    sipParsing_setParsingInfo(sipHostportABNF, sipP_hostportNum, pParentParsingInfo, sippParsingInfo, sipParsing_setHostportParsingInfo);

    status = sipParsing_getHdrValue(pSipMsg, hdrEndPos, sipHostportABNF, sippParsingInfo, sipP_hostportNum, pParsingStatus);

EXIT:
	return status;
}


static osStatus_e sipParsing_setHostportParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg)
{
    osStatus_e status = OS_STATUS_OK;

    sipHostport_t* pHostport = (sipHostport_t*) arg;

    switch (paramName)
    {
        case SIPP_PARAM_HOST:
            pSippParsingInfo->arg = &pHostport->host;
        	break;
				
		case SIPP_PARAM_PORT:
            pSippParsingInfo->arg = &pHostport->port;
            break;

	    default:
    	    logError("unexpected parameter, sipHostportABNF.paramName=%s.", paramName);
        	status = OS_ERROR_INVALID_VALUE;
        	goto EXIT;
    }

EXIT:
	return status;
}


void sipHostport_cleanup(void* data)
{
	//nothing to cleanup
	return;
}
