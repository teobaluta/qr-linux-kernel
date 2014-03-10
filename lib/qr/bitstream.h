/*
 * qrencode - QR Code encoder
 *
 * Binary sequence class.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef __BITSTREAM_H__
#define __BITSTREAM_H__

struct BitStream {
	int length;
	unsigned char *data;
};

extern struct BitStream *BitStream_new(void);
extern int BitStream_append(struct BitStream *bstream, struct BitStream *arg);
extern int BitStream_appendNum(struct BitStream *bstream, int bits, unsigned int num);
extern int BitStream_appendBytes(struct BitStream *bstream, int size, unsigned char *data);
#define BitStream_size(__bstream__) (__bstream__->length)
extern unsigned char *BitStream_toByte(struct BitStream *bstream);
extern void BitStream_free(struct BitStream *bstream);

#endif /* __BITSTREAM_H__ */
