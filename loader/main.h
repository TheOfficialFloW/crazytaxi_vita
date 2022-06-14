#ifndef __MAIN_H__
#define __MAIN_H__

#include "so_util.h"

extern so_module crazytaxi_mod;

int debugPrintf(char *text, ...);

int ret0(void);

SceUID _vshKernelSearchModuleByName(const char *, const void *);

#endif
