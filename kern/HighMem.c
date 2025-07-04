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
#include <psp2kern/kernel/threadmgr.h>

#define LOGGING_ENABLED 1
#include "Log.h"

static SceUID extra_1_blockid = -1;
static SceUID extra_2_blockid = -1;
static SceUID extra_3_blockid = -1;
static void* extra_3_addr = NULL;
static void* extra_3_addr_mapped;

static SceUID mem_hooks[4];

static tai_hook_ref_t ksceKernelAllocMemBlockRef;
static tai_hook_ref_t ksceKernelFreeMemBlockRef;
static tai_hook_ref_t ksceKernelUnmapMemBlockRef;
static tai_hook_ref_t SceGrabForDriver_E9C25A28_ref;

static SceUID ksceKernelAllocMemBlockPatched(const char *name, SceKernelMemBlockType type, int size, SceKernelAllocMemBlockKernelOpt *optp) {
	SceUID blockid = TAI_CONTINUE(SceUID, ksceKernelAllocMemBlockRef, name, type, size, optp);

	uint32_t addr;
	ksceKernelGetMemBlockBase(blockid, (void *)&addr);

	if (addr == 0x23000000) {
		extra_1_blockid = blockid;
	} else if (addr == 0x24000000) {
		extra_2_blockid = blockid;

		#if 1
		struct SceKernelAllocMemBlockKernelOpt block_opt = {0};
		block_opt.size = sizeof(block_opt);
		block_opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_PHYCONT;
		static const extra_3_size = 1024 * 1024 * 32;
		extra_3_blockid = ksceKernelAllocMemBlock("MORE", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_GAME_RW, extra_3_size, &block_opt);


		if (extra_3_blockid < 0){
			log("%s: failed allocating extra memory, 0x%x\n", __func__, extra_3_blockid);
		}else{
			ksceKernelGetMemBlockBase(extra_3_blockid, (void *)&extra_3_addr_mapped);
			struct SceKernelVARange va_range = {
				.addr = extra_3_addr_mapped,
				.size = extra_3_size
			};
			struct SceKernelPARange pa_range = {0};
			
			int range_get_status = ksceKernelVARangeToPARange(&va_range, &pa_range);
			if (range_get_status == 0){
				log("%s: physical address is 0x%x with size %d\n", __func__, pa_range.addr, pa_range.size);
				extra_3_addr = pa_range.addr;
			}else{
				log("%s: failed getting physical range, 0x%x\n", __func__, range_get_status);
				extra_3_addr = NULL;
			}

			log("%s: allocated extra memory, 0x%x/0x%x 0x%x\n", __func__, extra_3_addr_mapped, extra_3_addr, extra_3_blockid);
		}
		#endif
	}

	if (addr >= 0x20000000 && addr < 0x27000000){
		log("%s: game cdram alloc %s with size %d type 0x%x, 0x%x 0x%x\n", __func__, name, size, type, addr, blockid);
	}

	return blockid;
}

static int ksceKernelFreeMemBlockPatched(SceUID uid) {
	if (uid == extra_1_blockid){
		log("%s: blocked releasing of extra_1\n", __func__);
		return 0;
	}

	if (uid == extra_2_blockid){
		extra_2_blockid = -1;
	}

	int res = TAI_CONTINUE(int, ksceKernelFreeMemBlockRef, uid);

	if (uid == extra_2_blockid) {
		ksceKernelFreeMemBlock(extra_1_blockid);
		extra_1_blockid = -1;

		log("%s: released both extra_1 and extra_2\n", __func__);
		if (extra_3_blockid >= 0){
			ksceKernelFreeMemBlock(extra_3_blockid);
			extra_3_blockid = -1;
			extra_3_addr = NULL;
			log("%s: released extra_3\n", __func__);
		}
	}

	return res;
}

static int ksceKernelUnmapMemBlockPatched(SceUID uid) {
	return 0;
}

static int SceGrabForDriver_E9C25A28_patched(int unk, uint32_t paddr) {
	log("%s: %d 0x%x\n", __func__, unk, paddr);

	if (unk == 2 && paddr == 0x21000001){
		log("%s: overriding bank 2 address 0x%x to 0x%x\n", __func__, 0x21000001, 0x22000001);
		paddr = 0x22000001;
	}

	#if 0
	if (unk == 3 && extra_3_addr != NULL){
		log("%s: overridng bank 3 address 0x%x to 0x%x\n", __func__, paddr, extra_3_addr);
		paddr = extra_3_addr;
	}
	#else
	if (unk == 3 && extra_3_addr != NULL){
		int result = TAI_CONTINUE(int, SceGrabForDriver_E9C25A28_ref, 4, extra_3_addr);
		log("%s: bank 4, 0x%x\n", __func__, result);
	}
	#endif

	int result = TAI_CONTINUE(int, SceGrabForDriver_E9C25A28_ref, unk, paddr);
	log("%s: %d 0x%x, 0x%x\n", __func__, unk, paddr, result);
	return result;
}

SceUID inspect_thread = -1;
int inspect_thread_state = 0;
int psp_mem_inspector(unsigned int args, void *argc){
	while(inspect_thread_state == 1){
		ksceKernelDelayThread(1000000 * 5);
		if (extra_2_blockid < 0){
			log("%s: psp memory not alocated\n", __func__);
			continue;
		}
		log("%s: bank 2: 0x%x\n", __func__, *(uint32_t*)0x22000000);
		log("%s: bank 3: 0x%x\n", __func__, *(uint32_t*)0x23000000);
		log("%s: bank 3 + 16MB: 0x%x\n", __func__, *(uint32_t*)0x24000000);
		if (extra_3_addr != NULL){
			uint32_t val = 0;
			ksceDmacMemcpy(&val, extra_3_addr, sizeof(val));
			log("%s: bank 4: 0x%x\n", __func__, val);
		}
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

	if (inspect_thread >= 0){
		inspect_thread_state = 0;
		while(inspect_thread_state != -1){
			ksceKernelDelayThread(10000);
		}
		ksceKernelDeleteThread(inspect_thread);
		inspect_thread = -1;
	}
}
