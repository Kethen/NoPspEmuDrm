#include <vitasdk.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <taihen.h>

#include "PspEmu.h"
#include "SceShell.h"
#include "Log.h"

static int patched_pspemu = 0;
static int patched_sceshell = 0;

void _start() __attribute__ ((weak, alias ("module_start"))); 
int module_start(SceSize args, void *argp) {
	
	tai_module_info_t tai_info;
	tai_info.size = sizeof(tai_module_info_t);
	
	SceUID ret = taiGetModuleInfo("ScePspemu", &tai_info);
	if (ret >= 0){
		patched_pspemu = 1;
		int ret = pspemu_module_start(tai_info);
		if (ret != SCE_KERNEL_START_SUCCESS){
			LOG("%s: failed patching pspemu, 0x%x", __func__, ret);
			return ret;
		}
		ret = ps1cfw_enabler_start(tai_info);
		if (ret != SCE_KERNEL_START_SUCCESS){
			LOG("%s: failed starting ps1cfw_enabler, 0x%x\n", __func__, ret);
		}
		return ret;
	}
	
	ret = taiGetModuleInfo("SceShell", &tai_info);
	if (ret >= 0){
		patched_sceshell = 1;
		return sceshell_module_start(tai_info);
	}

	return SCE_KERNEL_START_NO_RESIDENT;
}

int module_stop(SceSize args, void *argp) {
	if(patched_pspemu) {
		int ret = ps1cfw_enabler_stop();
		if (ret != SCE_KERNEL_STOP_SUCCESS){
			LOG("%s: failed stopping ps1cfw_enabler, 0x%x\n", __func__, ret);
			return ret;
		}
		ret = pspemu_module_stop();
		if (ret != SCE_KERNEL_STOP_SUCCESS){
			LOG("%s: failed stopping pspemu patches, 0x%x\n", __func__, ret);
		}
		return ret;
	}
	if(patched_sceshell) return sceshell_module_stop();
	
	return SCE_KERNEL_STOP_SUCCESS;
}
