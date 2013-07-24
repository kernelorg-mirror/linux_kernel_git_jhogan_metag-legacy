/*
 * romwrappers.S
 *
 * Copyright (C) 2001, 2002, 2005 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * ROM interrupt wrap routines that divert the return path from the default
 * code to a replacement. Only install these handlers if the tbi_vectors[]
 * entry is valid!
 */

        .file   "romwrappers.S"

#include <asm/traps.h>

/** Function - wrapper for fault handler */
        .text
        .type   _fault_wrapper,function
        .global _fault_wrapper
_fault_wrapper:
	MOVT	D0Re0,#HI(_tbi_vectors + (TBI_VEC_RESUME * 4))
	ADD	D0Re0,D0Re0,#LO(_tbi_vectors + (TBI_VEC_RESUME * 4))
	GETD	D1RtP,[D0Re0]
	B	_fault_handler
        .size   _fault_wrapper,.-_fault_wrapper

/** Function - wrapper for switch 1 (syscall) handler */
        .type   _switch1_wrapper,function
        .global _switch1_wrapper
_switch1_wrapper:
	MOVT	D0Re0,#HI(_tbi_vectors + (TBI_VEC_RESUME * 4))
	ADD	D0Re0,D0Re0,#LO(_tbi_vectors + (TBI_VEC_RESUME * 4))
	GETD	D1RtP,[D0Re0]
	B	_switch1_handler
        .size   _switch1_wrapper,.-_switch1_wrapper

/** Function - wrapper for unhandled switch handler */
        .type   _switchx_wrapper,function
        .global _switchx_wrapper
_switchx_wrapper:
	MOVT	D0Re0,#HI(_tbi_vectors + (TBI_VEC_RESUME * 4))
	ADD	D0Re0,D0Re0,#LO(_tbi_vectors + (TBI_VEC_RESUME * 4))
	GETD	D1RtP,[D0Re0]
	B	_switchx_handler
        .size   _switchx_wrapper,.-_switchx_wrapper

/** Function - wrapper for generic interrupt handler */
        .type   _trigger_wrapper,function
        .global _trigger_wrapper
_trigger_wrapper:
	MOVT	D0Re0,#HI(_tbi_vectors + (TBI_VEC_RESUME * 4))
	ADD	D0Re0,D0Re0,#LO(_tbi_vectors + (TBI_VEC_RESUME * 4))
	GETD	D1RtP,[D0Re0]
	B	_trigger_handler
        .size   _trigger_wrapper,.-_trigger_wrapper

/** Function - wrapper for kick interrupt handler */
        .type   _kick_wrapper,function
        .global _kick_wrapper
_kick_wrapper:
	MOVT	D0Re0,#HI(_tbi_vectors + (TBI_VEC_RESUME * 4))
	ADD	D0Re0,D0Re0,#LO(_tbi_vectors + (TBI_VEC_RESUME * 4))
	GETD	D1RtP,[D0Re0]
	B	_kick_handler
        .size   _kick_wrapper,.-_kick_wrapper
