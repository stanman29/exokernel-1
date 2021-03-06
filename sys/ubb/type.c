
/*
 * Copyright (C) 1997 Massachusetts Institute of Technology 
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose and without fee or royalty is
 * hereby granted, provided that the full text of this NOTICE appears on
 * ALL copies of the software and documentation or portions thereof,
 * including modifications, that you make.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED. BY WAY OF EXAMPLE,
 * BUT NOT LIMITATION, COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS OR
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR
 * THAT THE USE OF THE SOFTWARE OR DOCUMENTATION WILL NOT INFRINGE ANY
 * THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. COPYRIGHT
 * HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE OR
 * DOCUMENTATION.
 *
 * The name and trademarks of copyright holders may NOT be used in
 * advertising or publicity pertaining to the software without specific,
 * written prior permission. Title to copyright in this software and any
 * associated documentation will at all times remain with copyright
 * holders. See the file AUTHORS which should have accompanied this software
 * for a list of all copyright holders.
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by the copyright
 * holders listed in the AUTHORS file. The rest of this file is covered by
 * the copyright notices, if any, listed below.
 */

#include "xn-struct.h"
#include "registry.h"
#include "template.h"

#include <virtual-disk.h>
#include <kexcept.h>
#include <demand.h>
#include "type.h"
#include "udf.h"

/*
 * Given a disk address and type description, compute the offset 
 * within its associated type.
 */
xn_elem_t type_da_to_toff(da_t da, struct xr_td *td) {
        da_t lb, ub;
        da_t base;
        size_t size, nelem;

        size = td->size;
        base = td->base * XN_BLOCK_SIZE;
        nelem = td->nelem;

        /* Check that it is in range. */
        lb = base;
        ub = base + (nelem+1) * size;
        ensure(da >= lb && da < ub, XN_BOGUS_FIELD);

        return (da - base) % size;
}

/* Ensure that there are no sensitive fields in type.  */
xn_err_t type_writable(xn_elem_t type, cap_t cap, size_t toff, size_t nbytes) {
	struct udf_type *t;	

        ensure(t = type_lookup(type), XN_BOGUS_TYPE);

	/* Allowed if [toff, toff+nbytes) is entirely within the raw set */
	return !u_in_set(&t->u.t.raw_write, toff, nbytes) ?
		XN_BOGUS_CAP : XN_SUCCESS;
}

xn_err_t type_readable(xn_elem_t type, cap_t cap, size_t toff, size_t nbytes) {
	struct udf_type *t;	

        ensure(t = type_lookup(type), XN_BOGUS_TYPE);

	/* Allowed if [toff, toff+nbytes) is entirely within the raw set */
	return !u_in_set(&t->u.t.raw_read, toff, nbytes) ?
		XN_BOGUS_CAP : XN_SUCCESS;
}

xn_err_t type_size(xn_elem_t type) {
        struct udf_type *t;

        ensure(t = type_lookup(type), XN_BOGUS_TYPE);
        return t->nbytes;
}

size_t type_to_blocks(xn_elem_t type, int nelem) {
	long size;

	if((size = type_size(type)) < 0)
		fatal(Type must be in catalogue!);

        return bytes_to_blocks(nelem * size);
}

/* Does the parent pointer propogate down? */
int type_is_sticky(xn_elem_t type) {
        struct udf_type *t;

        t = type_lookup(type);
	demand(t, impossible);
	return t->is_sticky;
}
