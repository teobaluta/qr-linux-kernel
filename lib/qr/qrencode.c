/*
 * qrencode - QR Code encoder
 *
 * Copyright (C) 2014 Levente Kurusa <levex@linux.com>
 * Copyright (C) 2006-2012 Kentaro Fukuchi <kentaro@fukuchi.org>
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

#include "qrencode.h"
#include "qrspec.h"
#include "bitstream.h"
#include "qrinput.h"
#include "rscode.h"
#include "split.h"
#include "mask.h"
#include "mmask.h"

/******************************************************************************
 * Raw code
 *****************************************************************************/

struct RSblock {
	int dataLength;
	unsigned char *data;
	int eccLength;
	unsigned char *ecc;
};

struct QRRawCode {
	int version;
	int dataLength;
	int eccLength;
	unsigned char *datacode;
	unsigned char *ecccode;
	int b1;
	int blocks;
	struct RSblock *rsblock;
	int count;
};

static void RSblock_initBlock(struct RSblock *block, int dl,
			      unsigned char *data,
			      int el, unsigned char *ecc, struct RS *rs)
{
	block->dataLength = dl;
	block->data = data;
	block->eccLength = el;
	block->ecc = ecc;

	encode_rs_char(rs, data, ecc);
}

static int RSblock_init(struct RSblock *blocks, int spec[5],
			unsigned char *data,
			unsigned char *ecc)
{
	int i;
	struct RSblock *block;
	unsigned char *dp, *ep;
	struct RS *rs;
	int el, dl;

	dl = QRspec_rsDataCodes1(spec);
	el = QRspec_rsEccCodes1(spec);
	rs = init_rs(8, 0x11d, 0, 1, el, 255 - dl - el);
	if (rs == NULL)
		return -1;

	block = blocks;
	dp = data;
	ep = ecc;
	for (i = 0; i < QRspec_rsBlockNum1(spec); i++) {
		RSblock_initBlock(block, dl, dp, el, ep, rs);
		dp += dl;
		ep += el;
		block++;
	}

	if (QRspec_rsBlockNum2(spec) == 0)
		return 0;

	dl = QRspec_rsDataCodes2(spec);
	el = QRspec_rsEccCodes2(spec);
	rs = init_rs(8, 0x11d, 0, 1, el, 255 - dl - el);
	if (rs == NULL)
		return -1;
	for (i = 0; i < QRspec_rsBlockNum2(spec); i++) {
		RSblock_initBlock(block, dl, dp, el, ep, rs);
		dp += dl;
		ep += el;
		block++;
	}

	return 0;
}

static void QRraw_free(struct QRRawCode *raw);
static struct QRRawCode *QRraw_new(struct QRinput *input)
{
	struct QRRawCode *raw;
	int spec[5], ret;

	raw = kmalloc(sizeof(struct QRRawCode), GFP_ATOMIC);
	if (raw == NULL)
		return NULL;

	raw->datacode = QRinput_getByteStream(input);
	if (raw->datacode == NULL) {
		kfree(raw);
		return NULL;
	}

	QRspec_getEccSpec(input->version, input->level, spec);

	raw->version = input->version;
	raw->b1 = QRspec_rsBlockNum1(spec);
	raw->dataLength = QRspec_rsDataLength(spec);
	raw->eccLength = QRspec_rsEccLength(spec);
	raw->ecccode = kmalloc(raw->eccLength, GFP_ATOMIC);
	if (raw->ecccode == NULL) {
		kfree(raw->datacode);
		kfree(raw);
		return NULL;
	}

	raw->blocks = QRspec_rsBlockNum(spec);
	raw->rsblock = kcalloc(raw->blocks, sizeof(struct RSblock), GFP_ATOMIC);
	if (raw->rsblock == NULL) {
		QRraw_free(raw);
		return NULL;
	}
	ret = RSblock_init(raw->rsblock, spec, raw->datacode, raw->ecccode);
	if (ret < 0) {
		QRraw_free(raw);
		return NULL;
	}

	raw->count = 0;

	return raw;
}

/**
 * Return a code (byte).
 * This function can be called iteratively.
 * @param raw raw code.
 * @return code
 */
static unsigned char QRraw_getCode(struct QRRawCode *raw)
{
	int col, row;
	unsigned char ret;

	if (raw->count < raw->dataLength) {
		row = raw->count % raw->blocks;
		col = raw->count / raw->blocks;
		if (col >= raw->rsblock[0].dataLength)
			row += raw->b1;
		ret = raw->rsblock[row].data[col];
	} else if (raw->count < raw->dataLength + raw->eccLength) {
		row = (raw->count - raw->dataLength) % raw->blocks;
		col = (raw->count - raw->dataLength) / raw->blocks;
		ret = raw->rsblock[row].ecc[col];
	} else {
		return 0;
	}
	raw->count++;
	return ret;
}

static void QRraw_free(struct QRRawCode *raw)
{
	if (raw != NULL) {
		kfree(raw->datacode);
		kfree(raw->ecccode);
		kfree(raw->rsblock);
		kfree(raw);
	}
}

/******************************************************************************
 * Frame filling
 *****************************************************************************/

struct FrameFiller {
	int width;
	unsigned char *frame;
	int x, y;
	int dir;
	int bit;
};

static struct FrameFiller *FrameFiller_new(int width, unsigned char *frame)
{
	struct FrameFiller *filler;

	filler = kmalloc(sizeof(struct FrameFiller), GFP_ATOMIC);
	if (filler == NULL)
		return NULL;
	filler->width = width;
	filler->frame = frame;
	filler->x = width - 1;
	filler->y = width - 1;
	filler->dir = -1;
	filler->bit = -1;

	return filler;
}

static unsigned char *FrameFiller_next(struct FrameFiller *filler)
{
	unsigned char *p;
	int x, y, w;

	if (filler->bit == -1) {
		filler->bit = 0;
		return filler->frame + filler->y * filler->width + filler->x;
	}

	x = filler->x;
	y = filler->y;
	p = filler->frame;
	w = filler->width;

	if (filler->bit == 0) {
		x--;
		filler->bit++;
	} else {
		x++;
		y += filler->dir;
		filler->bit--;
	}

	if (filler->dir < 0) {
		if (y < 0) {
			y = 0;
			x -= 2;
			filler->dir = 1;
			if (x == 6) {
				x--;
				y = 9;
			}
		}
	} else {
		if (y == w) {
			y = w - 1;
			x -= 2;
			filler->dir = -1;
			if (x == 6) {
				x--;
				y -= 8;
			}
		}
	}
	if (x < 0 || y < 0)
		return NULL;

	filler->x = x;
	filler->y = y;

	if (p[y * w + x] & 0x80) {
		/* This tail recursion could be optimized. */
		return FrameFiller_next(filler);
	}
	return &p[y * w + x];
}

/******************************************************************************
 * QR-code encoding
 *****************************************************************************/

static struct QRcode *QRcode_new(int version, int width, unsigned char *data)
{
	struct QRcode *qrcode;

	qrcode = kmalloc(sizeof(struct QRcode), GFP_ATOMIC);
	if (qrcode == NULL)
		return NULL;

	qrcode->version = version;
	qrcode->width = width;
	qrcode->data = data;

	return qrcode;
}

void QRcode_free(struct QRcode *qrcode)
{
	if (qrcode != NULL) {
		kfree(qrcode->data);
		kfree(qrcode);
	}
}
EXPORT_SYMBOL_GPL(QRcode_free);

static struct QRcode *QRcode_encodeMask(struct QRinput *input, int mask)
{
	int width, version;
	struct QRRawCode *raw;
	unsigned char *frame, *masked, *p, code, bit;
	struct FrameFiller *filler;
	int i, j;
	struct QRcode *qrcode = NULL;

	if (input->version < 0 || input->version > QRSPEC_VERSION_MAX)
		return NULL;

	if (input->level > QR_ECLEVEL_H)
		return NULL;

	raw = QRraw_new(input);
	if (raw == NULL)
		return NULL;

	version = raw->version;
	width = QRspec_getWidth(version);
	frame = QRspec_newFrame(version);
	if (frame == NULL) {
		QRraw_free(raw);
		return NULL;
	}
	filler = FrameFiller_new(width, frame);
	if (filler == NULL) {
		QRraw_free(raw);
		kfree(frame);
		return NULL;
	}

	/* inteleaved data and ecc codes */
	for (i = 0; i < raw->dataLength + raw->eccLength; i++) {
		code = QRraw_getCode(raw);
		bit = 0x80;
		for (j = 0; j < 8; j++) {
			p = FrameFiller_next(filler);
			if (p == NULL)
				goto EXIT;
			*p = 0x02 | ((bit & code) != 0);
			bit = bit >> 1;
		}
	}
	QRraw_free(raw);
	raw = NULL;
	/* remainder bits */
	j = QRspec_getRemainder(version);
	for (i = 0; i < j; i++) {
		p = FrameFiller_next(filler);
		if (p == NULL)
			goto EXIT;
		*p = 0x02;
	}

	/* masking */
	if (mask < 0)
		masked = Mask_mask(width, frame, input->level);
	else
		masked = Mask_makeMask(width, frame, mask, input->level);

	if (masked == NULL)
		goto EXIT;

	qrcode = QRcode_new(version, width, masked);

EXIT:
	QRraw_free(raw);
	kfree(filler);
	kfree(frame);
	return qrcode;
}

struct QRcode *QRcode_encodeInput(struct QRinput *input)
{
	return QRcode_encodeMask(input, -1);

}
EXPORT_SYMBOL_GPL(QRcode_encodeInput);

static struct QRcode *QRcode_encodeStringReal(const char *string, int version,
				       enum QRecLevel level,
				       enum QRencodeMode hint,
				       int casesensitive)
{
	struct QRinput *input;
	struct QRcode *code;
	int ret;

	if (string == NULL)
		return NULL;

	if (hint != QR_MODE_8 && hint != QR_MODE_KANJI)
		return NULL;


	input = QRinput_new2(version, level);

	if (input == NULL)
		return NULL;

	ret = Split_splitStringToQRinput(string, input, hint, casesensitive);
	if (ret < 0) {
		QRinput_free(input);
		return NULL;
	}
	code = QRcode_encodeInput(input);
	QRinput_free(input);

	return code;
}

struct QRcode *QRcode_encodeString(const char *string, int version,
				   enum QRecLevel level, enum QRencodeMode hint,
				   int casesensitive)
{
	return QRcode_encodeStringReal(string, version, level, hint,
				       casesensitive);
}
EXPORT_SYMBOL_GPL(QRcode_encodeString);

static struct QRcode *QRcode_encodeDataReal(const unsigned char *data,
					    int length, int version,
					    enum QRecLevel level)
{
	struct QRinput *input;
	struct QRcode *code;
	int ret;

	if (data == NULL || length == 0)
		return NULL;

	input = QRinput_new2(version, level);

	if (input == NULL)
		return NULL;

	ret = QRinput_append(input, QR_MODE_8, length, data);
	if (ret < 0) {
		QRinput_free(input);
		return NULL;
	}
	code = QRcode_encodeInput(input);
	QRinput_free(input);

	return code;
}

struct QRcode *QRcode_encodeData(int size, const unsigned char *data,
				 int version, enum QRecLevel level)
{
	return QRcode_encodeDataReal(data, size, version, level);
}
EXPORT_SYMBOL_GPL(QRcode_encodeData);

struct QRcode *QRcode_encodeString8bit(const char *string, int version,
				enum QRecLevel level)
{
	if (string == NULL)
		return NULL;

	return QRcode_encodeDataReal((unsigned char *)string, strlen(string),
				     version, level);
}
EXPORT_SYMBOL_GPL(QRcode_encodeString8bit);

/******************************************************************************
 * Structured QR-code encoding
 *****************************************************************************/

static struct QRcode_List *QRcode_List_newEntry(void)
{
	struct QRcode_List *entry;

	entry = kmalloc(sizeof(struct QRcode_List), GFP_ATOMIC);
	if (entry == NULL)
		return NULL;

	entry->next = NULL;
	entry->code = NULL;

	return entry;
}

static void QRcode_List_freeEntry(struct QRcode_List *entry)
{
	if (entry != NULL) {
		QRcode_free(entry->code);
		kfree(entry);
	}
}

void QRcode_List_free(struct QRcode_List *qrlist)
{
	struct QRcode_List *list = qrlist, *next;

	while (list != NULL) {
		next = list->next;
		QRcode_List_freeEntry(list);
		list = next;
	}
}
EXPORT_SYMBOL_GPL(QRcode_List_free);

int QRcode_List_size(struct QRcode_List *qrlist)
{
	struct QRcode_List *list = qrlist;
	int size = 0;

	while (list != NULL) {
		size++;
		list = list->next;
	}

	return size;
}
EXPORT_SYMBOL_GPL(QRcode_List_size);

struct QRcode_List *QRcode_encodeInputStructured(struct QRinput_Struct *s)
{
	struct QRcode_List *head = NULL;
	struct QRcode_List *tail = NULL;
	struct QRcode_List *entry;
	struct QRinput_InputList *list = s->head;

	while (list != NULL) {
		if (head == NULL) {
			entry = QRcode_List_newEntry();
			if (entry == NULL)
				goto ABORT;
			head = entry;
			tail = head;
		} else {
			entry = QRcode_List_newEntry();
			if (entry == NULL)
				goto ABORT;
			tail->next = entry;
			tail = tail->next;
		}
		tail->code = QRcode_encodeInput(list->input);
		if (tail->code == NULL)
			goto ABORT;
		list = list->next;
	}

	return head;
ABORT:
	QRcode_List_free(head);
	return NULL;
}
EXPORT_SYMBOL_GPL(QRcode_encodeInputStructured);

static struct QRcode_List *QRcode_encodeInputToStructured(struct QRinput *input)
{
	struct QRinput_Struct *s;
	struct QRcode_List *codes;

	s = QRinput_splitQRinputToStruct(input);
	if (s == NULL)
		return NULL;

	codes = QRcode_encodeInputStructured(s);
	QRinput_Struct_free(s);

	return codes;
}

static struct QRcode_List *QRcode_encodeDataStructuredReal(int size,
						    const unsigned char *data,
						    int version,
						    enum QRecLevel level,
						    int eightbit,
						    enum QRencodeMode hint,
						    int casesensitive)
{
	struct QRinput *input;
	struct QRcode_List *codes;
	int ret;

	if (version <= 0)
		return NULL;

	if (!eightbit && (hint != QR_MODE_8 && hint != QR_MODE_KANJI))
		return NULL;

	input = QRinput_new2(version, level);
	if (input == NULL)
		return NULL;

	if (eightbit) {
		ret = QRinput_append(input, QR_MODE_8, size, data);
	} else {
		ret =
		    Split_splitStringToQRinput((char *)data, input, hint,
					       casesensitive);
	}
	if (ret < 0) {
		QRinput_free(input);
		return NULL;
	}
	codes = QRcode_encodeInputToStructured(input);
	QRinput_free(input);

	return codes;
}

struct QRcode_List *QRcode_encodeDataStructured(int size,
						const unsigned char *data,
						int version,
						enum QRecLevel level)
{
	return QRcode_encodeDataStructuredReal(size, data, version, level, 1,
					       QR_MODE_NUL, 0);
}
EXPORT_SYMBOL_GPL(QRcode_encodeDataStructured);

struct QRcode_List *QRcode_encodeString8bitStructured(const char *string,
						      int version,
						      enum QRecLevel level)
{
	if (string == NULL)
		return NULL;

	return QRcode_encodeDataStructured(strlen(string),
					   (unsigned char *)string, version,
					   level);
}
EXPORT_SYMBOL_GPL(QRcode_encodeString8bitStructured);

struct QRcode_List
*QRcode_encodeStringStructured(const char *string, int version,
			       enum QRecLevel level, enum QRencodeMode hint,
			       int casesensitive)
{
	if (string == NULL)
		return NULL;

	return QRcode_encodeDataStructuredReal(strlen(string),
					       (unsigned char *)string, version,
					       level, 0, hint, casesensitive);
}
EXPORT_SYMBOL_GPL(QRcode_encodeStringStructured);

