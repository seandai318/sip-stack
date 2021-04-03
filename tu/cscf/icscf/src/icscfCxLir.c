/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file icscfLir.c
 * implement 3GPP 28.229 ICSCF Cx LIR functions
 ********************************************************/


#include "osHash.h"
#include "osTimer.h"
#include "osSockAddr.h"

#include "diaMsg.h"
#include "diaIntf.h"
#include "diaCxLir.h"
#include "diaCxAvp.h"

#include "cscfConfig.h"
#include "cscfHelper.h"
#include "icscfRegister.h"
#include "icscfCxLir.h"



osStatus_e icscf_performLir4Scscf(osPointerLen_t* pImpu, diaNotifyApp_h icscfLir4ScscfCb, void* scscfId)
{
	osStatus_e status = OS_STATUS_OK;

	diaCxLirAppInput_t lirInput = {};
	lirInput.pImpu = pImpu;
	//since this function is called by scscf, always set uat = DIA_3GPP_CX_USER_AUTH_TYPE_REGISTRATION
	lirInput.userAuthType = DIA_3GPP_CX_USER_AUTH_TYPE_REGISTRATION;
	lirInput.featureList = 1 << DIA_CX_FEATURE_LIST_ID_SIFC;

	status = diaCx_sendLIR(&lirInput, icscfLir4ScscfCb, scscfId);

EXIT:
	return status;
}


osStatus_e icscf_decodeLia(diaMsgDecoded_t* pDiaDecoded, osPointerLen_t* pServerName, scscfCapInfo_t* pCapList, diaResultCode_t* pResultCode)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pServerName)
    {
        logError("pServerName is null.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(pDiaDecoded->cmdCode != DIA_CMD_CODE_LIR)
	{
		logError("expect LIA, but the rsp cmdCode=%d.", pDiaDecoded->cmdCode);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(pDiaDecoded->cmdFlag & DIA_CMD_FLAG_REQUEST)
	{
		logError("received LIR request, ignore.");
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
	}

	pServerName->l = 0;

    //starts from avp after sessionId
    osListElement_t* pLE = pDiaDecoded->avpList.head->next;
    while(pLE)
    {
        diaAvp_t* pAvp = pLE->data;
        switch(pAvp->avpCode)
        {
            case DIA_AVP_CODE_RESULT_CODE:
                pResultCode->resultCode = pAvp->avpData.data32;
                break;
            case DIA_AVP_CODE_EXPERIMENTAL_RESULT:
            {
                osListElement_t* pAvpLE = pAvp->avpData.dataGrouped.dataList.head;
                while(pAvpLE)
                {
                    diaAvp_t* pGrpAvp = pAvpLE->data;
                    if(pGrpAvp->avpCode == DIA_AVP_CODE_EXPERIMENTAL_RESULT_CODE)
                    {
                        pResultCode->expCode = pGrpAvp->avpData.data32;
                        break;
                    }

                    pAvpLE = pAvpLE->next;
                }
                break;
            }
			case DIA_AVP_CODE_CX_SERVER_NAME:
				*pServerName = pAvp->avpData.dataStr.pl;
				break;
			case DIA_AVP_CODE_CX_SERVER_CAPABILITY:
			{
				if(!pCapList)
				{
					logInfo("received server capabilities AVP, but the caller does not expect scscf capabilities, ignore.");
					break;
				}

				pCapList->manCapNum = 0;
				pCapList->optCapNum = 0;
				osListElement_t* pAvpLE = pAvp->avpData.dataGrouped.dataList.head;
                while(pAvpLE)
                {
                    diaAvp_t* pGrpAvp = pAvpLE->data;
                    if(pGrpAvp->avpCode == DIA_AVP_CODE_CX_MANDATORY_CAPABILITY)
                    {
						if(pCapList->manCapNum >= ICSCF_MAX_SCSCF_CAP_NUM)
						{
							logInfo("received more mandatory capabilities than ICSCF supports(%d), ignore.", ICSCF_MAX_SCSCF_CAP_NUM);
						}
						else
						{
                        	pCapList->mandatoryCap[pCapList->manCapNum++] = pGrpAvp->avpData.data32;
						}
                    }
					else if(pGrpAvp->avpCode == DIA_AVP_CODE_CX_OPTIONAL_CAPABILITY)
					{
                        if(pCapList->optCapNum >= ICSCF_MAX_SCSCF_CAP_NUM)
                        {
                            logInfo("received more optional capabilities than ICSCF supports(%d), ignore.", ICSCF_MAX_SCSCF_CAP_NUM);
                        }
                        else
                        {
                        	pCapList->optionalCap[pCapList->optCapNum++] = pGrpAvp->avpData.data32;
                    	}
					}
					else
					{
						logInfo("received other avp(%d) inside DIA_AVP_CODE_CX_SERVER_CAPABILITY, ignore.", pGrpAvp->avpCode);
					}

                    pAvpLE = pAvpLE->next;
                }
                break;
            }				
            default:
                mlogInfo(LM_CSCF, "avpCode(%d) is not processed.", pAvp->avpCode);
                break;
        }

        pLE = pLE->next;
    }

	if(pResultCode->isResultCode && pResultCode->resultCode < 3000 && pResultCode->resultCode >= 2000 && pServerName->l == 0)
	{
		logError("LIA resultCode=%d, but does not contain server name.", pResultCode->resultCode);
		status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
	}		
EXIT:
    return status;
}

