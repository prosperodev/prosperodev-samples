/*
#  ____   ____   ____      ___  ____   ____  ____   ____  ____   ____ _     _
#  ____|  ____> |    |    |     ____| <____  ____> |    |     | <____  \   /
# |      |    \ |____| ___|    |      <____ |    \ |____| ____| <____   \_/  
# 
# PROSPERODEV Open Source Project.
#------------------------------------------------------------------------------------
# Copyright 2010-2022, prosperodev - http://github.com/prosperodev
# Licenced under MIT License
# Review README & LICENSE files for further details.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <prosperodev.h>
#include <debugnet.h>

dlsym_t *prosperoDlsym;


int initApp()
{
	libprospero_init();
	debugNetInit("192.168.1.12",18194,3);
	
	return 0;
}

void finishApp()
{
	debugNetFinish();
}

typedef struct SceKernelVirtualQueryInfo {
    void* start;//0
    void* end;//8
    off_t offset;//16
    int protection;//24
    int unk0;//28
    unsigned unk1:1;//32
    unsigned isDirectMemory:1;
    unsigned unk3:1;
    char name[32];//33
    unsigned unk4;
    unsigned unk5;
} SceKernelVirtualQueryInfo;


int payload_main(struct payload_args *args)
{
	if(args==NULL)
	{
		return -1;
	}
	prosperoDlsym=args->dlsym;
	initApp();
	SceKernelModuleInfo info;
	info.size=0x160;
	int handle=sceKernelLoadStartModule("libSceSysmodule.sprx",0,0,0,0,0);
	int ret=sceKernelGetModuleInfo(handle,&info);
	debugNetPrintf(DEBUGNET_INFO,"[SYSMODULEID] %s  module name %s\n",__FUNCTION__,info.name);
	debugNetPrintf(DEBUGNET_INFO,"[SYSMODULEID] %s  number of segments %d\n",__FUNCTION__,info.segmentCount);
	for(int i=0;i<info.segmentCount;i++)
	{
		debugNetPrintf(DEBUGNET_INFO,"[SYSMODULEID] %s segment %d start 0x%08X size 0x%08X protection 0x%02X\n",__FUNCTION__,i,info.segmentInfo[i].address,info.segmentInfo[i].size,info.segmentInfo[i].prot);
	}
	typedef struct SysModules
	{
		int block0[10];//0
		int internalModuleId;//40
		int block1[3];//44
		char* moduleName;//56
	}SysModules;
	SysModules *mods=info.segmentInfo[2].address+0x4678;//for 4.03

	for(int i=0;i<312;i++)
	{
		int id=mods[i].internalModuleId;
		char *moduleName=mods[i].moduleName;
		if(id==0)
		{
			debugNetPrintf(DEBUGNET_INFO,"[SYSMODULEID] %s %d %s id %s\n",__FUNCTION__,i,(moduleName)?moduleName:"not available","not available");
		}
		else
		{
			debugNetPrintf(DEBUGNET_INFO,"[SYSMODULEID] %s %d %s id 0x%08X\n",__FUNCTION__,i,(moduleName)?moduleName:"not available",id);
		}
	}

	
	
	return 0;
}






