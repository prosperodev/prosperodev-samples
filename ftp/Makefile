ifndef PROSPERODEV
$(error PROSPERODEV, is not set)
endif

target ?= prospero_elf
TargetFile=homebrew.elf

include $(PROSPERODEV)/make/prosperosdk.mk
#CompilerFlagsCpp +=-D__DEBUGNET_PC_IP__=\"$(DEBUGNET_PC_IP)\" 
LinkerFlags+=  -lprospero -lkernel_sys_stub -lSceLibcInternal_stub -lSceNet_stub -lSceSysmodule_stub -lSceSystemService_stub -lSceUserService_stub -lScePad_stub -lSceVideoOut_stub -lSceAudioOut_stub  -ldebugnet
#IncludePath += -I$(PROSPERODEV)/usr/include/prospero

