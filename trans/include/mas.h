/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file mas.h
 ********************************************************/

#ifndef _MAS_H
#define _MAS_H

typedef enum {
	MAS_MODULE_SIP,
	MAS_MODULE_OTHER,
} masModuleType_e;

typedef struct {
	masModuleType_e moduleType;
	void* pData;
} masTimerData_t;


void masStartTimer(timer_t time, void* pData);

#endif
