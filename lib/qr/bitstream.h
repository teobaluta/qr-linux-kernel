/*
 * BitStream - storage of bits to which you can append
 *
 * Copyright (C) 2014 Levente Kurusa <levex@linux.com>
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
#ifndef __BITSTREAM_H
#define __BITSTREAM_H

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>

struct BitStream {
	u8 *_data;
	int length;
	int space;
	spinlock_t lock;
	gfp_t gfp;
};

extern struct BitStream *BitStream_allocate(int length, gfp_t gfp);
extern struct BitStream *BitStream_new(void);
extern void BitStream_free(struct BitStream *bstr);
extern int BitStream_appendBytes(struct BitStream *bstr, int len, u8 *data);
extern int BitStream_appendNum(struct BitStream *bstr, int bits, int num);
extern int BitStream_append(struct BitStream *dst, struct BitStream *src);
extern unsigned char *BitStream_toByte(struct BitStream *bstr);

#define BitStream_size(b) (b->length)


#endif /* __BITSTREAM_H */
