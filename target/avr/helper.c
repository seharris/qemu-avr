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

#include "qemu/osdep.h"

#include "cpu.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/ioport.h"
#include "qemu/host-utils.h"
#include "qemu/error-report.h"

bool avr_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    bool ret = false;
    CPUClass *cc = CPU_GET_CLASS(cs);
    AVRCPU *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;

    if (interrupt_request & CPU_INTERRUPT_RESET) {
        if (cpu_interrupts_enabled(env)) {
            cs->exception_index = EXCP_RESET;
            cc->do_interrupt(cs);

            cs->interrupt_request &= ~CPU_INTERRUPT_RESET;

            ret = true;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        if (cpu_interrupts_enabled(env) && env->intsrc != 0) {
            int index = ctz32(env->intsrc);
            cs->exception_index = EXCP_INT(index);
            cc->do_interrupt(cs);

            env->intsrc &= env->intsrc - 1; /* clear the interrupt */
            cs->interrupt_request &= ~CPU_INTERRUPT_HARD;

            ret = true;
        }
    }
    return ret;
}

void avr_cpu_do_interrupt(CPUState *cs)
{
    AVRCPU *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;

    uint32_t ret = env->pc_w;
    int vector = 0;
    int size = avr_feature(env, AVR_FEATURE_JMP_CALL) ? 2 : 1;
    int base = 0;

    if (cs->exception_index == EXCP_RESET) {
        vector = 0;
    } else if (env->intsrc != 0) {
        vector = ctz32(env->intsrc) + 1;
    }

    if (avr_feature(env, AVR_FEATURE_3_BYTE_PC)) {
        cpu_stb_data(env, env->sp--, (ret & 0x0000ff));
        cpu_stb_data(env, env->sp--, (ret & 0x00ff00) >> 8);
        cpu_stb_data(env, env->sp--, (ret & 0xff0000) >> 16);
    } else if (avr_feature(env, AVR_FEATURE_2_BYTE_PC)) {
        cpu_stb_data(env, env->sp--, (ret & 0x0000ff));
        cpu_stb_data(env, env->sp--, (ret & 0x00ff00) >> 8);
    } else {
        cpu_stb_data(env, env->sp--, (ret & 0x0000ff));
    }

    env->pc_w = base + vector * size;
    env->sregI = 0; /* clear Global Interrupt Flag */

    cs->exception_index = -1;
}

int avr_cpu_memory_rw_debug(CPUState *cs, vaddr addr, uint8_t *buf,
                                int len, bool is_write)
{
    return cpu_memory_rw_debug(cs, addr, buf, len, is_write);
}

hwaddr avr_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return addr; /* I assume 1:1 address correspondance */
}

int avr_cpu_handle_mmu_fault(
    CPUState *cs, vaddr address, int size, int rw, int mmu_idx)
{
    /* currently it's assumed that this will never happen */
    cs->exception_index = EXCP_DEBUG;
    cpu_dump_state(cs, stderr, 0);
    return 1;
}

void tlb_fill(CPUState *cs, target_ulong vaddr, int size,
              MMUAccessType access_type, int mmu_idx, uintptr_t retaddr)
{
    int prot = 0;
    MemTxAttrs attrs = {};
    uint32_t paddr;

    vaddr &= TARGET_PAGE_MASK;

    if (mmu_idx == MMU_CODE_IDX) {
        /* access to code in flash */
        paddr = OFFSET_CODE + vaddr;
        prot = PAGE_READ | PAGE_EXEC;
        if (paddr + TARGET_PAGE_SIZE > OFFSET_DATA) {
            error_report("execution left flash memory");
            exit(1);
        }
    } else if (vaddr < NO_CPU_REGISTERS + NO_IO_REGISTERS) {
        /*
         * access to CPU registers, exit and rebuilt this TB to use full access
         * incase it touches specially handled registers like SREG or SP
         */
        AVRCPU *cpu = AVR_CPU(cs);
        CPUAVRState *env = &cpu->env;
        env->fullacc = 1;
        cpu_loop_exit_restore(cs, retaddr);
    } else {
        /* access to memory. nothing special */
        paddr = OFFSET_DATA + vaddr;
        prot = PAGE_READ | PAGE_WRITE;
    }

    tlb_set_page_with_attrs(
        cs, vaddr, paddr, attrs, prot, mmu_idx, TARGET_PAGE_SIZE);
}

void helper_sleep(CPUAVRState *env)
{
    CPUState *cs = CPU(avr_env_get_cpu(env));

    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

void helper_unsupported(CPUAVRState *env)
{
    CPUState *cs = CPU(avr_env_get_cpu(env));

    /*
     *  I count not find what happens on the real platform, so
     *  it's EXCP_DEBUG for meanwhile
     */
    cs->exception_index = EXCP_DEBUG;
    if (qemu_loglevel_mask(LOG_UNIMP)) {
        qemu_log("UNSUPPORTED\n");
        cpu_dump_state(cs, qemu_logfile, 0);
    }
    cpu_loop_exit(cs);
}

void helper_debug(CPUAVRState *env)
{
    CPUState *cs = CPU(avr_env_get_cpu(env));

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

void helper_wdr(CPUAVRState *env)
{
    CPUState *cs = CPU(avr_env_get_cpu(env));

    /* WD is not implemented yet, placeholder */
    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

/*
 * This function implements IN instruction
 *
 * It does the following
 * a.  if an IO register belongs to CPU, its value is read and returned
 * b.  otherwise io address is translated to mem address and physical memory
 *     is read.
 * c.  it caches the value for sake of SBI, SBIC, SBIS & CBI implementation
 *
 */
target_ulong helper_inb(CPUAVRState *env, uint32_t port)
{
    target_ulong data = 0;

    switch (port) {
    case 0x38: /* RAMPD */
        data = 0xff & (env->rampD >> 16);
        break;
    case 0x39: /* RAMPX */
        data = 0xff & (env->rampX >> 16);
        break;
    case 0x3a: /* RAMPY */
        data = 0xff & (env->rampY >> 16);
        break;
    case 0x3b: /* RAMPZ */
        data = 0xff & (env->rampZ >> 16);
        break;
    case 0x3c: /* EIND */
        data = 0xff & (env->eind >> 16);
        break;
    case 0x3d: /* SPL */
        data = env->sp & 0x00ff;
        break;
    case 0x3e: /* SPH */
        data = env->sp >> 8;
        break;
    case 0x3f: /* SREG */
        data = cpu_get_sreg(env);
        break;
    default:
        /* not a special register, pass to normal memory access */
        cpu_physical_memory_read(OFFSET_IO_REGISTERS + port, &data, 1);
    }

    return data;
}

/*
 *  This function implements OUT instruction
 *
 *  It does the following
 *  a.  if an IO register belongs to CPU, its value is written into the register
 *  b.  otherwise io address is translated to mem address and physical memory
 *      is written.
 *  c.  it caches the value for sake of SBI, SBIC, SBIS & CBI implementation
 *
 */
void helper_outb(CPUAVRState *env, uint32_t port, uint32_t data)
{
    data &= 0x000000ff;

    switch (port) {
    case 0x38: /* RAMPD */
        if (avr_feature(env, AVR_FEATURE_RAMPD)) {
            env->rampD = (data & 0xff) << 16;
        }
        break;
    case 0x39: /* RAMPX */
        if (avr_feature(env, AVR_FEATURE_RAMPX)) {
            env->rampX = (data & 0xff) << 16;
        }
        break;
    case 0x3a: /* RAMPY */
        if (avr_feature(env, AVR_FEATURE_RAMPY)) {
            env->rampY = (data & 0xff) << 16;
        }
        break;
    case 0x3b: /* RAMPZ */
        if (avr_feature(env, AVR_FEATURE_RAMPZ)) {
            env->rampZ = (data & 0xff) << 16;
        }
        break;
    case 0x3c: /* EIDN */
        env->eind = (data & 0xff) << 16;
        break;
    case 0x3d: /* SPL */
        env->sp = (env->sp & 0xff00) | (data);
        break;
    case 0x3e: /* SPH */
        if (avr_feature(env, AVR_FEATURE_2_BYTE_SP)) {
            env->sp = (env->sp & 0x00ff) | (data << 8);
        }
        break;
    case 0x3f: /* SREG */
        cpu_set_sreg(env, data);
        break;
    default:
        /* not a special register, pass to normal memory access */
        cpu_physical_memory_write(OFFSET_IO_REGISTERS + port, &data, 1);
    }
}

/*
 *  this function implements LD instruction when there is a posibility to read
 *  from a CPU register
 */
target_ulong helper_fullrd(CPUAVRState *env, uint32_t addr)
{
    uint8_t data;

    env->fullacc = false;

    if (addr < NO_CPU_REGISTERS) {
        /* CPU registers */
        data = env->r[addr];
    } else if (addr < NO_CPU_REGISTERS + NO_IO_REGISTERS) {
        /* IO registers */
        data = helper_inb(env, addr - NO_CPU_REGISTERS);
    } else {
        /* memory */
        cpu_physical_memory_read(OFFSET_DATA + addr, &data, 1);
    }
    return data;
}

/*
 *  this function implements ST instruction when there is a posibility to write
 *  into a CPU register
 */
void helper_fullwr(CPUAVRState *env, uint32_t data, uint32_t addr)
{
    env->fullacc = false;

    /* Following logic assumes this: */
    assert(OFFSET_CPU_REGISTERS == OFFSET_DATA);
    assert(OFFSET_IO_REGISTERS == OFFSET_CPU_REGISTERS + NO_CPU_REGISTERS);

    if (addr < NO_CPU_REGISTERS) {
        /* CPU registers */
        env->r[addr] = data;
    } else if (addr < NO_CPU_REGISTERS + NO_IO_REGISTERS) {
        /* IO registers */
        helper_outb(env, addr - NO_CPU_REGISTERS, data);
    } else {
        /* memory */
        cpu_physical_memory_write(OFFSET_DATA + addr, &data, 1);
    }
}
