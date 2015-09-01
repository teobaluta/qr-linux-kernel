/*
 * qrencode - QR Code encoder
 *
 * Masking.
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

#ifndef __MASK_H__
#define __MASK_H__

unsigned char *mask_make_mask(int width, unsigned char *frame,
			      int mask, enum qrec_level level);

unsigned char *mask_mask(int width, unsigned char *frame,
			 enum qrec_level level);

#endif /* __MASK_H__ */
