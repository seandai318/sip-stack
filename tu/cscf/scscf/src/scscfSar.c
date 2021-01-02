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
#include "scscfRegistrar.h"
#include "scscfIntf.h"


static osStatus_e scscfReg_decodeSaa(diaMsgDecoded_t* pDiaDecoded, scscfUserProfile_t* pUsrProfile, diaResultCode_t* pResultCode, scscfChgInfo_t* pChgInfo);


static osStatus_e scscfReg_performMar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, scscfRegInfo_t* pRegInfo)
{
    osStatus_e status = OS_STATUS_OK;
    return status;
}


osStatus_e scscfReg_performSar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, scscfRegInfo_t* pRegInfo, dia3gppServerAssignmentType_e saType, int regExpire)
{
    osStatus_e status = OS_STATUS_OK;

    diaCxUserDataAvailable_e usrDataAvailable = DIA_3GPP_CX_USER_DATA_NOT_AVAILABLE;
    switch(pRegInfo->regState)
    {
        case SCSCF_REG_STATE_REGISTERED:
            break;
        case SCSCF_REG_STATE_NOT_REGISTERED:
        case SCSCF_REG_STATE_UN_REGISTERED:
            if(regExpire == 0)
            {
                //simple accept
                goto EXIT;
            }
            else
            {
                if(pRegInfo->regState == SCSCF_REG_STATE_UN_REGISTERED)
                {
                    usrDataAvailable = DIA_3GPP_CX_USER_DATA_ALREADY_AVAILABLE;
                }
            }
            break;
        default:
            logError("received sip REGISTER with expire=%d when pRegInfo->regState=%d, this shall never happen, reject.", regExpire, pRegInfo->regState);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
            break;
    }

    diaCxSarInfo_t sarInfo = {saType, usrDataAvailable, DIA_3GPP_CX_MULTI_REG_IND_NOT_MULTI_REG, NULL};
	osPointerLen_t serverName = {SCSCF_URI_WITH_PORT, strlen(SCSCF_URI_WITH_PORT)};
	//calculate HSSDest
    struct sockaddr_in* pDest = diaConnGetActiveDest(DIA_INTF_TYPE_CX);
    if(!pDest)
    {
        logInfo("the connection towards HSS is not available for impu(%r).", pImpu);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

	diaCxSarAppInput_t sarInput = {pImpi, pImpu, &serverName, &sarInfo, 1 << DIA_CX_FEATURE_LIST_ID_SIFC, NULL};
debug("to-remove, scscfReg_onDiaMsg=%p.", scscfReg_onDiaMsg);
    status = diaCx_sendSAR(&sarInput, scscfReg_onDiaMsg, pRegInfo);
    if(status != OS_STATUS_OK)
    {
        logError("fails to diaCx_initSAR for impu(%r) for DIA_3GPP_CX_UNREGISTERED_USER.", pImpu);
        goto EXIT;
    }

EXIT:
    return status;
}


osStatus_e scscfReg_onSaa(scscfRegInfo_t* pRegInfo, diaResultCode_t resultCode)
{
	osStatus_e status = OS_STATUS_OK;

    switch(pRegInfo->tempWorkInfo.sarRegType)
    {
        case SCSCF_REG_SAR_UN_REGISTER:
        {
#if 0 	//to-do when integrating session part
            osListElement_t* pLE = pRegInfo->sessDatalist.head;
            if(!pLE)
            {
                logError("no session waits on HSS response, it shall not happen, pHashData=%p.", pRegInfo);
            }
            else
            {
                while(pLE)
                {
                    ScscfSessInfo_t* pSessInfo = pLE->data;
                    pSessInfo->scscfHssNotify(pSessInfo->sessData, DIA_CMD_CODE_SAR, resultCode, &pRegInfo->regInfo.userProfile);
                    pLE = pLE->next;
                }

                osList_clear(&pHashData->sessDatalist);
            }

            if(!dia_is2xxxResultCode(resultCode))
            {
                osHash_deleteNode(pUserHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
            }
            else
            {
                pRegInfo->regState = SCSCF_REG_STATE_UN_REGISTERED;
                pRegInfo->purgeTimerId = osStartTimer(SCSCFREG_USER_PURGE_TIME, scscfReg_onTimeout, pRegInfo);
            }
#endif
            break;
        }
        case SCSCF_REG_SAR_REGISTER:
        {
            sipResponse_e rspCode = cscf_cx2SipRspCodeMap(resultCode);
            cscf_sendRegResponse(pRegInfo->tempWorkInfo.pTUMsg, pRegInfo->tempWorkInfo.pReqDecodedRaw, pRegInfo, 0, pRegInfo->tempWorkInfo.pTUMsg->pPeer, pRegInfo->tempWorkInfo.pTUMsg->pLocal, rspCode);

            if(rspCode < 200 || rspCode >= 300)
            {
                osHash_deleteNode(pRegInfo->tempWorkInfo.pRegHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
            }
            else
            {
                //create hash for all identities belong to the UE's subscription
                scscfReg_createSubHash(pRegInfo);

                pRegInfo->regState = SCSCF_REG_STATE_REGISTERED;
                pRegInfo->expiryTimerId = osStartTimer(pRegInfo->ueContactInfo.regExpire, scscfReg_onTimeout, pRegInfo);

				//update the tempWorkInfo.impu with the nobarring impu
				osPointerLen_t* pImpu = scscfReg_getNoBarImpu(&pRegInfo->ueList, true); //true=tel uri is preferred
				if(!pImpu)
				{
					logError("no no-barring impu is available.");
					status = OS_ERROR_INVALID_VALUE;
					goto EXIT;
				}

				//replace the originals tored impu with the new one
				pRegInfo->tempWorkInfo.impu = *pImpu;

                //continue 3rd party registration
                pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE;
                scscfIfcEvent_t ifcEvent = {true, SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(SCSCF_REG_SAR_REGISTER)};
                bool is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent);
				if(is3rdPartyRegDone)
                {
                    pRegInfo->tempWorkInfo.regWorkState =  SCSCF_REG_WORK_STATE_NONE;
                    scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);
                }
            }
            break;
        }
        case SCSCF_REG_SAR_RE_REGISTER:
        {
            sipResponse_e rspCode = cscf_cx2SipRspCodeMap(resultCode);
            cscf_sendRegResponse(pRegInfo->tempWorkInfo.pTUMsg, pRegInfo->tempWorkInfo.pReqDecodedRaw, pRegInfo, 0, pRegInfo->tempWorkInfo.pTUMsg->pPeer, &pRegInfo->tempWorkInfo.sipLocalHost, rspCode);

            if(rspCode > 199 && rspCode < 299)
            {
                pRegInfo->expiryTimerId = osRestartTimer(pRegInfo->expiryTimerId);

                //continue 3rd party registration
                pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE;
                scscfIfcEvent_t ifcEvent = {true, SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(SCSCF_REG_SAR_RE_REGISTER)};
                if(scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent))
                {
                    pRegInfo->tempWorkInfo.regWorkState =  SCSCF_REG_WORK_STATE_NONE;
                    scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);
                }
            }
            break;
        }
        case SCSCF_REG_SAR_DE_REGISTER:
        {
            sipResponse_e rspCode = cscf_cx2SipRspCodeMap(resultCode);
            cscf_sendRegResponse(pRegInfo->tempWorkInfo.pTUMsg, pRegInfo->tempWorkInfo.pReqDecodedRaw, pRegInfo, 0, pRegInfo->tempWorkInfo.pTUMsg->pPeer, &pRegInfo->tempWorkInfo.sipLocalHost, rspCode);

            //continue 3rd party registration
            pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE;
            scscfIfcEvent_t ifcEvent = {true, SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(SCSCF_REG_SAR_DE_REGISTER)};
            if(scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent))
            {
                pRegInfo->tempWorkInfo.regWorkState =  SCSCF_REG_WORK_STATE_NONE;
                scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);

                //if scscfReg_perform3rdPartyReg == isDone, free the subscription, otherwise, wait for the response from the last AS to free the subscription.
                scscfReg_deleteSubHash(pRegInfo);
            }
            break;
        }
        default:
            logError("a UE receives a SAA while in sarRegType=%d, this shall never happen.", pRegInfo->tempWorkInfo.sarRegType);
			status = OS_ERROR_INVALID_VALUE;
            break;
    }

EXIT:
    return status;
}


osStatus_e scscfReg_decodeHssMsg(diaMsgDecoded_t* pDiaDecoded, scscfRegInfo_t* pRegInfo, diaResultCode_t* pResultCode)
{
    osStatus_e status = OS_STATUS_OK;

    switch(pDiaDecoded->cmdCode)
    {
        case DIA_CMD_CODE_SAR:
            if(pDiaDecoded->cmdFlag & DIA_CMD_FLAG_REQUEST)
            {
                logError("received SAR request, ignore.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            status = scscfReg_decodeSaa(pDiaDecoded, &pRegInfo->userProfile, pResultCode, &pRegInfo->hssChgInfo);
            break;
        default:
            logError("unexpected dia command(%d) received, ignore.", pDiaDecoded->cmdCode);
            break;
    }

EXIT:
    return status;
}


static osStatus_e scscfReg_decodeSaa(diaMsgDecoded_t* pDiaDecoded, scscfUserProfile_t* pUsrProfile, diaResultCode_t* pResultCode, scscfChgInfo_t* pChgInfo)
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
                pResultCode->resultCode = pAvp->avpData.data32;
                break;
            case DIA_AVP_CODE_EXPERIMENTAL_RESULT:
			{
				osListElement_t* pAvpLE = pAvp->avpData.dataGrouped.dataList.head;
				while(pAvpLE)
				{
					diaAvp_t* pAvp = pAvpLE->data;
					if(pAvp->avpCode == DIA_AVP_CODE_EXPERIMENTAL_RESULT_CODE)
                	{
						pResultCode->expCode = pAvp->avpData.data32;
						break;
					}

					pAvpLE = pAvpLE->next;
				}
                break;
			}
            case DIA_AVP_CODE_CX_USER_DATA_CX:
			{
                osVPointerLen_t* pXmlUserData = &pAvp->avpData.dataStr;
                status = scscfConfig_parseUserProfile(&pXmlUserData->pl, pUsrProfile);
                break;
			}
            case DIA_AVP_CODE_CX_CHARGING_INFO:
            {
                osList_t* pChgInfoList = &pAvp->avpData.dataGrouped.dataList;
                if(pChgInfoList)
                {
                    osListElement_t* pLE = pChgInfoList->head;
                    while(pLE)
                    {
                        diaAvp_t* pAvp = pLE->data;
                        if(pAvp)
                        {
                            if(pAvp->avpCode == DIA_AVP_CODE_CX_PRI_CHG_COLLECTION_FUNC_NAME)
                            {
                                pChgInfo->chgAddrType = CHG_ADDR_TYPE_CCF;
                                osVPL_copyVPL(&pChgInfo->chgAddr, &pAvp->avpData.dataStr);
                            }
                            else if(pAvp->avpCode == DIA_AVP_CODE_CX_PRI_EVENT_CHG_FUNC_NAME)
                            {
                                pChgInfo->chgAddrType = CHG_ADDR_TYPE_ECF;
                                osVPL_copyVPL(&pChgInfo->chgAddr, &pAvp->avpData.dataStr);
                            }
                            else
                            {
                                mlogInfo(LM_CSCF, "pAvp->avpCode(%d) is ignored.", pAvp->avpCode);
                            }
                        }
                        pLE = pLE->next;
                    }
                }
                break;
            }
            case DIA_AVP_CODE_USER_NAME:
                //*pUserName = pAvp->avpData.dataStr;
                break;
            default:
                mlogInfo(LM_CSCF, "avpCode(%d) is not processed.", pAvp->avpCode);
                break;
        }

        pLE = pLE->next;
    }

EXIT:
    return status;
}


