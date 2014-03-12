/*
 * qrencode - QR Code encoder
 *
 * Masking for Micro QR Code.
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

#ifndef __MMASK_H__
#define __MMASK_H__

extern unsigned char *MMask_makeMask(int version, unsigned char *frame,
				     int mask, enum QRecLevel level);
extern unsigned char *MMask_mask(int version, unsigned char *frame,
				 enum QRecLevel level);

#ifdef WITH_TESTS
extern int MMask_evaluateSymbol(int width, unsigned char *frame);
extern void MMask_writeFormatInformation(int version, int width,
					 unsigned char *frame, int mask,
					 enum QRecLevel level);
extern unsigned char *MMask_makeMaskedFrame(int width, unsigned char *frame,
					    int mask);
#endif

#endif /* __MMASK_H__ */
