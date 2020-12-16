/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file scscfCx.c
 * implement 3GPP 28.229 SCSCF Cx functions, including MAR and SAR
 ********************************************************/


#include "osHash.h"
#include "osTimer.h"
#include "osSockAddr.h"

#include "diaMsg.h"
#include "diaIntf.h"
#include "diaCxSar.h"
#include "diaCxAvp.h"

#include "cscfConfig.h"
#include "cscfHelper.h"
#include "icscfRegister.h"




osStatus_e icscfCx_performUar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, DiaCxUarAuthType_e authType, icscfRegInfo_t* pRegInfo)
{
    osStatus_e status = OS_STATUS_OK;

    diaCxUarAppInput_t uarInput = {authType, pImpi, pImpu, 1 << DIA_CX_FEATURE_LIST_ID_SIFC, NULL};
    status = diaCx_sendUAR(&uarInput, icscfReg_onDiaMsg, pRegInfo);
    if(status != OS_STATUS_OK)
    {
        logError("fails to diaCx_sendUAR for impi(%r) for UAR.", pImpi);
		status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

EXIT:
    return status;
}


osStatus_e icscfReg_decodeUaa(diaMsgDecoded_t* pDiaDecoded, icscfUaaInfo_t* pUaaInfo)
{
    osStatus_e status = OS_STATUS_OK;

    //starts from avp after sessionId
    osListElement_t* pLE = pDiaDecoded->avpList.head->next;
    while(pLE)
    {
        diaAvp_t* pAvp = pLE->data;
        switch(pAvp->avpCode)
        {
            case DIA_AVP_CODE_RESULT_CODE:
				pUaaInfo->rspCode.isResultCode = true;
                pUaaInfo->rspCode.resultCode = pAvp->avpData.data32;
                goto EXIT;
                break;
            case DIA_AVP_CODE_EXPERIMENTAL_RESULT_CODE:
				pUaaInfo->rspCode.isResultCode = false;
				pUaaInfo->rspCode.expCode = pAvp->avpData.data32;
                goto EXIT;
                break;
            case DIA_AVP_CODE_CX_SERVER_NAME:
			{
				pUaaInfo->isCap = false;
                osVPointerLen_t* pServerNameData = &pAvp->avpData.dataStr;
				pUaaInfo->serverName = pServerNameData->pl;
                break;
			}
            case DIA_AVP_CODE_CX_SERVER_CAPABILITY:
            {
				pUaaInfo->isCap = true;
				pUaaInfo->isCapExist = true;
			    osListPlus_init(&pUaaInfo->serverCap.manCap, false);
    			osListPlus_init(&pUaaInfo->serverCap.optCap, false);
    			osListPlus_init(&pUaaInfo->serverCap.serverName, false);
				osListPlusElement_init(&pUaaInfo->curLE);

                osList_t* pCapList = &pAvp->avpData.dataGrouped.dataList;
                if(pCapList)
                {
                    osListElement_t* pGrpLE = pCapList->head;
                    while(pLE)
                    {
                        diaAvp_t* pGrpAvp = pGrpLE->data;
                        if(pAvp)
                        {
							switch(pGrpAvp->avpCode)
							{
								case DIA_AVP_CODE_CX_MANDATORY_CAPABILITY:
                    				osListPlus_append(&pUaaInfo->serverCap.manCap, &pGrpAvp->avpData.dataU32);
                    				break;
                				case DIA_AVP_CODE_CX_OPTIONAL_CAPABILITY:
                    				osListPlus_append(&pUaaInfo->serverCap.optCap, &pGrpAvp->avpData.dataU32);
                    				break;
                				case DIA_AVP_CODE_CX_SERVER_NAME:
                    				osListPlus_append(&pUaaInfo->serverCap.serverName, &pGrpAvp->avpData.dataStr.pl);
                    				break;
				                default:
                				    logError("UAR server capability contains avp(%d), ignore.", pGrpAvp->avpCode);
                    				break;
            				}
                        }
                        pGrpLE = pGrpLE->next;
                    }
                }
                break;
            }
            default:
                mlogInfo(LM_CSCF, "avpCode(%d) is not processed.", pAvp->avpCode);
                break;
        }

        pLE = pLE->next;
    }

EXIT:
    return status;
}


