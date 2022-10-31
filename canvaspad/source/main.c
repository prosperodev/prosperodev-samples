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
#include <prosperoPad.h>

dlsym_t *prosperoDlsym;

void updateController();

int initApp()
{
	libprospero_init();
	debugNetInit("192.168.1.12",18194,3);
	
	return 0;
}

void finishApp()
{
	prosperoPadFinish();
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

unsigned int *framebuffer = NULL;
int GetVirtualMemInfo()
{
	SceKernelVirtualQueryInfo info;
	void *addr=NULL;
	//debugNetUDPPrintf("%-32s %s\t%s\t\t%s\n", "NAME", "START", "END", "PROT");
	//sceKernelVirtualQuery(addr, SCE_KERNEL_VQ_FIND_NEXT, &info, sizeof(info))==SCE_OK)
	for(int i=0;i<25;i++)
	{
		if(sceKernelVirtualQuery(addr, 1, &info, sizeof(info))==0)
		{

			if(info.protection&PROT_READ && strcmp(info.name,"payload")!=0 && strcmp(info.name,"SceNKBmalloc")==0)
			{
				debugNetUDPPrintf("index:%d %-32s 0x%08x\t0x%08x\t0x%02x\n",i, info.name, info.start, info.end, info.protection);

				framebuffer = info.start;
				while(framebuffer < (unsigned int *)info.end)
				{
					if(framebuffer[0] == 0xffc2c2c2 && framebuffer[1] == 0xffc2c2c2)
					{
						debugNetPrintf(DEBUGNET_INFO,"framebuffer found at 0x%08X\n",framebuffer);
						break;
					}
					framebuffer++;
				}
			}
			addr=info.end;
		}
	}
	return 0;
}

void *canvasRenderer(void *)
{
	int i=0;
	while(i<3600)//216000)
	{
		updateController();
		for(int x=0;x<320;x++)
		{
			for(int y=0;y<288;y++)
			{
				framebuffer[x+y*320]=0xffff0000;
			}
		}
		
		sceKernelUsleep(100);

		i++;
	}
	

	return NULL;
}
void updateController()
{
	int ret;
	unsigned int buttons=0;
	ret=prosperoPadUpdate();
	debugNetPrintf(DEBUGNET_DEBUG,"prosperoPadUpdate %d\n",ret);

	if(ret==0)
	{
		if(prosperoPadGetButtonPressed(PROSPEROPAD_L2|PROSPEROPAD_R2) || prosperoPadGetButtonHold(PROSPEROPAD_L2|PROSPEROPAD_R2))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Combo L2R2 pressed\n");
			buttons=prosperoPadGetCurrentButtonsPressed();
			buttons&= ~(PROSPEROPAD_L2|PROSPEROPAD_R2);
			prosperoPadSetCurrentButtonsPressed(buttons);
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_L1|PROSPEROPAD_R1) )
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Combo L1R1 pressed\n");
			buttons=prosperoPadGetCurrentButtonsPressed();
			buttons&= ~(PROSPEROPAD_L1|PROSPEROPAD_R1);
			prosperoPadSetCurrentButtonsPressed(buttons);
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_L1|PROSPEROPAD_R2) || prosperoPadGetButtonHold(PROSPEROPAD_L1|PROSPEROPAD_R2))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Combo L1R2 pressed\n");
			buttons=prosperoPadGetCurrentButtonsPressed();
			buttons&= ~(PROSPEROPAD_L1|PROSPEROPAD_R2);
			prosperoPadSetCurrentButtonsPressed(buttons);
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_L2|PROSPEROPAD_R1) || prosperoPadGetButtonHold(PROSPEROPAD_L2|PROSPEROPAD_R1) )
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Combo L2R1 pressed\n");
			buttons=prosperoPadGetCurrentButtonsPressed();
			buttons&= ~(PROSPEROPAD_L2|PROSPEROPAD_R1);
			prosperoPadSetCurrentButtonsPressed(buttons);
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_UP))// || prosperoPadGetButtonHold(PROSPEROPAD_UP))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Up pressed\n");
			
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_DOWN))// || prosperoPadGetButtonHold(PROSPEROPAD_DOWN))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Down pressed\n");
			
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_RIGHT))// || prosperoPadGetButtonHold(PROSPEROPAD_RIGHT))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Right pressed\n");
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_LEFT))// || prosperoPadGetButtonHold(PROSPEROPAD_LEFT))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Left pressed\n");
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_TRIANGLE))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Triangle pressed exit\n");
			//flag=0;
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_CIRCLE))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Circle pressed\n");  
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_CROSS))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Cross pressed\n");
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_SQUARE))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"Square pressed\n");
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_L1))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"L1 pressed %d\n");
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_L2))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"L2 pressed %d\n");
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_R1))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"R1 pressed\n");
		}
		if(prosperoPadGetButtonPressed(PROSPEROPAD_R2))
		{
			debugNetPrintf(DEBUGNET_DEBUG,"R2 pressed\n");
		}
	}
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
	debugNetPrintf(DEBUGNET_INFO,"[CanvasPad] Prospero sdk canvas pad sample\n");
	GetVirtualMemInfo();
	int ret;
	for(int i=0;i<500;i++)
	{
		SceKernelModuleInfo tmpInfo;
		tmpInfo.size=352;
		ret=sceKernelGetModuleInfo(i,&tmpInfo);
		if(ret)
		{
			//debugNetPrintf(DEBUGNET_ERROR,"[%s][%d] error 0x%08X\n",__FUNCTION__,__LINE__,ret);
			//goto finish;
		}
		else
		{
			debugNetPrintf(DEBUGNET_INFO,"Module %s is loaded with handle 0x%08X\n",tmpInfo.name,i);
		}
		
	}

/*
	int param=700;
	ret=sceUserServiceInitialize(&param);
	debugNetPrintf(DEBUGNET_INFO,"sceUserServiceInitialize return 0x%8x\n",ret);
	SceUserServiceUserId userId;
	ret=sceUserServiceGetInitialUser(&userId);
	if(ret<0)
	{
		debugNetPrintf(DEBUGNET_ERROR,"sceUserServiceGetInitialUser return 0x%8x\n",ret);
		userId=0x10000000;
	}
	debugNetPrintf(DEBUGNET_INFO,"sceUserServiceGetInitialUser return 0x%8x\n",ret);
	debugNetPrintf(DEBUGNET_INFO,"userId  0x%8x\n",userId);
	userId=0x10000000;
	         


 	int handlevideo=sceVideoOutOpen(0xff, 0, 0, NULL);
	debugNetPrintf(DEBUGNET_INFO,"sceVideoOutOpen return  0x%8x\n",handlevideo);

	ret=scePadInit();
	if(ret<0)
	{
		debugNetPrintf(DEBUGNET_ERROR,"scePadInit return 0x%8x\n",ret);
		return 0;
	}
	debugNetPrintf(DEBUGNET_INFO,"scePadInit return 0x%8x\n",ret);
	//int handle=scePadOpen(0x1b538397, 0, 0, NULL);

	//debugNetPrintf(DEBUGNET_INFO,"scePadOpen return handle 0x%8x\n",handle);
	for(int i=0;i<0x10000000;i++)
	{
		int handle=scePadGetHandle(0x1b538397,0,0);
		//int handle=scePadOpen(0x1b538397, 0, 0, NULL);
		if(handle<0)
		{
			debugNetPrintf(DEBUGNET_ERROR,"userid: 0x%x scePadGetHandle return 0x%8x\n",0x1b538397,handle);
			//return 0;
		}
		else
		{
			debugNetPrintf(DEBUGNET_INFO,"userid: 0x%x scePadGetHandle return handle 0x%8x\n",0x1b538397,handle);
		}
	//}*/
	if(framebuffer!=NULL)
	{
		/*ret=fork();
		if(ret==0)
		{
			debugNetPrintf(DEBUGNET_INFO,"forking child done\n");
			for(int i=0;i<500;i++)
			{
				SceKernelModuleInfo tmpInfo;
				tmpInfo.size=352;
				ret=sceKernelGetModuleInfo(i,&tmpInfo);
				if(ret)
				{
					//debugNetPrintf(DEBUGNET_ERROR,"[%s][%d] error 0x%08X\n",__FUNCTION__,__LINE__,ret);
					//goto finish;
				}
				else
				{
					debugNetPrintf(DEBUGNET_INFO,"Module %s is loaded with handle 0x%08X\n",tmpInfo.name,i);
				}
		
			}
			
			canvasRenderer();
			//exit(0);
		}
		else
		{
			debugNetPrintf(DEBUGNET_INFO,"forking main return 0x%08X\n",ret);

		}*/
		prosperoPadInit();
		ScePthread thread;
		if(framebuffer!=NULL)
		{
			//canvasRenderer();
			scePthreadCreate(&thread, NULL, canvasRenderer, NULL, "canvasRenderer");
		}
	}
	return 0;
}






