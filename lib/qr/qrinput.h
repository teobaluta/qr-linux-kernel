/*
 * qrencode - QR Code encoder
 *
 * Input data chunk class
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

#ifndef __QRINPUT_H__
#define __QRINPUT_H__

#include <linux/qrencode.h>
#include "bitstream.h"

int qrinput_is_splittable_mode(enum qrencode_mode mode);

/******************************************************************************
 * Entry of input data
 *****************************************************************************/
struct qrinput_list {
	enum qrencode_mode mode;
	int size;		/*  Size of data chunk (byte). */
	unsigned char *data;	/* Data chunk. */
	struct bit_stream *bstream;
	struct qrinput_list *next;
};

/******************************************************************************
 * Input Data
 *****************************************************************************/

/**
 * Singly linked list to contain input strings. An instance of this class
 * contains its version and error correction level too. It is required to
 * set them by qrinput_set_version() and qrinput_set_error_correction_level(),
 * or use qrinput_new2() to instantiate an object.
 */
struct qrinput {
	int version;
	enum qrec_level level;
	struct qrinput_list *head;
	struct qrinput_list *tail;
	int fnc1;
	unsigned char appid;
};

/******************************************************************************
 * Structured append input data
 *****************************************************************************/

struct qrinput_input_list {
	struct qrinput *input;
	struct qrinput_input_list *next;
};

/**
 * Set of qrinput for structured symbols.
 */
struct qrinput_struct {
	int size;		/* number of structured symbols */
	int parity;
	struct qrinput_input_list *head;
	struct qrinput_input_list *tail;
};

/**
 * Pack all bit streams padding bits into a byte array.
 * @param input input data.
 * @return padded merged byte stream
 */
unsigned char *qrinput_get_byte_stream(struct qrinput *input);

int qrinput_estimate_bits_mode_num(int size);
int qrinput_estimate_bits_mode_an(int size);
int qrinput_estimate_bits_mode8(int size);
int qrinput_estimate_bits_mode_kanji(int size);

struct qrinput *qrinput_dup(struct qrinput *input);

extern const signed char qrinput_an_table[128];

/**
 * Look up the alphabet-numeric convesion table (see JIS X0510:2004, pp.19).
 * @param __c__ character
 * @return value
 */
#define qrinput_look_an_table(__c__) \
	((__c__ & 0x80) ? -1 : qrinput_an_table[(int)__c__])

/**
 * Length of a standard mode indicator in bits.
 */
#define MODE_INDICATOR_SIZE 4

/**
 * Length of a segment of structured-append header.
 */
#define STRUCTURE_HEADER_SIZE 20

/**
 * Maximum number of symbols in a set of structured-appended symbols.
 */
#define MAX_STRUCTURED_SYMBOLS 16

#endif /* __QRINPUT_H__ */
