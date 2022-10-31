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

int GetVirtualMemInfo()
{
	int ptrwidth;         
	ptrwidth = 2*sizeof(void *) + 2;
	SceKernelVirtualQueryInfo info;
	void *addr=NULL;
	size_t size=0;
	int i=0;
	debugNetUDPPrintf("%-32s %s\t%s\t\t%s\n", "NAME", "START", "END", "PROT");
	//getMemoryInfo//sys_query_memory_protection
	//getOtherMemoryInfo//sys_virtual_query
	//sceKernelVirtualQuery(addr, SCE_KERNEL_VQ_FIND_NEXT, &info, sizeof(info))==SCE_OK)
	while (sceKernelVirtualQuery(addr, 1, &info, sizeof(info))==0)
	{
		if (!info.isDirectMemory) {
			size+=(int*)info.end-(int*)info.start;
		}

		addr=info.end;

		debugNetUDPPrintf("index:%d %-32s 0x%08x\t0x%08x\t0x%02x\n", i,info.name, info.start, info.end, info.protection);
		i++;
	}
	return size;
}

int payload_main(struct payload_args *args)
{
	if(args==NULL)
	{
		return -1;
	}
	prosperoDlsym=args->dlsym;
	initApp();

	/*int ret=getpid();
	debugNetPrintf(DEBUGNET_INFO,"[%s] getpid return %d\n",__FUNCTION__,ret);
	
	*(args->payloadout)=ret;*/
	debugNetPrintf(DEBUGNET_INFO,"[VirtualMemQuery] Prospero sdk listproc sample\n");
	int ret=GetVirtualMemInfo();
	debugNetPrintf(DEBUGNET_INFO,"[VirtualMemQuery] Current Executable Size: %d\n",ret);
	

	finishApp();
	return 0;
}






