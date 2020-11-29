/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file scscfCx.c
 * implement 3GPP 28.229 SCSCF Cx functions, including MAR and SAR
 ********************************************************/


#include "diaMsg.h"
#include "diaIntf.h"
#include "diaCxSar.h"


static osStatus_e scscfReg_decodeSaa(osPointerLen_t* pImpu, diaMsgDecoded_t* pDiaDecoded, scscfUserProfile_t* pUsrProfile, diaResultCode_t* pResultCode, scscfChgInfo_t* pChgInfo);


static osStatus_e scscfReg_performMar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, pRegData)
{
    osStatus_e status = OS_STATUS_OK;
    return status;
}


static osStatus_e scscfReg_performSar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, pRegData, int regExpire)
{
    osStatus_e status = OS_STATUS_OK;

    dia3gppServerAssignmentType_e saType;
    diaCxUserDataAvailable_e usrDataAvailable = DIA_3GPP_CX_USER_DATA_NOT_AVAILABLE;
    switch(pRegData->regState)
    {
        case MAS_REGSTATE_REGISTERED:
            if(regExpire == 0)
            {
                saType = DIA_3GPP_CX_USER_DEREGISTRATION;
            }
            else
            {
                saType = DIA_3GPP_CX_RE_REGISTRATION;
            }
            break;
        case MAS_REGSTATE_NOT_REGISTERED:
        case MAS_REGSTATE_UN_REGISTERED:
            if(regExpire == 0)
            {
                //simple accept
                rspCode = SIP_RESPONSE_200;
                goto EXIT;
            }
            else
            {
                if(pRegData->regState == MAS_REGSTATE_UN_REGISTERED)
                {
                    usrDataAvailable = DIA_3GPP_CX_USER_DATA_ALREADY_AVAILABLE;
                }
                saType = DIA_3GPP_CX_REGISTRATION;
            }
            break;
        default:
            logError("received sip REGISTER with expire=%d when pRegData->regState=%d, this shall never happen, reject.", regExpire, pRegData->regState);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
            break;
    }

    diaCxSarInfo_t sarInfo = {saType, usrDataAvailable, DIA_3GPP_CX_MULTI_REG_IND_NOT_MULTI_REG, NULL};
    diaCxSarAppInput_t sarInput = {pImpi, pImpu, SCSCF_URI_WITH_PORT, CSCF_CONFIG_HSS_URI, &sarInfo, 1 << DIA_CX_FEATURE_LIST_ID_SIFC, NULL};
    status = diaCx_sendSAR(pImpi, pImpu, saType, scscfReg_onDiaMsg, pRegData);
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
    switch(pRegInfo->tempWorkInfo.sarRegType)
    {
        case if(SCSCF_REG_SAR_UN_REGISTER)
        {
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
            break;
        }
        case SCSCF_REG_SAR_REGISTER:
        {
            sipResponse_e rspCode = scscf_hss2SipRspCodeMap(resultCode);
            scscReg_sendResponse(pRegInfo->regMsgInfo.pReqDecodedRaw, 0, pRegInfo->regMsgInfo.pSipTUMsg->pPeer, osSA_isInvalid(pRegInfo->regMsgInfo.sipLocalHost)? NULL : &pRegInfo->regMsgInfo.sipLocalHost, rspCode);

            if(rspCode < 200 || rspCode >= 300)
            {
                osHash_deleteNode(pUserHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
            }
            else
            {
                //create hash for all identities belong to the UE's subscription
                scscfReg_createSubHash(pRegInfo);

                pRegInfo->regState = SCSCF_REG_STATE_REGISTERED;
                pRegInfo->expiryTimerId = osStartTimer(pRegInfo->ueContactInfo.regExpire, scscfReg_onTimeout, pRegInfo);

                //continue 3rd party registration
                pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE;
                scscfIfcEvent_t ifcEvent = {true, SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(SCSCF_REG_SAR_REGISTER)};
                if(scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent))
                {
                    pRegInfo->tempWorkInfo.regWorkState =  SCSCF_REG_WORK_STATE_NONE;
                    scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);
                }
            }
            break;
        }
        case SCSCF_REG_SAR_RE_REGISTER:
        {
            sipResponse_e rspCode = scscf_hss2SipRspCodeMap(resultCode);
            scscReg_sendResponse(pRegInfo->regMsgInfo.pReqDecodedRaw, 0, pRegInfo->regMsgInfo.pSipTUMsg->pPeer, osSA_isInvalid(pRegInfo->regMsgInfo.sipLocalHost)? NULL : &pRegInfo->regMsgInfo.sipLocalHost, rspCode);

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
            sipResponse_e rspCode = scscf_hss2SipRspCodeMap(resultCode);
            scscReg_sendResponse(pRegInfo->regMsgInfo.pReqDecodedRaw, 0, pRegInfo->regMsgInfo.pSipTUMsg->pPeer, osSA_isInvalid(pRegInfo->regMsgInfo.sipLocalHost)? NULL : &pRegInfo->regMsgInfo.sipLocalHost, rspCode);

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
#endif
            break;
        }
        default:
            logError("a UE receives a SAA while in sarRegType=%d, this shall never happen.", pRegInfo->sarRegType);
            break;
    }

EXIT:
    return;
}


static osStatus_e scscfReg_decodeHssMsg(diaMsgDecoded_t* pDiaDecoded, scscfRegInfo_t* pRegInfo, diaResultCode_t* pResultCode)
{
    osStatus_e status = OS_STATUS_OK;

    switch(pDiaDecoded->cmdCode)
    {
        case DIA_CMD_CODE_SAR:
            if(!(pDiaDecoded->cmdFlag & DIA_CMD_FLAG_REQUEST))
            {
                logError("received SAR request, ignore.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            status = scscfReg_decodeSaa(pImpu, pDiaDecoded, &pRegInfo->usrProfile, pResultCode, &pHashData->chgInfo);
            break;
        default:
            logError("unexpected dia command(%d) received, ignore.", pDiaDecoded->cmdCode);
            break;
    }

EXIT:
    return status;
}


static osStatus_e scscfReg_decodeSaa(osPointerLen_t* pImpu, diaMsgDecoded_t* pDiaDecoded, scscfUserProfile_t* pUsrProfile, diaResultCode_t* pResultCode, scscfChgInfo_t* pChgInfo)
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
                isResultCode = true;
                pResultCode->resultCode = pAvp->avpData.data32;
                goto EXIT;
                break;
            case DIA_AVP_CODE_EXPERIMENTAL_RESULT_CODE:
                isResultCode = false;
                pResultCode->expCode = pAvp->avpData.data32;
                goto EXIT;
                break;
            case DIA_AVP_CODE_CX_USER_DATA_CX:
                osVPointerLen_t* pXmlUserData = &pAvp->avpData.dataStr;
                status = scfConfig_parseUserProfile(pXmlUserData, pUsrProfile);
                break;
            case DIA_AVP_CODE_CX_CHARGING_INFO:
            {
                osList_t* pChgInfoList = pAvp->avpData.dataGrouped.dataList;
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
                                pChgInfo.chgAddrType = CHG_ADDR_TYPE_CCF;
                                osVPL_copyVPL(&pChgInfo.chgAddr, &pAvp->avpData.dataStr);
                            }
                            else if(pAvp->avpCode == DIA_AVP_CODE_CX_PRI_EVENT_CHG_FUNC_NAME)
                            {
                                pChgInfo.chgAddrType = CHG_ADDR_TYPE_ECF;
                                osVPL_copyVPL(&pChgInfo.chgAddr, &pAvp->avpData.dataStr);
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
                *pUserName = pAvp->avpData.dataStr;
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


