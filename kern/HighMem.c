#include <vitasdkkern.h>
#include <taihen.h>
#include <psp2kern/kernel/sysmem.h>

#include "Log.h"

// Track extra memory blocks by their IDs
static SceUID extra_1_blockid = -1;
static SceUID extra_2_blockid = -1;
static SceUID mem_hooks[4] = { -1, -1, -1, -1 };

static tai_hook_ref_t ksceKernelAllocMemBlockRef;
static tai_hook_ref_t ksceKernelFreeMemBlockRef;
static tai_hook_ref_t ksceKernelUnmapMemBlockRef;
static tai_hook_ref_t SceGrabForDriver_E9C25A28_ref;

// Patch for memory block allocation: track special blocks by address
static SceUID ksceKernelAllocMemBlockPatched(const char *name, SceKernelMemBlockType type, int size, SceKernelAllocMemBlockKernelOpt *optp) {
    SceUID blockid = TAI_CONTINUE(SceUID, ksceKernelAllocMemBlockRef, name, type, size, optp);

    uint32_t addr;
    ksceKernelGetMemBlockBase(blockid, (void *)&addr);

    if (addr == 0x23000000) {
        extra_1_blockid = blockid;
    } else if (addr == 0x24000000) {
        extra_2_blockid = blockid;
    }

    return blockid;
}

// Patch for memory block free: prevent releasing extra_1 before extra_2
static int ksceKernelFreeMemBlockPatched(SceUID uid) {
    if (uid == extra_1_blockid) {
        log("%s: blocked releasing of extra_1\n", __func__);
        return 0;
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

// Patch for memory block unmap: always succeed
static int ksceKernelUnmapMemBlockPatched(SceUID uid) {
    return 0;
}

// Patch for SceGrabForDriver_E9C25A28: override address in specific case
static int SceGrabForDriver_E9C25A28_patched(int unk, uint32_t paddr) {
    log("%s: %d 0x%x\n", __func__, unk, paddr);

    if (unk == 2 && paddr == 0x21000001) {
        log("%s: overriding address 0x%x to 0x%x\n", __func__, 0x21000001, 0x22000001);
        paddr = 0x22000001;
    }

    return TAI_CONTINUE(int, SceGrabForDriver_E9C25A28_ref, unk, paddr);
}

// Initialize all hooks with error checking and logging
int init_highmem() {
    int i = 0;
    int result = 0;

    mem_hooks[0] = taiHookFunctionImportForKernel(KERNEL_PID, &ksceKernelAllocMemBlockRef, "SceCompat", 0x6F25E18A, 0xC94850C9, ksceKernelAllocMemBlockPatched);
    if (mem_hooks[0] < 0) {
        log("init_highmem: Failed to hook ksceKernelAllocMemBlock\n");
        result = -1;
        goto fail;
    }
    mem_hooks[1] = taiHookFunctionImportForKernel(KERNEL_PID, &ksceKernelFreeMemBlockRef, "SceCompat", 0x6F25E18A, 0x009E1C61, ksceKernelFreeMemBlockPatched);
    if (mem_hooks[1] < 0) {
        log("init_highmem: Failed to hook ksceKernelFreeMemBlock\n");
        result = -2;
        goto fail;
    }
    mem_hooks[2] = taiHookFunctionImportForKernel(KERNEL_PID, &ksceKernelUnmapMemBlockRef, "SceCompat", 0x6F25E18A, 0xFFCD9B60, ksceKernelUnmapMemBlockPatched);
    if (mem_hooks[2] < 0) {
        log("init_highmem: Failed to hook ksceKernelUnmapMemBlock\n");
        result = -3;
        goto fail;
    }
    mem_hooks[3] = taiHookFunctionImportForKernel(KERNEL_PID, &SceGrabForDriver_E9C25A28_ref, "SceCompat", 0x81C54BED, 0xE9C25A28, SceGrabForDriver_E9C25A28_patched);
    if (mem_hooks[3] < 0) {
        log("init_highmem: Failed to hook SceGrabForDriver_E9C25A28\n");
        result = -4;
        goto fail;
    }
    log("init_highmem: All hooks installed successfully\n");
    return 0;

fail:
    // Release any hooks that were installed before failure
    for (i = 0; i < 4; i++) {
        if (mem_hooks[i] >= 0) {
            taiHookReleaseForKernel(mem_hooks[i], NULL);
            mem_hooks[i] = -1;
        }
    }
    return result;
}

// Release all hooks and reset variables
void term_highmem() {
    for (int i = 0; i < 4; i++) {
        if (mem_hooks[i] >= 0) {
            taiHookReleaseForKernel(mem_hooks[i], NULL);
            mem_hooks[i] = -1;
        }
    }
    // Reset tracked block IDs
    extra_1_blockid = -1;
    extra_2_blockid = -1;
    log("term_highmem: All hooks released and variables reset\n");
}
