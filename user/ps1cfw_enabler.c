// adapted from https://github.com/PSP-Archive/ARK-4/blob/main/loader/live/kernel/psxloader/ps1cfw_enabler/ps1cfw_enabler.c

#include <taihen.h>
#include <vitasdk.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Log.h"

#define SCE_PSPEMU_CACHE_NONE 0x1

static SceUID io_patch_path = -1;
static SceUID io_patch_size = -1;
static SceUID ctrl_patch = -1;

static const uint32_t movs_a1_0_nop_opcode = 0xBF002000;
static const uint32_t nop_nop_opcode = 0xBF00BF00;
static const uint32_t mov_r2_r4_mov_r4_r2 = 0x46224614;
static const uint32_t mips_move_a2_0 = 0x00003021;
static const uint32_t mips_nop = 0;

static int enabled = 0;
tai_module_info_t tai_info_saved = {0};

typedef struct PopsConfig{
    uint32_t magic;
    char title_id[20];
    char path[256];
}PopsConfig;

#define ARK_MAGIC 0xB00B1E55

static PopsConfig popsconfig;

static int (* ScePspemuErrorExit)(int error);
static int (* ScePspemuConvertAddress)(uint32_t addr, int mode, uint32_t cache_size);
static int (* ScePspemuWritebackCache)(void *addr, int size);
static int (* ScePspemuPausePops)(int pause);


static void get_functions(uint32_t text_addr) {
    ScePspemuErrorExit                  = (void *)(text_addr + 0x4104 + 0x1);
    ScePspemuConvertAddress             = (void *)(text_addr + 0x6364 + 0x1);
    ScePspemuWritebackCache             = (void *)(text_addr + 0x6490 + 0x1);
  
    if (tai_info_saved.module_nid == 0x2714F07D) {
        ScePspemuPausePops                  = (void *)(text_addr + 0x300C0 + 0x1);
    }
    else {
        ScePspemuPausePops                  = (void *)(text_addr + 0x300D4 + 0x1);
    }
}

static void patch_and_enable(){
	SceKernelModuleInfo mod_info;
	mod_info.size = sizeof(SceKernelModuleInfo);
	int ret = sceKernelGetModuleInfo(tai_info_saved.modid, &mod_info);

    // Get PspEmu functions
    get_functions((uint32_t)mod_info.segments[0].vaddr);

    // allow opening any path
    io_patch_path = taiInjectData(tai_info_saved.modid, 0x00, 0x839C, &nop_nop_opcode, 0x4);

    // allow opening files of any size
    io_patch_size = taiInjectData(tai_info_saved.modid, 0x00, 0xA13C, &mov_r2_r4_mov_r4_r2, 0x4);

    // fix controller on Vita TV
    ctrl_patch = taiInjectData(tai_info_saved.modid, 0, (tai_info_saved.module_nid == 0x2714F07D)?0x2073C:0x20740, &movs_a1_0_nop_opcode, sizeof(movs_a1_0_nop_opcode));

	enabled = 1;
}

// IO Open patched
int ps1cfw_open_filter(char file[256], int *custom_ret) {
	LOG("%s: %s\n", __func__, file);

	static int first_item = 1;
	if (first_item){
		if (strstr(file, "SCPS10084") != 0){
			LOG("%s: first item contains SCPS10084, enabling ps1cfw path filtering and patches\n", __func__);
			patch_and_enable();
		}
		first_item = 0;
	}

	if (!enabled){
		return 0;
	}

	// Virtual Kernel Exploit (allow easy escalation of priviledge on ePSP)
	if (strstr(file, "__dokxploit__") != 0){
		uint32_t *m;

		// remove k1 checks in IoRead (lets you write into kram)
		m = (uint32_t *)ScePspemuConvertAddress(0x8805769C, SCE_PSPEMU_CACHE_NONE, 4);
		*m = mips_move_a2_0; // move $a2, 0
		ScePspemuWritebackCache(m, 4);

		// remove k1 checks in IoWrite (lets you read kram)
		m = (uint32_t *)ScePspemuConvertAddress(0x880577B0, SCE_PSPEMU_CACHE_NONE, 4);
		*m = mips_move_a2_0; // move $a2, 0
		ScePspemuWritebackCache(m, 4);

		// allow running any code as kernel (lets us pass function pointer as second argument of libctime)
		m = (uint32_t *)ScePspemuConvertAddress((tai_info_saved.module_nid==0x2714F07D)?0x88010044:0x8800FFB4, SCE_PSPEMU_CACHE_NONE, 4);
		*m = mips_nop; // nop
		ScePspemuWritebackCache(m, 4);
		*custom_ret = 0;
		return 1;
	}

	// Configure currently loaded game
	char* popsetup = strstr(file, "__popsconfig__");
	if (popsetup){
		char* title_id = strchr(popsetup, '/') + 1;
		char* path = strchr(title_id, '/');
		strncpy(popsconfig.title_id, title_id, (path-title_id));
		strcpy(popsconfig.path, path);
		popsconfig.magic = ARK_MAGIC;
		*custom_ret = -101;
		return 1;
	}

	// Clear configuration
	if (strstr(file, "__popsclear__")){
	memset(&popsconfig, 0, sizeof(PopsConfig));
		*custom_ret = -102;
		return 1;
	}
    
	// Handle when system has booted
	if (strstr(file, "__popsbooted__")){
		sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);
		sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);
		sceKernelPowerUnlock(0);
		*custom_ret = -103;
		return 1;
	}
    
	// Pause POPS
	if (strstr(file, "__popspause__")){
		ScePspemuPausePops(1);
		sceDisplayWaitVblankStart();
		*custom_ret = -104;
		return 1;
	}
    
	// Resume POPS
	if (strstr(file, "__popsresume__")){
		ScePspemuPausePops(0);
		sceDisplayWaitVblankStart();
		*custom_ret = -105;
		return 1;
	}
    
	// Clean Exit
	if (strstr(file, "__popsexit__")){
		*custom_ret = ScePspemuErrorExit(0);
		return 1;
	}

	// Redirect files for memory card manager
	if (popsconfig.magic == ARK_MAGIC && popsconfig.title_id[0] && popsconfig.path[0]){
		char *p = strrchr(file, '/');
		if (p) {
			if (strcmp(p+1, "__sce_menuinfo") == 0) {
				char *filename = popsconfig.path;
				char *q = strrchr(filename, '/');
				if (q) {
					char path[128];
					strncpy(path, filename, q-(filename));
					path[q-filename] = '\0';

					snprintf(file, 256, "ms0:%s/__sce_menuinfo", path);
				}
			} else if (strstr(file, "/SCPS10084/") &&
				(strcmp(p+1, "PARAM.SFO") == 0 ||
				strcmp(p+1, "SCEVMC0.VMP") == 0 ||
				strcmp(p+1, "SCEVMC1.VMP") == 0))
			{
				snprintf(file, 256, "ms0:PSP/SAVEDATA/%s/%s", popsconfig.title_id, p+1);
			}
		}
	}
	return 0;
}

void ps1cfw_getstat_filter(char file[256]) {
	LOG("%s: %s\n", __func__, file);

	if (!enabled){
		return;
	}

	if (popsconfig.magic == ARK_MAGIC && popsconfig.title_id[0] && popsconfig.path[0]){
		char *p = strrchr(file, '/');
		if (p) {
			if (strstr(file, "/SCPS10084/") &&
				(strcmp(p+1, "PARAM.SFO") == 0 ||
				strcmp(p+1, "SCEVMC0.VMP") == 0 ||
				strcmp(p+1, "SCEVMC1.VMP") == 0))
			{
				snprintf(file, 256, "ms0:PSP/SAVEDATA/%s/%s", popsconfig.title_id, p+1);
			}
		}
	}
}

int ps1cfw_enabler_start(tai_module_info_t tai_info) {
    memset(&popsconfig, 0, sizeof(PopsConfig));

	tai_info_saved = tai_info;

    return SCE_KERNEL_START_SUCCESS;
}

int ps1cfw_enabler_stop() {

  if (io_patch_path >= 0) taiInjectRelease(io_patch_path);
  if (io_patch_size >= 0) taiInjectRelease(io_patch_size);
  if (ctrl_patch    >= 0) taiInjectRelease(ctrl_patch);

  return SCE_KERNEL_STOP_SUCCESS;
}
