/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#if !defined(CPU_AVR_H)
#define CPU_AVR_H

#include "qemu-common.h"

#define TARGET_LONG_BITS 32

#define CPUArchState struct CPUAVRState

#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"

/*
 * TARGET_PAGE_BITS cannot be more than 8 bits because
 * 1.  all IO registers occupy [0x0000 .. 0x00ff] address range, and they
 *     should be implemented as a device and not memory
 * 2.  SRAM starts at the address 0x0100
 */
#define TARGET_PAGE_BITS 8
#define TARGET_PHYS_ADDR_SPACE_BITS 24
#define TARGET_VIRT_ADDR_SPACE_BITS 24
#define NB_MMU_MODES 2

/*
 * AVR has two memory spaces, data & code.
 * e.g. both have 0 address
 * ST/LD instructions access data space
 * LPM/SPM and instruction fetching access code memory space
 */
#define MMU_CODE_IDX 0
#define MMU_DATA_IDX 1

#define EXCP_RESET 1
#define EXCP_INT(n) (EXCP_RESET + (n) + 1)

/* Number of CPU registers */
#define NO_CPU_REGISTERS 32
/* Number of IO registers accessible by ld/st/in/out */
#define NO_IO_REGISTERS 64

/*
 * Offsets of AVR memory regions in host memory space.
 *
 * This is needed because the AVR has separate code and data address
 * spaces that both have start from zero but have to go somewhere in
 * host memory.
 *
 * It's also useful to know where some things are, like the IO registers.
 */
/* Flash program memory */
#define OFFSET_CODE 0x00000000
/* CPU registers, IO registers, and SRAM */
#define OFFSET_DATA 0x00800000
/* CPU registers specifically, these are mapped at the start of data */
#define OFFSET_CPU_REGISTERS OFFSET_DATA
/*
 * IO registers, including status register, stack pointer, and memory
 * mapped peripherals, mapped just after CPU registers
 */
#define OFFSET_IO_REGISTERS (OFFSET_DATA + NO_CPU_REGISTERS)

enum avr_features {
    AVR_FEATURE_SRAM,

    AVR_FEATURE_1_BYTE_PC,
    AVR_FEATURE_2_BYTE_PC,
    AVR_FEATURE_3_BYTE_PC,

    AVR_FEATURE_1_BYTE_SP,
    AVR_FEATURE_2_BYTE_SP,

    AVR_FEATURE_BREAK,
    AVR_FEATURE_DES,
    AVR_FEATURE_RMW, /* Read Modify Write - XCH LAC LAS LAT */

    AVR_FEATURE_EIJMP_EICALL,
    AVR_FEATURE_IJMP_ICALL,
    AVR_FEATURE_JMP_CALL,

    AVR_FEATURE_ADIW_SBIW,

    AVR_FEATURE_SPM,
    AVR_FEATURE_SPMX,

    AVR_FEATURE_ELPMX,
    AVR_FEATURE_ELPM,
    AVR_FEATURE_LPMX,
    AVR_FEATURE_LPM,

    AVR_FEATURE_MOVW,
    AVR_FEATURE_MUL,
    AVR_FEATURE_RAMPD,
    AVR_FEATURE_RAMPX,
    AVR_FEATURE_RAMPY,
    AVR_FEATURE_RAMPZ,
};

typedef struct CPUAVRState CPUAVRState;

struct CPUAVRState {
    uint32_t pc_w; /* 0x003fffff up to 22 bits */

    uint32_t sregC; /* 0x00000001 1 bits */
    uint32_t sregZ; /* 0x0000ffff 16 bits, negative logic; */
                    /* 0=flag set, >0=flag cleared */
    uint32_t sregN; /* 0x00000001 1 bits */
    uint32_t sregV; /* 0x00000001 1 bits */
    uint32_t sregS; /* 0x00000001 1 bits */
    uint32_t sregH; /* 0x00000001 1 bits */
    uint32_t sregT; /* 0x00000001 1 bits */
    uint32_t sregI; /* 0x00000001 1 bits */

    uint32_t rampD; /* 0x00ff0000 8 bits */
    uint32_t rampX; /* 0x00ff0000 8 bits */
    uint32_t rampY; /* 0x00ff0000 8 bits */
    uint32_t rampZ; /* 0x00ff0000 8 bits */
    uint32_t eind; /* 0x00ff0000 8 bits */

    uint32_t r[NO_CPU_REGISTERS]; /* 8 bits each */
    uint32_t sp; /* 16 bits */

    uint64_t intsrc; /* interrupt sources */
    bool fullacc; /* CPU/MEM if true MEM only otherwise */

    uint32_t features;

    /* Those resources are used only in QEMU core */
    CPU_COMMON
};

static inline int avr_feature(CPUAVRState *env, int feature)
{
    return (env->features & (1U << feature)) != 0;
}

static inline void avr_set_feature(CPUAVRState *env, int feature)
{
    env->features |= (1U << feature);
}

#define cpu_list avr_cpu_list
#define cpu_signal_handler cpu_avr_signal_handler

#include "exec/cpu-all.h"
#include "cpu-qom.h"

#define AVR_CPU_TYPE_SUFFIX "-" TYPE_AVR_CPU
#define AVR_CPU_TYPE_NAME(model) model AVR_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_AVR_CPU

static inline int cpu_mmu_index(CPUAVRState *env, bool ifetch)
{
    return ifetch ? MMU_CODE_IDX : MMU_DATA_IDX;
}

void avr_cpu_tcg_init(void);

void avr_cpu_list(void);
int cpu_avr_exec(CPUState *cpu);
int cpu_avr_signal_handler(int host_signum, void *pinfo, void *puc);
int avr_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int size,
                                int rw, int mmu_idx);
int avr_cpu_memory_rw_debug(CPUState *cs, vaddr address, uint8_t *buf,
                                int len, bool is_write);

enum {
    TB_FLAGS_FULL_ACCESS = 1,
};

static inline void cpu_get_tb_cpu_state(CPUAVRState *env, target_ulong *pc,
                                target_ulong *cs_base, uint32_t *pflags)
{
    uint32_t flags = 0;

    *pc = env->pc_w * 2;
    *cs_base = 0;

    if (env->fullacc) {
        flags |= TB_FLAGS_FULL_ACCESS;
    }

    *pflags = flags;
}

static inline int cpu_interrupts_enabled(CPUAVRState *env)
{
    return env->sregI != 0;
}

static inline uint8_t cpu_get_sreg(CPUAVRState *env)
{
    uint8_t sreg;
    sreg = (env->sregC & 0x01) << 0
         | (env->sregZ == 0 ? 1 : 0) << 1
         | (env->sregN) << 2
         | (env->sregV) << 3
         | (env->sregS) << 4
         | (env->sregH) << 5
         | (env->sregT) << 6
         | (env->sregI) << 7;
    return sreg;
}

static inline void cpu_set_sreg(CPUAVRState *env, uint8_t sreg)
{
    env->sregC = (sreg >> 0) & 0x01;
    env->sregZ = (sreg >> 1) & 0x01 ? 0 : 1;
    env->sregN = (sreg >> 2) & 0x01;
    env->sregV = (sreg >> 3) & 0x01;
    env->sregS = (sreg >> 4) & 0x01;
    env->sregH = (sreg >> 5) & 0x01;
    env->sregT = (sreg >> 6) & 0x01;
    env->sregI = (sreg >> 7) & 0x01;
}

#include "exec/exec-all.h"

#endif /* !defined (CPU_AVR_H) */
