/*
 * bit_stream - storage of bits to which you can append
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

struct bit_stream {
	u8 *data;
	int length;
	int space;
	gfp_t gfp;
};

struct bit_stream *bit_stream_allocate(int length, gfp_t gfp);
struct bit_stream *bit_stream_new(void);
void bit_stream_free(struct bit_stream *bstr);
int bit_stream_append_bytes(struct bit_stream *bstr, int len, u8 *data);
int bit_stream_append_num(struct bit_stream *bstr, int bits, int num);
int bit_stream_append(struct bit_stream *dst, struct bit_stream *src);
unsigned char *bit_stream_to_byte(struct bit_stream *bstr);

#define bit_stream_size(b) (b->length)

#endif /* __BITSTREAM_H */
