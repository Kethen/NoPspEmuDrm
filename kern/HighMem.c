// NoPspEmuDrm 
// Created by Li, based on NoNpDrm kernel plugin

// highmem routines imported from Adrenaline

// below is the copyright notice for the original NoNpDrm :

/*
  NoNpDrm Plugin
  Copyright (C) 2017-2018, TheFloW

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// below is the copyright notice for the original Adrenaline :

/*
  Adrenaline
  Copyright (C) 2016-2018, TheFloW

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <vitasdkkern.h>
#include <taihen.h>
#include <psp2kern/kernel/sysmem.h>

#define LOGGING_ENABLED 1
#include "Log.h"

static SceUID extra_1_blockid = -1;
static SceUID extra_2_blockid = -1;
static SceUID mem_hooks[4];

static tai_hook_ref_t ksceKernelAllocMemBlockRef;
static tai_hook_ref_t ksceKernelFreeMemBlockRef;
static tai_hook_ref_t ksceKernelUnmapMemBlockRef;
static tai_hook_ref_t SceGrabForDriver_E9C25A28_ref;

int cached_addr_stored = 0;
void *cached_addr[3];
int uncached_addr_stored = 0;
void *uncached_addr[3];
int cdram_addr_stored = 0;
void *cdram_addr[3];

static SceUID ksceKernelAllocMemBlockPatched(const char *name, SceKernelMemBlockType type, int size, SceKernelAllocMemBlockKernelOpt *optp) {
	#define ENLARGE_ALLOC(_name, _counter) { \
		if (strcmp(name, _name) == 0 && _counter == 2){ \
			int _new_size = size + 1024 * 1024 * 4; \
			log("%s: expanding #%d %s from %d to %d\n", __func__, _counter, name, size, _new_size); \
			size = _new_size; \
		} \
	}

	#if 0
	ENLARGE_ALLOC("SceCompatCached", cached_addr_stored);
	ENLARGE_ALLOC("SceCompatUncached", uncached_addr_stored);
	ENLARGE_ALLOC("SceCompatCdram", cdram_addr_stored);
	#endif

	#undef ENLARGE_ALLOC

	SceUID blockid = TAI_CONTINUE(SceUID, ksceKernelAllocMemBlockRef, name, type, size, optp);

	uint32_t addr;
	ksceKernelGetMemBlockBase(blockid, (void *)&addr);

	if (addr == 0x23000000) {
		extra_1_blockid = blockid;
	} else if (addr == 0x24000000) {
		extra_2_blockid = blockid;
	}

	if (optp != NULL){
		log("%s: allocate name %s type 0x%x size %d paddr 0x%x attr 0x%x, 0x%x/0x%x\n", __func__, name, type, size, optp->paddr, optp->attr, addr, blockid);
	}else{
		log("%s: allocate name %s type 0x%x size %d, 0x%d/0x%d\n", __func__, name, type, size, addr, blockid);
	}

	#define SAVE_ADDR(_name, _array, _counter) { \
		if (strcmp(name, _name) == 0){ \
			_array[_counter] = addr; \
			_counter++; \
			if (_counter == 3){ \
				_counter = 0; \
			} \
		} \
	}

	SAVE_ADDR("SceCompatCached", cached_addr, cached_addr_stored);
	SAVE_ADDR("SceCompatUncached", uncached_addr, uncached_addr_stored);
	SAVE_ADDR("SceCompatCdram", cdram_addr, cdram_addr_stored);

	#undef SAVE_ADDR

	return blockid;
}

static int ksceKernelFreeMemBlockPatched(SceUID uid) {
	if (uid == extra_1_blockid){
		#if 1
		log("%s: blocked releasing of extra_1\n", __func__);
		return 0;
		#else
		log("%s: TEST releasing extra_1\n", __func__);
		extra_1_blockid = -1;
		#endif
	}

	int res = TAI_CONTINUE(int, ksceKernelFreeMemBlockRef, uid);

	if (uid == extra_2_blockid) {
		ksceKernelFreeMemBlock(extra_1_blockid);
		extra_1_blockid = -1;
		extra_2_blockid = -1;
		log("%s: released both extra_1 and extra_2\n", __func__);
	}

	return res;
}

static int ksceKernelUnmapMemBlockPatched(SceUID uid) {
	return 0;
}

static int SceGrabForDriver_E9C25A28_patched(int unk, uint32_t paddr) {
	log("%s: %d 0x%x\n", __func__, unk, paddr);

	if (unk == 2 && paddr == 0x21000001){
		log("%s: overriding address 0x%x to 0x%x\n", __func__, 0x21000001, 0x22000001);
		paddr = 0x22000001;
	}

	int result = TAI_CONTINUE(int, SceGrabForDriver_E9C25A28_ref, unk, paddr);

	if (unk == 3){
		int ret_4 = TAI_CONTINUE(int, SceGrabForDriver_E9C25A28_ref, 4, 0x24000001);
		int ret_5 = TAI_CONTINUE(int, SceGrabForDriver_E9C25A28_ref, 5, 0x25000001);
		log("%s: trying to add extra banks 4 and 5, 0x%x 0x%x\n", __func__, ret_4, ret_5);
	}

	return result;
}

static SceUID inspect_thread = -1;
static int inspect_thread_state = 0;
static int psp_mem_inspector(unsigned int args, void *argc){
	while(inspect_thread_state == 1){
		ksceKernelDelayThread(1000000 * 5);
		if (extra_2_blockid < 0){
			log("%s: psp memory not alocated\n", __func__);
			continue;
		}

		#define LOG_BANK(_num, _addr){ \
			uint32_t _val = 0; \
			ksceDmacMemcpy(&_val, _addr, sizeof(uint32_t)); \
			log("%s: bank %d (0x%x): 0x%x\n", __func__, _num, _addr, _val); \
		}

		LOG_BANK(-1, 0x20000000);
		LOG_BANK(0, 0x21000000);
		LOG_BANK(1, 0x22000000);
		LOG_BANK(2, 0x23000000);
		LOG_BANK(3, 0x24000000);
		LOG_BANK(4, 0x25000000);

		#define LOG_ADDR(_addr) { \
			uint32_t _addr_test; \
			ksceKernelVAtoPA(_addr, &_addr_test); \
			log("%s: ksceKernelVAtoPA 0x%08x -> 0x%08x\n", __func__, _addr, _addr_test); \
		}

		LOG_ADDR(0x20000000);
		LOG_ADDR(0x21000000);
		LOG_ADDR(0x22000000);
		LOG_ADDR(0x23000000);
		LOG_ADDR(0x24000000);
		LOG_ADDR(0x25000000);
		LOG_ADDR(0x74000000);
	}
	inspect_thread_state = -1;
}

void init_highmem(){
	mem_hooks[0] = taiHookFunctionImportForKernel(KERNEL_PID, &ksceKernelAllocMemBlockRef, "SceCompat", 0x6F25E18A, 0xC94850C9, ksceKernelAllocMemBlockPatched);
	mem_hooks[1] = taiHookFunctionImportForKernel(KERNEL_PID, &ksceKernelFreeMemBlockRef, "SceCompat", 0x6F25E18A, 0x009E1C61, ksceKernelFreeMemBlockPatched);
	mem_hooks[2] = taiHookFunctionImportForKernel(KERNEL_PID, &ksceKernelUnmapMemBlockRef, "SceCompat", 0x6F25E18A, 0xFFCD9B60, ksceKernelUnmapMemBlockPatched);
	mem_hooks[3] = taiHookFunctionImportForKernel(KERNEL_PID, &SceGrabForDriver_E9C25A28_ref, "SceCompat", 0x81C54BED, 0xE9C25A28, SceGrabForDriver_E9C25A28_patched);


	#if 1
	inspect_thread = ksceKernelCreateThread("inspect psp memory", psp_mem_inspector, 0x10000100, 0x10000, 0, 0, NULL);
	if (inspect_thread < 0){
		log("%s: failed creating psp memory inspect thread, 0x%x\n", __func__, inspect_thread);
		return;
	}

	inspect_thread_state = 1;
	ksceKernelStartThread(inspect_thread, 0, NULL);
	#endif
}

void term_highmem(){
	taiHookReleaseForKernel(mem_hooks[0], ksceKernelAllocMemBlockRef);
	taiHookReleaseForKernel(mem_hooks[1], ksceKernelFreeMemBlockRef);
	taiHookReleaseForKernel(mem_hooks[2], ksceKernelUnmapMemBlockRef);
	taiHookReleaseForKernel(mem_hooks[3], SceGrabForDriver_E9C25A28_ref);
}
