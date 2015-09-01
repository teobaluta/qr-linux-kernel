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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/qrencode.h>
#include "qrspec.h"
#include "mask.h"

static int mask_write_format_information(int width, unsigned char *frame,
					 int mask, enum qrec_level level)
{
	unsigned int format;
	unsigned char v;
	int i;
	int blacks = 0;

	format = qrspec_get_format_info(mask, level);

	for (i = 0; i < 8; i++) {
		if (format & 1) {
			blacks += 2;
			v = 0x85;
		} else {
			v = 0x84;
		}
		frame[width * 8 + width - 1 - i] = v;
		if (i < 6)
			frame[width * i + 8] = v;
		else
			frame[width * (i + 1) + 8] = v;

		format = format >> 1;
	}
	for (i = 0; i < 7; i++) {
		if (format & 1) {
			blacks += 2;
			v = 0x85;
		} else {
			v = 0x84;
		}
		frame[width * (width - 7 + i) + 8] = v;
		if (i == 0)
			frame[width * 8 + 7] = v;
		else
			frame[width * 8 + 6 - i] = v;

		format = format >> 1;
	}

	return blacks;
}

/**
 * Demerit coefficients.
 * See Section 8.8.2, pp.45, JIS X0510:2004.
 */
#define N1 (3)
#define N2 (3)
#define N3 (40)
#define N4 (10)

#define MASKMAKER(__exp__) \
	int x, y;\
	int b = 0;\
\
	for (y = 0; y < width; y++) {\
		for (x = 0; x < width; x++) {\
			if (*s & 0x80) {\
				*d = *s;\
			} else {\
				*d = *s ^ ((__exp__) == 0);\
			} \
			b += (int)(*d & 1);\
			s++; d++;\
		} \
	}

static int mask_mask0(int width, const unsigned char *s, unsigned char *d)
{
	MASKMAKER((x + y) & 1)
	return b;
}

static int mask_mask1(int width, const unsigned char *s, unsigned char *d)
{
	MASKMAKER(y & 1)
	return b;
}

static int mask_mask2(int width, const unsigned char *s, unsigned char *d)
{
	MASKMAKER(x % 3)
	return b;
}

static int mask_mask3(int width, const unsigned char *s, unsigned char *d)
{
	MASKMAKER((x + y) % 3)
	return b;
}

static int mask_mask4(int width, const unsigned char *s, unsigned char *d)
{
	MASKMAKER(((y / 2) + (x / 3)) & 1)
	return b;
}

static int mask_mask5(int width, const unsigned char *s, unsigned char *d)
{
	MASKMAKER(((x * y) & 1) + (x * y) % 3)
	return b;
}

static int mask_mask6(int width, const unsigned char *s, unsigned char *d)
{
	MASKMAKER((((x * y) & 1) + (x * y) % 3) & 1)
	return b;
}

static int mask_mask7(int width, const unsigned char *s, unsigned char *d)
{
	MASKMAKER((((x * y) % 3) + ((x + y) & 1)) & 1)
	return b;
}

#define MASKNUM (8)
typedef int mask_maker(int, const unsigned char *, unsigned char *);
static mask_maker *mask_makers[MASKNUM] = {
	mask_mask0, mask_mask1, mask_mask2, mask_mask3,
	mask_mask4, mask_mask5, mask_mask6, mask_mask7
};

unsigned char *mask_make_mask(int width, unsigned char *frame, int mask,
			      enum qrec_level level)
{
	unsigned char *masked;

	if (mask < 0 || mask >= MASKNUM)
		return NULL;

	masked = kmalloc(width * width, GFP_ATOMIC);
	if (!masked)
		return NULL;

	mask_makers[mask] (width, frame, masked);
	mask_write_format_information(width, masked, mask, level);

	return masked;
}

static int calc_n1_n3(int length, int *run_length)
{
	int i;
	int demerit = 0;
	int fact;

	for (i = 0; i < length; i++) {
		if (run_length[i] >= 5)
			demerit += N1 + (run_length[i] - 5);
		if ((i & 1)) {
			if (i >= 3 && i < length - 2 &&
			    (run_length[i] % 3) == 0) {
				fact = run_length[i] / 3;
				if (run_length[i - 2] == fact &&
				    run_length[i - 1] == fact &&
				    run_length[i + 1] == fact &&
				    run_length[i + 2] == fact) {
					if (i == 3 ||
					    run_length[i - 3] >= 4 * fact) {
						demerit += N3;
					} else if (i + 4 >= length ||
						   run_length[i + 3] >=
						   4 * fact) {
						demerit += N3;
					}
				}
			}
		}
	}

	return demerit;
}

static int mask_calc_n2(int width, unsigned char *frame)
{
	int x, y;
	unsigned char *p;
	unsigned char b22, w22;
	int demerit = 0;

	p = frame + width + 1;
	for (y = 1; y < width; y++) {
		for (x = 1; x < width; x++) {
			b22 = p[0] & p[-1] & p[-width] & p[-width - 1];
			w22 = p[0] | p[-1] | p[-width] | p[-width - 1];
			if ((b22 | (w22 ^ 1)) & 1)
				demerit += N2;
			p++;
		}
		p++;
	}

	return demerit;
}

static int mask_calc_run_length(int width, unsigned char *frame, int dir,
				int *run_length)
{
	int head;
	int i;
	unsigned char *p;
	int pitch;

	pitch = (dir == 0) ? 1 : width;
	if (frame[0] & 1) {
		run_length[0] = -1;
		head = 1;
	} else {
		head = 0;
	}
	run_length[head] = 1;
	p = frame + pitch;

	for (i = 1; i < width; i++) {
		if ((p[0] ^ p[-pitch]) & 1) {
			head++;
			run_length[head] = 1;
		} else {
			run_length[head]++;
		}
		p += pitch;
	}

	return head + 1;
}

static int mask_evaluate_symbol(int width, unsigned char *frame)
{
	int x, y;
	int demerit = 0;
	int run_length[QRSPEC_WIDTH_MAX + 1];
	int length;

	demerit += mask_calc_n2(width, frame);

	for (y = 0; y < width; y++) {
		length = mask_calc_run_length(width, frame + y * width, 0,
					      run_length);
		demerit += calc_n1_n3(length, run_length);
	}

	for (x = 0; x < width; x++) {
		length = mask_calc_run_length(width, frame + x, 1, run_length);
		demerit += calc_n1_n3(length, run_length);
	}

	return demerit;
}

unsigned char *mask_mask(int width, unsigned char *frame, enum qrec_level level)
{
	int i;
	unsigned char *mask, *best_mask;
	int min_demerit = INT_MAX;
	int blacks;
	int bratio;
	int demerit;
	int w2 = width * width;

	mask = kmalloc(w2, GFP_ATOMIC);
	if (!mask)
		return NULL;
	best_mask = NULL;

	for (i = 0; i < MASKNUM; i++) {
		demerit = 0;
		blacks = mask_makers[i] (width, frame, mask);
		blacks += mask_write_format_information(width, mask, i, level);
		bratio = (200 * blacks + w2) / w2 / 2;
		demerit = (abs(bratio - 50) / 5) * N4;
		demerit += mask_evaluate_symbol(width, mask);
		if (demerit < min_demerit) {
			min_demerit = demerit;
			kfree(best_mask);
			best_mask = mask;
			mask = kmalloc(w2, GFP_ATOMIC);
			if (!mask)
				break;
		}
	}
	kfree(mask);
	return best_mask;
}
