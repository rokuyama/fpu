/*	$NetBSD: fpu_emu.c,v 1.60 2022/09/20 12:25:01 rin Exp $ */

/*
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Eduardo Horvath and Simon Burge for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)fpu.c	8.1 (Berkeley) 6/11/93
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: fpu_emu.c,v 1.60 2022/09/20 12:25:01 rin Exp $");

#ifdef _KERNEL_OPT
#include "opt_ddb.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/evcnt.h>
#include <sys/proc.h>
#include <sys/siginfo.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/syslog.h>

#include <powerpc/instr.h>
#include <powerpc/psl.h>

#include <machine/fpu.h>
#include <machine/reg.h>
#include <machine/trap.h>

#include <powerpc/fpu/fpu_emu.h>
#include <powerpc/fpu/fpu_extern.h>

#define	FPU_EMU_EVCNT_DECL(name)					\
static struct evcnt fpu_emu_ev_##name =					\
    EVCNT_INITIALIZER(EVCNT_TYPE_TRAP, NULL, "fpemu", #name);		\
EVCNT_ATTACH_STATIC(fpu_emu_ev_##name)

#define	FPU_EMU_EVCNT_INCR(name)					\
    fpu_emu_ev_##name.ev_count++

FPU_EMU_EVCNT_DECL(stfiwx);
FPU_EMU_EVCNT_DECL(fpstore);
FPU_EMU_EVCNT_DECL(fpload);
FPU_EMU_EVCNT_DECL(fcmpu);
FPU_EMU_EVCNT_DECL(frsp);
FPU_EMU_EVCNT_DECL(fctiw);
FPU_EMU_EVCNT_DECL(fcmpo);
FPU_EMU_EVCNT_DECL(mtfsb1);
FPU_EMU_EVCNT_DECL(fnegabs);
FPU_EMU_EVCNT_DECL(mcrfs);
FPU_EMU_EVCNT_DECL(mtfsb0);
FPU_EMU_EVCNT_DECL(fmr);
FPU_EMU_EVCNT_DECL(mtfsfi);
FPU_EMU_EVCNT_DECL(fnabs);
FPU_EMU_EVCNT_DECL(fabs);
FPU_EMU_EVCNT_DECL(mffs);
FPU_EMU_EVCNT_DECL(mtfsf);
FPU_EMU_EVCNT_DECL(fctid);
FPU_EMU_EVCNT_DECL(fcfid);
FPU_EMU_EVCNT_DECL(fdiv);
FPU_EMU_EVCNT_DECL(fsub);
FPU_EMU_EVCNT_DECL(fadd);
FPU_EMU_EVCNT_DECL(fsqrt);
FPU_EMU_EVCNT_DECL(fsel);
FPU_EMU_EVCNT_DECL(fpres);
FPU_EMU_EVCNT_DECL(fmul);
FPU_EMU_EVCNT_DECL(frsqrte);
FPU_EMU_EVCNT_DECL(fmsub);
FPU_EMU_EVCNT_DECL(fmadd);
FPU_EMU_EVCNT_DECL(fnmsub);
FPU_EMU_EVCNT_DECL(fnmadd);

/* FPSR exception masks */
#define FPSR_EX_MSK	(FPSCR_VX|FPSCR_OX|FPSCR_UX|FPSCR_ZX|		\
			FPSCR_XX|FPSCR_VXSNAN|FPSCR_VXISI|FPSCR_VXIDI|	\
			FPSCR_VXZDZ|FPSCR_VXIMZ|FPSCR_VXVC|FPSCR_VXSOFT|\
			FPSCR_VXSQRT|FPSCR_VXCVI)
#define	FPSR_EX		(FPSCR_VE|FPSCR_OE|FPSCR_UE|FPSCR_ZE|FPSCR_XE)
#define	FPSR_INV	(FPSCR_VXSNAN|FPSCR_VXISI|FPSCR_VXIDI|		\
			FPSCR_VXZDZ|FPSCR_VXIMZ|FPSCR_VXVC|FPSCR_VXSOFT|\
			FPSCR_VXSQRT|FPSCR_VXCVI)
#define	MCRFS_MASK							\
    (									\
	FPSCR_FX     | FPSCR_OX     |					\
	FPSCR_UX     | FPSCR_ZX     | FPSCR_XX    | FPSCR_VXSNAN |	\
	FPSCR_VXISI  | FPSCR_VXIDI  | FPSCR_VXZDZ | FPSCR_VXIMZ  |	\
	FPSCR_VXVC   |							\
	FPSCR_VXSOFT | FPSCR_VXSQRT | FPSCR_VXCVI			\
    )

#define	FR(reg)	(fs->fpreg[reg])

int fpe_debug = 0;

#ifdef DDB
extern vaddr_t opc_disasm(vaddr_t loc, int opcode);
#endif

static int fpu_execute(struct trapframe *, struct fpemu *, union instr *);

#ifdef DEBUG
/*
 * Dump a `fpn' structure.
 */
void
fpu_dumpfpn(struct fpn *fp)
{
	static const char *class[] = {
		"SNAN", "QNAN", "ZERO", "NUM", "INF"
	};

	KASSERT(fp != NULL);

	printf("%s %c.%x %x %x %xE%d\n", class[fp->fp_class + 2],
		fp->fp_sign ? '-' : ' ',
		fp->fp_mant[0],	fp->fp_mant[1],
		fp->fp_mant[2], fp->fp_mant[3],
		fp->fp_exp);
}
#endif

/*
 * fpu_execute returns the following error numbers (0 = no error):
 */
#define	FPE		1	/* take a floating point exception */
#define	NOTFPU		2	/* not an FPU instruction */
#define	FAULT		3


/*
 * Emulate a floating-point instruction.
 * Return true if insn is consumed anyway.
 * Otherwise, the caller must take care of it.
 */
bool
fpu_emulate(struct trapframe *tf, struct fpreg *fpf, ksiginfo_t *ksi)
{
	struct pcb *pcb;
	union instr insn;
	struct fpemu fe;

	KSI_INIT_TRAP(ksi);
	ksi->ksi_signo = 0;
	ksi->ksi_addr = (void *)tf->tf_srr0;

	/* initialize insn.is_datasize to tell it is *not* initialized */
	fe.fe_fpstate = fpf;
	fe.fe_cx = 0;

	/* always set this (to avoid a warning) */

	if (copyin((void *) (tf->tf_srr0), &insn.i_int, sizeof (insn.i_int))) {
#ifdef DEBUG
		printf("fpu_emulate: fault reading opcode\n");
#endif
		ksi->ksi_signo = SIGSEGV;
		ksi->ksi_trap = EXC_ISI;
		ksi->ksi_code = SEGV_MAPERR;
		return true;
	}

	DPRINTF(FPE_EX, ("fpu_emulate: emulating insn %x at %p\n",
	    insn.i_int, (void *)tf->tf_srr0));

	if ((insn.i_any.i_opcd == OPC_TWI) ||
	    ((insn.i_any.i_opcd == OPC_integer_31) &&
	    (insn.i_x.i_xo == OPC31_TW))) {
		/* Check for the two trap insns. */
		DPRINTF(FPE_EX, ("fpu_emulate: SIGTRAP\n"));
		ksi->ksi_signo = SIGTRAP;
		ksi->ksi_trap = EXC_PGM;
		ksi->ksi_code = TRAP_BRKPT;
		return true;
	}
	switch (fpu_execute(tf, &fe, &insn)) {
	case 0:
success:
		DPRINTF(FPE_EX, ("fpu_emulate: success\n"));
		tf->tf_srr0 += 4;
		return true;

	case FPE:
		pcb = lwp_getpcb(curlwp);
		if ((pcb->pcb_flags & PSL_FE_PREC) == 0)
			goto success;
		DPRINTF(FPE_EX, ("fpu_emulate: SIGFPE\n"));
		ksi->ksi_signo = SIGFPE;
		ksi->ksi_trap = EXC_PGM;
		ksi->ksi_code = fpu_get_fault_code();
		return true;

	case FAULT:
		DPRINTF(FPE_EX, ("fpu_emulate: SIGSEGV\n"));
		ksi->ksi_signo = SIGSEGV;
		ksi->ksi_trap = EXC_DSI;
		ksi->ksi_code = SEGV_MAPERR;
		ksi->ksi_addr = (void *)fe.fe_addr;
		return true;

	case NOTFPU:
	default:
		DPRINTF(FPE_EX, ("fpu_emulate: SIGILL\n"));
#if defined(DDB) && defined(DEBUG)
		if (fpe_debug & FPE_EX) {
			printf("fpu_emulate:  illegal insn %x at %p:",
			insn.i_int, (void *) (tf->tf_srr0));
			opc_disasm((vaddr_t)(tf->tf_srr0), insn.i_int);
		}
#endif
		return false;
	}
}

/*
 * fpu_to_single(): Helper function for stfs{,u}{,x}.
 *
 * Single-precision (float) data is internally represented in
 * double-precision (double) format in floating-point registers (FRs).
 * Even though double value cannot be translated into float format in
 * general, Power ISA (2.0.3--3.1) specify conversion algorithm when
 * stored to memory (see Sec. 4.6.3):
 *
 *  - Extra fraction bits are truncated regardless of rounding mode.
 *  - When magnitude is larger than the maximum number in float format,
 *    bits 63--62 and 58--29 are mechanically copied into bits 31--0.
 *  - When magnitude is representable as denormalized number in float
 *    format, it is stored as normalized double value in FRs;
 *    denormalization is required in this case.
 *  - When magnitude is smaller than the minimum denormalized number in
 *    float format, the result is undefined. For G5 (970MP Rev 1.1),
 *    (sign | 0) seems to be stored. For G4 and prior, some ``random''
 *    garbage is stored in exponent. We mimic G5 for now.
 */
static uint32_t
fpu_to_single(uint64_t reg)
{
	uint32_t sign, frac, word;
	int exp, shift;

	sign = (reg & __BIT(63)) >> 32;
	exp = __SHIFTOUT(reg, __BITS(62, 52)) - 1023;
	if (exp > -127 || (reg & ~__BIT(63)) == 0) {
		/*
		 * No denormalization required: normalized, zero, inf, NaN,
		 * or numbers larger than MAXFLOAT (see comment above).
		 *
		 * Note that MSB and 7-LSBs in exponent are same for double
		 * and float formats in this case.
		 */
		word =  ((reg & __BIT(62)) >> 32) |
		    __SHIFTOUT(reg, __BITS(58, 52) | __BITS(51, 29));
	} else if (exp <= -127 && exp >= -149) {
		/* Denormalized. */
		shift = - 126 - exp; /* 1 ... 23 */
		frac = __SHIFTOUT(__BIT(52) | reg, __BITS(52, 29 + shift));
		word = /* __SHIFTIN(0, __BITS(30, 23)) | */ frac;
	} else {
		/* Undefined. Mimic G5 for now. */
		word = 0;
	}
	return sign | word;
}

/*
 * Execute an FPU instruction (one that runs entirely in the FPU; not
 * FBfcc or STF, for instance).  On return, fe->fe_fs->fs_fsr will be
 * modified to reflect the setting the hardware would have left.
 *
 * Note that we do not catch all illegal opcodes, so you can, for instance,
 * multiply two integers this way.
 */
static int
fpu_execute(struct trapframe *tf, struct fpemu *fe, union instr *insn)
{
	struct fpn *fp;
	union instr instr = *insn;
	int *a;
	int ra, rb, rc, rt, type, mask, fsr, cx, bf, setcr, cond;
	u_int bits;
	struct fpreg *fs;
	int i;

	/* Setup work. */
	fp = NULL;
	fs = fe->fe_fpstate;
	fe->fe_fpscr = ((int *)&fs->fpscr)[1];

	/*
	 * On PowerPC all floating point values are stored in registers
	 * as doubles, even when used for single precision operations.
	 */
	type = FTYPE_DBL;
	cond = instr.i_any.i_rc;
	setcr = 0;
	bf = 0;	/* XXX gcc */

#if defined(DDB) && defined(DEBUG)
	if (fpe_debug & FPE_EX) {
		vaddr_t loc = tf->tf_srr0;

		printf("Trying to emulate: %p ", (void *)loc);
		opc_disasm(loc, instr.i_int);
	}
#endif

	/*
	 * `Decode' and execute instruction.
	 */

	if ((instr.i_any.i_opcd >= OPC_LFS && instr.i_any.i_opcd <= OPC_STFDU) ||
	    instr.i_any.i_opcd == OPC_integer_31) {
		/*
		 * Handle load/store insns:
		 *
		 * Convert to/from single if needed, calculate addr,
		 * and update index reg if needed.
		 */
		vaddr_t addr;
		size_t size = sizeof(double);
		int store, update;

		cond = 0; /* ld/st never set condition codes */


		if (instr.i_any.i_opcd == OPC_integer_31) {
			if (instr.i_x.i_xo == OPC31_STFIWX) {
				FPU_EMU_EVCNT_INCR(stfiwx);

				/* Store as integer */
				ra = instr.i_x.i_ra;
				rb = instr.i_x.i_rb;
				DPRINTF(FPE_INSN, ("reg %d has %lx reg %d has %lx\n",
					ra, tf->tf_fixreg[ra], rb, tf->tf_fixreg[rb]));

				addr = tf->tf_fixreg[rb];
				if (ra != 0)
					addr += tf->tf_fixreg[ra];
				rt = instr.i_x.i_rt;
				a = (int *)&fs->fpreg[rt];
				DPRINTF(FPE_INSN,
					("fpu_execute: Store INT %x at %p\n",
						a[1], (void *)addr));
				if (copyout(&a[1], (void *)addr, sizeof(int))) {
					fe->fe_addr = addr;
					return (FAULT);
				}
				return (0);
			}

			if ((instr.i_x.i_xo & OPC31_FPMASK) != OPC31_FPOP)
				/* Not an indexed FP load/store op */
				return (NOTFPU);

			store = (instr.i_x.i_xo & 0x80);
			if ((instr.i_x.i_xo & 0x40) == 0) {
				type = FTYPE_SNG;
				size = sizeof(float);
			}
			update = (instr.i_x.i_xo & 0x20);

			/* calculate EA of load/store */
			ra = instr.i_x.i_ra;
			rb = instr.i_x.i_rb;
			DPRINTF(FPE_INSN, ("reg %d has %lx reg %d has %lx\n",
				ra, tf->tf_fixreg[ra], rb, tf->tf_fixreg[rb]));
			addr = tf->tf_fixreg[rb];
			if (ra != 0)
				addr += tf->tf_fixreg[ra];
			rt = instr.i_x.i_rt;
		} else {
			store = instr.i_d.i_opcd & 0x4;
			if ((instr.i_d.i_opcd & 0x2) == 0) {
				type = FTYPE_SNG;
				size = sizeof(float);
			}
			update = instr.i_d.i_opcd & 0x1;

			/* calculate EA of load/store */
			ra = instr.i_d.i_ra;
			addr = instr.i_d.i_d;
			DPRINTF(FPE_INSN, ("reg %d has %lx displ %lx\n",
				ra, tf->tf_fixreg[ra], addr));
			if (ra != 0)
				addr += tf->tf_fixreg[ra];
			rt = instr.i_d.i_rt;
		}

		if (update && ra == 0)
			return (NOTFPU);

		if (store) {
			/* Store */
			uint32_t word;
			const void *kaddr;

			FPU_EMU_EVCNT_INCR(fpstore);
			if (type != FTYPE_DBL) {
				/*
				 * As Power ISA specifies conversion algorithm
				 * for store floating-point single insns, we
				 * cannot use fpu_explode() and _implode() here.
				 * See fpu_to_single() and comment therein for
				 * more details.
				 */
				DPRINTF(FPE_INSN,
					("fpu_execute: Store SNG at %p\n",
						(void *)addr));
				word = fpu_to_single(FR(rt));
				kaddr = &word;
			} else {
				DPRINTF(FPE_INSN,
					("fpu_execute: Store DBL at %p\n",
						(void *)addr));
				kaddr = &FR(rt);
			}
			if (copyout(kaddr, (void *)addr, size)) {
				fe->fe_addr = addr;
				return (FAULT);
			}
		} else {
			/* Load */
			FPU_EMU_EVCNT_INCR(fpload);
			DPRINTF(FPE_INSN, ("fpu_execute: Load from %p\n",
				(void *)addr));
			if (copyin((const void *)addr, &FR(rt), size)) {
				fe->fe_addr = addr;
				return (FAULT);
			}
			if (type != FTYPE_DBL) {
				fpu_explode(fe, fp = &fe->fe_f1, type, FR(rt));
				fpu_implode(fe, fp, FTYPE_DBL, &FR(rt));
			}
		}
		if (update)
			tf->tf_fixreg[ra] = addr;
		/* Complete. */
		return (0);
	} else if (instr.i_any.i_opcd == OPC_sp_fp_59 ||
		instr.i_any.i_opcd == OPC_dp_fp_63) {


		if (instr.i_any.i_opcd == OPC_dp_fp_63 &&
		    !(instr.i_a.i_xo & OPC63M_MASK)) {
			/* Format X */
			rt = instr.i_x.i_rt;
			ra = instr.i_x.i_ra;
			rb = instr.i_x.i_rb;


			/* One of the special opcodes.... */
			switch (instr.i_x.i_xo) {
			case	OPC63_FCMPU:
				FPU_EMU_EVCNT_INCR(fcmpu);
				DPRINTF(FPE_INSN, ("fpu_execute: FCMPU\n"));
				rt >>= 2;
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fpu_compare(fe, 0);
				/* Make sure we do the condition regs. */
				cond = 0;
				/* N.B.: i_rs is already left shifted by two. */
				bf = instr.i_x.i_rs & 0xfc;
				setcr = 1;
				break;

			case	OPC63_FRSP:
				/*
				 * Convert to single:
				 *
				 * PowerPC uses this to round a double
				 * precision value to single precision,
				 * but values in registers are always
				 * stored in double precision format.
				 */
				FPU_EMU_EVCNT_INCR(frsp);
				DPRINTF(FPE_INSN, ("fpu_execute: FRSP\n"));
				fpu_explode(fe, fp = &fe->fe_f1, FTYPE_DBL,
				    FR(rb));
				fpu_implode(fe, fp, FTYPE_SNG, &FR(rt));
				fpu_explode(fe, fp = &fe->fe_f1, FTYPE_SNG,
				    FR(rt));
				type = FTYPE_DBL | FTYPE_FPSCR;
				break;
			case	OPC63_FCTIW:
			case	OPC63_FCTIWZ:
				FPU_EMU_EVCNT_INCR(fctiw);
				DPRINTF(FPE_INSN, ("fpu_execute: FCTIW\n"));
				fpu_explode(fe, fp = &fe->fe_f1, type, FR(rb));
				type = FTYPE_INT | FTYPE_FPSCR;
				if (instr.i_x.i_xo == OPC63_FCTIWZ)
					type |= FTYPE_RD_RZ;
				break;
			case	OPC63_FCMPO:
				FPU_EMU_EVCNT_INCR(fcmpo);
				DPRINTF(FPE_INSN, ("fpu_execute: FCMPO\n"));
				rt >>= 2;
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fpu_compare(fe, 1);
				/* Make sure we do the condition regs. */
				cond = 0;
				/* N.B.: i_rs is already left shifted by two. */
				bf = instr.i_x.i_rs & 0xfc;
				setcr = 1;
				break;
			case	OPC63_MTFSB1:
				FPU_EMU_EVCNT_INCR(mtfsb1);
				DPRINTF(FPE_INSN, ("fpu_execute: MTFSB1\n"));
				fe->fe_cx = (1 << (31 - rt)) &
				    ~(FPSCR_FEX | FPSCR_VX);
				break;
			case	OPC63_FNEG:
				FPU_EMU_EVCNT_INCR(fnegabs);
				DPRINTF(FPE_INSN, ("fpu_execute: FNEGABS\n"));
				memcpy(&fs->fpreg[rt], &fs->fpreg[rb],
					sizeof(double));
				a = (int *)&fs->fpreg[rt];
				*a ^= (1 << 31);
				break;
			case	OPC63_MCRFS:
				FPU_EMU_EVCNT_INCR(mcrfs);
				DPRINTF(FPE_INSN, ("fpu_execute: MCRFS\n"));
				cond = 0;
				rt &= 0x1c;
				ra &= 0x1c;
				/* Extract the bits we want */
				bits = (fe->fe_fpscr >> (28 - ra)) & 0xf;
				/* Clear the bits we copied. */
				mask = (0xf << (28 - ra)) & MCRFS_MASK;
				fe->fe_fpscr &= ~mask;
				/* Now shove them in the right part of cr */
				tf->tf_cr &= ~(0xf << (28 - rt));
				tf->tf_cr |= bits << (28 - rt);
				break;
			case	OPC63_MTFSB0:
				FPU_EMU_EVCNT_INCR(mtfsb0);
				DPRINTF(FPE_INSN, ("fpu_execute: MTFSB0\n"));
				fe->fe_fpscr &= ~(1 << (31 - rt)) |
				    (FPSCR_FEX | FPSCR_VX);
				break;
			case	OPC63_FMR:
				FPU_EMU_EVCNT_INCR(fmr);
				DPRINTF(FPE_INSN, ("fpu_execute: FMR\n"));
				memcpy(&fs->fpreg[rt], &fs->fpreg[rb],
					sizeof(double));
				break;
			case	OPC63_MTFSFI:
				FPU_EMU_EVCNT_INCR(mtfsfi);
				DPRINTF(FPE_INSN, ("fpu_execute: MTFSFI\n"));
				rb >>= 1;
				rt &= 0x1c; /* Already left-shifted 4 */
				bits = rb << (28 - rt);
				mask = 0xf << (28 - rt);
				fe->fe_fpscr = (fe->fe_fpscr & ~mask) | bits;
				break;
			case	OPC63_FNABS:
				FPU_EMU_EVCNT_INCR(fnabs);
				DPRINTF(FPE_INSN, ("fpu_execute: FABS\n"));
				memcpy(&fs->fpreg[rt], &fs->fpreg[rb],
					sizeof(double));
				a = (int *)&fs->fpreg[rt];
				*a |= (1 << 31);
				break;
			case	OPC63_FABS:
				FPU_EMU_EVCNT_INCR(fabs);
				DPRINTF(FPE_INSN, ("fpu_execute: FABS\n"));
				memcpy(&fs->fpreg[rt], &fs->fpreg[rb],
					sizeof(double));
				a = (int *)&fs->fpreg[rt];
				*a &= ~(1 << 31);
				break;
			case	OPC63_MFFS:
				FPU_EMU_EVCNT_INCR(mffs);
				DPRINTF(FPE_INSN, ("fpu_execute: MFFS\n"));
				memcpy(&fs->fpreg[rt], &fs->fpscr,
					sizeof(fs->fpscr));
				break;
			case	OPC63_MTFSF:
				FPU_EMU_EVCNT_INCR(mtfsf);
				DPRINTF(FPE_INSN, ("fpu_execute: MTFSF\n"));
				if ((rt = instr.i_xfl.i_flm) == -1) {
					mask = -1;
				} else {
					mask = 0;
					/* Convert 1 bit -> 4 bits */
					for (i = 0; i < 8; i++)
						if (rt & (1 << i))
							mask |=
							    (0xf << (4 * i));
				}
				a = (int *)&fs->fpreg[rb];
				bits = a[1] & mask;
				fe->fe_fpscr = (fe->fe_fpscr & ~mask) | bits;
				break;
			case	OPC63_FCTID:
			case	OPC63_FCTIDZ:
				FPU_EMU_EVCNT_INCR(fctid);
				DPRINTF(FPE_INSN, ("fpu_execute: FCTID\n"));
				fpu_explode(fe, fp = &fe->fe_f1, type, FR(rb));
				type = FTYPE_LNG | FTYPE_FPSCR;
				if (instr.i_x.i_xo == OPC63_FCTIDZ)
					type |= FTYPE_RD_RZ;
				break;
			case	OPC63_FCFID:
				FPU_EMU_EVCNT_INCR(fcfid);
				DPRINTF(FPE_INSN, ("fpu_execute: FCFID\n"));
				fpu_explode(fe, fp = &fe->fe_f1, FTYPE_LNG,
				    FR(rb));
				type = FTYPE_DBL | FTYPE_FPSCR;
				break;
			default:
				return (NOTFPU);
				break;
			}
		} else {
			/* Format A */
			rt = instr.i_a.i_frt;
			ra = instr.i_a.i_fra;
			rb = instr.i_a.i_frb;
			rc = instr.i_a.i_frc;

			/*
			 * All arithmetic operations work on registers, which
			 * are stored as doubles.
			 */
			type = FTYPE_DBL;
			switch ((unsigned int)instr.i_a.i_xo) {
			case	OPC59_FDIVS:
				FPU_EMU_EVCNT_INCR(fdiv);
				DPRINTF(FPE_INSN, ("fpu_execute: FDIV\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fp = fpu_div(fe);
				break;
			case	OPC59_FSUBS:
				FPU_EMU_EVCNT_INCR(fsub);
				DPRINTF(FPE_INSN, ("fpu_execute: FSUB\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fp = fpu_sub(fe);
				break;
			case	OPC59_FADDS:
				FPU_EMU_EVCNT_INCR(fadd);
				DPRINTF(FPE_INSN, ("fpu_execute: FADD\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fp = fpu_add(fe);
				break;
			case	OPC59_FSQRTS:
				FPU_EMU_EVCNT_INCR(fsqrt);
				DPRINTF(FPE_INSN, ("fpu_execute: FSQRT\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(rb));
				fp = fpu_sqrt(fe);
				break;
			case	OPC63M_FSEL:
				FPU_EMU_EVCNT_INCR(fsel);
				DPRINTF(FPE_INSN, ("fpu_execute: FSEL\n"));
				a = (int *)&fe->fe_fpstate->fpreg[ra];
				if ((( a[0] & 0x80000000) &&
				     ((a[0] & 0x7fffffff) | a[1])) ||
				    (( a[0] & 0x7ff00000) &&
				     ((a[0] & 0x000fffff) | a[1]))) {
					/* negative/NaN or NaN */
					rc = rb;
				}
				DPRINTF(FPE_INSN, ("f%d => f%d\n", rc, rt));
				memcpy(&fs->fpreg[rt], &fs->fpreg[rc],
					sizeof(double));
				break;
			case	OPC59_FRES:
				FPU_EMU_EVCNT_INCR(fpres);
				DPRINTF(FPE_INSN, ("fpu_execute: FPRES\n"));
				fpu_explode(fe, &fe->fe_f1, FTYPE_INT, 1);
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fp = fpu_div(fe);
				break;
			case	OPC59_FMULS:
				FPU_EMU_EVCNT_INCR(fmul);
				DPRINTF(FPE_INSN, ("fpu_execute: FMUL\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rc));
				fp = fpu_mul(fe);
				break;
			case	OPC63M_FRSQRTE:
				/* Reciprocal sqrt() estimate */
				FPU_EMU_EVCNT_INCR(frsqrte);
				DPRINTF(FPE_INSN, ("fpu_execute: FRSQRTE\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(rb));
				fp = fpu_sqrt(fe);
				fe->fe_f2 = *fp;
				fpu_explode(fe, &fe->fe_f1, FTYPE_INT, 1);
				fp = fpu_div(fe);
				break;
			case	OPC59_FMSUBS:
				FPU_EMU_EVCNT_INCR(fmsub);
				DPRINTF(FPE_INSN, ("fpu_execute: FMSUB\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rc));
				fp = fpu_mul(fe);
				fe->fe_f1 = *fp;
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fp = fpu_sub(fe);
				break;
			case	OPC59_FMADDS:
				FPU_EMU_EVCNT_INCR(fmadd);
				DPRINTF(FPE_INSN, ("fpu_execute: FMADD\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rc));
				fp = fpu_mul(fe);
				fe->fe_f1 = *fp;
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fp = fpu_add(fe);
				break;
			case	OPC59_FNMSUBS:
				FPU_EMU_EVCNT_INCR(fnmsub);
				DPRINTF(FPE_INSN, ("fpu_execute: FNMSUB\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rc));
				fp = fpu_mul(fe);
				fe->fe_f1 = *fp;
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fp = fpu_sub(fe);
				/* Negate */
				if (!ISNAN(fp))
					fp->fp_sign ^= 1;
				break;
			case	OPC59_FNMADDS:
				FPU_EMU_EVCNT_INCR(fnmadd);
				DPRINTF(FPE_INSN, ("fpu_execute: FNMADD\n"));
				fpu_explode(fe, &fe->fe_f1, type, FR(ra));
				fpu_explode(fe, &fe->fe_f2, type, FR(rc));
				fp = fpu_mul(fe);
				fe->fe_f1 = *fp;
				fpu_explode(fe, &fe->fe_f2, type, FR(rb));
				fp = fpu_add(fe);
				/* Negate */
				if (!ISNAN(fp))
					fp->fp_sign ^= 1;
				break;
			default:
				return (NOTFPU);
				break;
			}

			/* If the instruction was single precision, round */
			if (!(instr.i_any.i_opcd & 0x4)) {
				fpu_implode(fe, fp, FTYPE_SNG | FTYPE_FPSCR,
				    &FR(rt));
				fpu_explode(fe, fp = &fe->fe_f1, FTYPE_SNG,
				    FR(rt));
			} else
				type |= FTYPE_FPSCR;
		}
	} else {
		return (NOTFPU);
	}

	/*
	 * ALU operation is complete.  Collapse the result and then check
	 * for exceptions.  If we got any, and they are enabled, do not
	 * alter the destination register, just stop with an exception.
	 * Otherwise set new current exceptions and accrue.
	 */
	if (fp)
		fpu_implode(fe, fp, type, &FR(rt));
	cx = fe->fe_cx;
	fsr = fe->fe_fpscr & ~(FPSCR_FEX|FPSCR_VX);
	if (cx != 0) {
		fsr |= cx;
		DPRINTF(FPE_INSN, ("fpu_execute: cx %x, fsr %x\n", cx, fsr));
	}
	if (fsr & FPSR_INV)
		fsr |= FPSCR_VX;
	mask = (fsr & FPSR_EX) << (25 - 3);
	if (fsr & mask)
		fsr |= FPSCR_FEX;
	if ((fsr ^ fe->fe_fpscr) & FPSR_EX_MSK)
		fsr |= FPSCR_FX;

	if (cond) {
		bits = fsr & 0xf0000000;
		/* Isolate condition codes */
		bits >>= 28;
		/* Move fpu condition codes to cr[1] */
		tf->tf_cr &= ~(0x0f000000);
		tf->tf_cr |= (bits << 24);
		DPRINTF(FPE_INSN, ("fpu_execute: cr[1] <= %x\n", bits));
	}

	if (setcr) {
		bits = fsr & FPSCR_FPCC;
		/* Isolate condition codes */
		bits <<= 16;
		/* Move fpu condition codes to cr[bf/4] */
		tf->tf_cr &= ~(0xf0000000>>bf);
		tf->tf_cr |= (bits >> bf);
		DPRINTF(FPE_INSN, ("fpu_execute: cr[%d] (cr=%x) <= %x\n", bf/4, tf->tf_cr, bits));
	}

	((int *)&fs->fpscr)[1] = fsr;
	if (fsr & FPSCR_FEX)
		return(FPE);
	return (0);	/* success */
}
