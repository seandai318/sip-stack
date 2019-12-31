#include "osDebug.h"
#include "osMBuf.h"
#include "osList.h"

#include "sipConfig.h"
#include "sipMsgRequest.h"
#include "sipHeader.h"
#include "sipHdrRoute.h"
#include "sipHdrVia.h"
#include "sipHdrMisc.h"
#include "sipHdrFromto.h"


sipMsgResponse_t* sipServerBuild100Response(sipMsgDecoded_t* sipMsgInDecoded)
{
    osStatus_e status = OS_STATUS_OK;
    sipMsgResponse_t* pRsp = NULL;

    if(!sipMsgInDecoded)
    {
        logError("null pointer, sipMsgInDecoded.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	sipHdrName_e addHdrList[] = {SIP_HDR_TO, SIP_HDR_CONTENT_LENGTH};
    pRsp = sipMsgCreateResponse(sipMsgInDecoded, 100, addHdrList, sizeof(addHdrList)/sizeof(addHdrList[0]));
    if(!pRsp)
    {
        logError("fails to create sipMsgResponse_t.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

EXIT:
    return pRsp;
}


/* add default hdr in addHdrList.  for other hdr that needs customization, one after another add using sipMsgAddHdr */
sipMsgResponse_t* sipServerBuild18x200Response(sipMsgDecoded_t* sipMsgInDecoded, sipResponse_e rspCode)
{
    osStatus_e status = OS_STATUS_OK;
    sipMsgResponse_t* pRsp = NULL;

    if(!sipMsgInDecoded)
    {
        logError("null pointer, sipMsgInDecoded.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    sipHdrName_e addHdrList[] = {SIP_HDR_RECORD_ROUTE, SIP_HDR_TO};
    pRsp = sipMsgCreateResponse(sipMsgInDecoded, rspCode, addHdrList, sizeof(addHdrList)/sizeof(addHdrList[0]));
    if(!pRsp)
    {
        logError("fails to create sipMsgResponse_t.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

EXIT:
    return pRsp;
}


sipMsgResponse_t* sipServerBuild4xx5xx6xxResponse(sipMsgDecoded_t* sipMsgInDecoded, sipResponse_e rspCode)
{
    osStatus_e status = OS_STATUS_OK;
    sipMsgResponse_t* pRsp = NULL;

    if(!sipMsgInDecoded)
    {
        logError("null pointer, sipMsgInDecoded.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    sipHdrName_e addHdrList[] = {SIP_HDR_RECORD_ROUTE, SIP_HDR_TO, SIP_HDR_CONTENT_LENGTH};
    pRsp = sipMsgCreateResponse(sipMsgInDecoded, rspCode, addHdrList, sizeof(addHdrList)/sizeof(addHdrList[0]));
    if(!pRsp)
    {
        logError("fails to create sipMsgResponse_t.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

EXIT:
    return pRsp;
}


osStatus_e sipServerCommonHdrBuildEncode(sipMsgDecoded_t* sipMsgInDecoded, sipMsgResponse_t* pResp, sipHdrName_e hdrCode)
{
	osStatus_e status = OS_STATUS_OK;
    sipHdrAddCtrl_t ctrl = {true, false, false, NULL};

	if(!sipMsgInDecoded || !pResp)
	{
		logError("null pointer, sipMsgInDecoded=%p, pResp=%p.", sipMsgInDecoded, pResp);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	switch(hdrCode)
	{
		case SIP_HDR_CONTENT_LENGTH:
		{
			uint32_t contentLen = 0;
			sipHdrAddCtrl_t ctrl = {false, false, false, NULL};
	        status = sipMsgAddHdr(pResp->sipMsg, hdrCode, &contentLen, NULL, ctrl);
   	   	    if(status != OS_STATUS_OK)
       	    {
               	logError("fails to sipMsgAddHdr for SIP_HDR_CONTENT_LENGTH.");
               	status = OS_ERROR_INVALID_VALUE;
               	goto EXIT;
           	}
			break;
		}
		case SIP_HDR_TO:
		{
			//if toTag has not been generated, generate one
			if(pResp->toTag.l == 0)
			{
    			status = sipHdrFromto_generateTagId(&pResp->toTag);
    			if(status != OS_STATUS_OK)
    			{
        			logError("fail to generate to tagId.");
        			goto EXIT;
    			}
			}
    		sipRawHdrList_t* hdrRawList= sipHdrGetRawValue(sipMsgInDecoded, hdrCode);
        	if(!hdrRawList)
        	{
            	logError("fails to get raw hdrRawList for hdrCode (%d).", hdrCode);
            	status = OS_ERROR_INVALID_VALUE;
            	goto EXIT;
        	}

        	status = sipMsgAddHdr(pResp->sipMsg, hdrCode, hdrRawList, NULL, ctrl);
        	if(status != OS_STATUS_OK)
        	{
            	logError("fails to sipMsgAddHdr for SIP_HDR_TO.");
            	status = OS_ERROR_INVALID_VALUE;
            	goto EXIT;
        	}
			sipHdrParamNameValue_t tagInfo={{"tag", 3}, pResp->toTag};
			sipMsgHdrAppend(pResp->sipMsg, &tagInfo, ';');
			break;
		}
		default:
		{
            sipRawHdrList_t* hdrRawList= sipHdrGetRawValue(sipMsgInDecoded, hdrCode);
            if(!hdrRawList)
            {
                logError("fails to get raw hdrRawList for hdrCode (%d).", hdrCode);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            status = sipMsgAddHdr(pResp->sipMsg, hdrCode, hdrRawList, NULL, ctrl);
            if(status != OS_STATUS_OK)
            {
                logError("fails to get raw hdrRawList for SIP_HDR_TO.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }
			break;
		}
	}
    
EXIT:
	return status;
}

