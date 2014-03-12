/*
 * qrencode - QR Code encoder
 *
 * Reed solomon encoder. This code is taken from Phil Karn's libfec then
 * editted and packed into a pair of .c and .h files.
 *
 * Copyright (C) 2002, 2003, 2004, 2006 Phil Karn, KA9Q
 * (libfec is released under the GNU Lesser General Public License.)
 *
 * Copyright (C) 2006-2011 Kentaro Fukuchi <kentaro@fukuchi.org>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __RSCODE_H__
#define __RSCODE_H__

/*
 * General purpose RS codec, 8-bit symbols.
 */

extern struct RS *init_rs(int symsize, int gfpoly, int fcr, int prim,
			  int nroots, int pad);
extern void encode_rs_char(struct RS *rs, const unsigned char *data,
			   unsigned char *parity);
extern void free_rs_char(struct RS *rs);
extern void free_rs_cache(void);

#endif /* __RSCODE_H__ */
