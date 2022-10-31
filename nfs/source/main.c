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
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <prosperodev.h>
#include <debugnet.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <prosperoNfs.h>

dlsym_t *prosperoDlsym;


int initApp()
{
	libprospero_init();
	debugNetInit("192.168.1.12",18194,3);
	debugNetPrintf(DEBUGNET_INFO,"[NFSSAMPLE] Prospero sdk nfs sample\n");

	int ret;
	ret=prosperoNfsInit("nfs://192.168.1.12/usr/local/prosperodev/hostapp");

	return 0;
}

void finishApp()
{
	prosperoNfsFinish();
	debugNetFinish();
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
	int ret;
	
	debugNetPrintf(DEBUGNET_INFO,"[NFSSAMPLE] opening rom file from nfs....\n");
	int fd=prosperoNfsOpen("msxprospero/GAMES/MGEAR1.ROM",O_RDONLY,0777);
	int size=prosperoNfsLseek(fd,0,SEEK_END);
	prosperoNfsLseek(fd,0,SEEK_SET);
	char *buf=malloc(size);
	prosperoNfsRead(fd,buf,size);
	prosperoNfsClose(fd);
	

	finishApp();
	return 0;
}

