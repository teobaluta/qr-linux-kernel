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

#include <linux/kernel.h>
#include <linux/slab.h>
#include "bitstream.h"

#define BITS_TO_BYTES(x) (((x) % 8 ) ? ((x) / 8 + 1) : ((x) / 8))

u8 *__BitStream_alloc_data(int len, gfp_t gfp)
{
	return kzalloc(BITS_TO_BYTES(len), gfp);
}

struct BitStream *BitStream_allocate(int space, gfp_t gfp)
{
	struct BitStream *bstr;
	u8 *bitmap;

	/* XXX */
	gfp = GFP_ATOMIC;

	bstr = kzalloc(sizeof(*bstr), gfp);
	if (!bstr)
		return NULL;
	
	bstr->gfp = gfp;

	if (space == 0)
		return bstr;

	bitmap = __BitStream_alloc_data(space, gfp);
	if (!bitmap) {
		kfree(bstr);
		return NULL;
	}

	bstr->_data = bitmap;
	bstr->space = space;
	bstr->length = 0;
	return bstr;
}

struct BitStream *BitStream_new(void)
{
	return BitStream_allocate(128, GFP_ATOMIC);
}

void BitStream_free(struct BitStream *bstr)
{
	if (!bstr)
		return;
	if (bstr->_data)
		kfree(bstr->_data);
	kfree(bstr);
}

int BitStream_resize(struct BitStream *bstr, int nspace)
{
	unsigned char *data;

	if (!bstr || nspace == 0)
		return -EINVAL;
	
	if (bstr->length >= nspace)
		return -ENOSPC;
	
	data = kzalloc(BITS_TO_BYTES(nspace), bstr->gfp);
	if (!data)
		return -ENOMEM;
	
	if (bstr->_data) {
		memcpy(data, bstr->_data, BITS_TO_BYTES(bstr->length));
		kfree(bstr->_data);
	}

	bstr->_data = data;
	bstr->space = nspace;
	return 0;
}

int __BitStream_get_bit(struct BitStream *bstr, int no)
{
	if (!bstr || !bstr->_data)
		return -EINVAL;
	
	if (no > bstr->length)
		return -EINVAL;

	return (bstr->_data[no / 8] & (1 << (no % 8))) ? 1 : 0;
}

int __BitStream_append_bit(struct BitStream *bstr, u8 bit)
{
	int rc;
	
	if (!bstr)
		return -EINVAL;

	if (!bstr->_data || bstr->length + 1 >= bstr->space) {
		rc = BitStream_resize(bstr, bstr->space + 256);
		if (rc)
			return rc;
	}
	
	if (bit != 0)
		bstr->_data[bstr->length / 8] |= (1UL << (bstr->length % 8));
	else {
		bstr->_data[bstr->length / 8] &= ~(1UL << (bstr->length % 8));
	}
	bstr->length ++;

	return 0;
}

int BitStream_appendBytes(struct BitStream *bstr, int bytes, u8 *data)
{
	int rc;
	int i, j;
	unsigned char mask;

	for (i = 0; i < bytes; i++) {
		mask = 0x80;
		for (j = 0; j < 8; j++) {
			rc =__BitStream_append_bit(bstr, data[i] & mask);
			if (rc)
				return rc;
			mask = mask >> 1;
		}
	}

	return 0;
}

int BitStream_appendNum(struct BitStream *bstr, int bits, int num)
{
	int rc;
	int i;
	unsigned int mask;
	
	mask = 1 << (bits - 1);
	for (i = 0; i < bits; i++) {
		rc = __BitStream_append_bit(bstr, num & mask);
		if (rc)
			return rc;
		mask = mask >> 1;
	}
	
	return 0;
}

int BitStream_append(struct BitStream *dst, struct BitStream *src)
{
	int rc, i;

	for (i = 0; i < src->length; i++) {
		rc = __BitStream_append_bit(dst, __BitStream_get_bit(src, i));
		if (rc)
			return rc;
	}
}

unsigned char *BitStream_toByte(struct BitStream *bstr)
{
	unsigned char *data, v;
	int i, j, size, bytes, p;
	
	data = kzalloc((bstr->length + 7) / 8, GFP_ATOMIC);
	if (!data)
		return NULL;
	size = BitStream_size(bstr);
	bytes = size / 8;
	p = 0;
	for (i = 0; i < bytes; i++) {
		v = 0;
		for (j = 0; j < 8; j++) {
			v = v << 1;
			v |= __BitStream_get_bit(bstr, p);
			p ++;
		}
		data[i] = v;
	}
	if (size & 7) {
		v = 0;
		for (j = 0; j < (size & 7); j++) {
			v = v << 1;
			v |= __BitStream_get_bit(bstr, p);
			p ++;
		}
		data[bytes] = v;
	}
	
	return data;
}
