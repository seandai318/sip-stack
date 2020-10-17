/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportUdpMgmt.h
 ********************************************************/


#ifndef _TRANSPORT_UDP_MGMT_H
#define _TRANSPORT_UDP_MGMT_H

#include <netinet/in.h>

#include "transportIntf.h"

int tpUdpMgmtGetFd(transportAppType_e appType, struct sockaddr_in localAddr);
tpLocalSendCallback_h tpUdpMgmtGetUdpCallback(int fd);
osStatus_e tpUdpMgmtSetFd(transportAppType_e appType, struct sockaddr_in localAddr, tpLocalSendCallback_h callback, int fd);

#endif
