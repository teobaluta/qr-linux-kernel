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

int QRinput_isSplittableMode(enum QRencodeMode mode);

/******************************************************************************
 * Entry of input data
 *****************************************************************************/
struct QRinput_List {
	enum QRencodeMode mode;
	int size;		/*  Size of data chunk (byte). */
	unsigned char *data;	/* Data chunk. */
	struct BitStream *bstream;
	struct QRinput_List *next;
};

/******************************************************************************
 * Input Data
 *****************************************************************************/

/**
 * Singly linked list to contain input strings. An instance of this class
 * contains its version and error correction level too. It is required to
 * set them by QRinput_setVersion() and QRinput_setErrorCorrectionLevel(),
 * or use QRinput_new2() to instantiate an object.
 */
struct QRinput {
	int version;
	enum QRecLevel level;
	struct QRinput_List *head;
	struct QRinput_List *tail;
	int fnc1;
	unsigned char appid;
};

/******************************************************************************
 * Structured append input data
 *****************************************************************************/

struct QRinput_InputList {
	struct QRinput *input;
	struct QRinput_InputList *next;
};

/**
 * Set of QRinput for structured symbols.
 */
struct QRinput_Struct {
	int size;		/* number of structured symbols */
	int parity;
	struct QRinput_InputList *head;
	struct QRinput_InputList *tail;
};

/**
 * Pack all bit streams padding bits into a byte array.
 * @param input input data.
 * @return padded merged byte stream
 */
extern unsigned char *QRinput_getByteStream(struct QRinput *input);


extern int QRinput_estimateBitsModeNum(int size);
extern int QRinput_estimateBitsModeAn(int size);
extern int QRinput_estimateBitsMode8(int size);
extern int QRinput_estimateBitsModeKanji(int size);

extern struct QRinput *QRinput_dup(struct QRinput *input);

extern const signed char QRinput_anTable[128];

/**
 * Look up the alphabet-numeric convesion table (see JIS X0510:2004, pp.19).
 * @param __c__ character
 * @return value
 */
#define QRinput_lookAnTable(__c__) \
	((__c__ & 0x80) ? -1 : QRinput_anTable[(int)__c__])

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

#ifdef WITH_TESTS
extern struct BitStream *QRinput_mergeBitStream(struct QRinput *input);
extern struct BitStream *QRinput_getBitStream(struct QRinput *input);
extern int QRinput_estimateBitStreamSize(struct QRinput *input, int version);
extern int QRinput_splitEntry(struct QRinput_List *entry, int bytes);
extern int QRinput_lengthOfCode(enum QRencodeMode mode, int version, int bits);
extern int QRinput_insertStructuredAppendHeader(struct QRinput *input,
						int size, int index,
						unsigned char parity);
#endif

#endif /* __QRINPUT_H__ */
