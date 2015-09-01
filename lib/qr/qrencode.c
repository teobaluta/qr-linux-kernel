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
#include <linux/qrencode.h>
#include <linux/rslib.h>

#include "qrencode.h"
#include "qrspec.h"
#include "bitstream.h"
#include "qrinput.h"
#include "split.h"
#include "mask.h"
#include "mmask.h"

/******************************************************************************
 * Raw code
 *****************************************************************************/

struct rsblock {
	int data_length;
	unsigned char *data;
	int ecc_length;
	unsigned char *ecc;
};

struct qrraw_code {
	int version;
	int data_length;
	int ecc_length;
	unsigned char *datacode;
	unsigned char *ecccode;
	int b1;
	int blocks;
	struct rsblock *rsblock;
	int count;
};

static void rsblock_init_block(struct rsblock *block,
			       int dl,
			       unsigned char *data,
			       int el,
			       unsigned char *ecc,
			       struct rs_control *rs)
{
	int i;
	u16 par[el];

	block->data_length = dl;
	block->data = data;
	block->ecc_length = el;
	block->ecc = ecc;

	memset(par, 0, sizeof(par));
	encode_rs8(rs, data, dl, par, 0x0000);

	for (i = 0; i < el; i++)
		ecc[i] = par[i];
}

static int rsblock_init(struct rsblock *blocks, int spec[5],
			unsigned char *data,
			unsigned char *ecc)
{
	int i;
	struct rsblock *block;
	unsigned char *dp, *ep;
	struct rs_control *rs;
	int el, dl;

	dl = qrspec_rs_data_codes1(spec);
	el = qrspec_rs_ecc_codes1(spec);
	rs = init_rs(8, 0x11d, 0, 1, el);
	if (!rs)
		return -1;

	block = blocks;
	dp = data;
	ep = ecc;
	for (i = 0; i < qrspec_rs_block_num1(spec); i++) {
		rsblock_init_block(block, dl, dp, el, ep, rs);
		dp += dl;
		ep += el;
		block++;
	}

	if (qrspec_rs_block_num2(spec) == 0)
		return 0;

	dl = qrspec_rs_data_codes2(spec);
	el = qrspec_rs_ecc_codes2(spec);
	rs = init_rs(8, 0x11d, 0, 1, el);
	if (!rs)
		return -1;
	for (i = 0; i < qrspec_rs_block_num2(spec); i++) {
		rsblock_init_block(block, dl, dp, el, ep, rs);
		dp += dl;
		ep += el;
		block++;
	}

	return 0;
}

static void qrraw_free(struct qrraw_code *raw);
static struct qrraw_code *qrraw_new(struct qrinput *input)
{
	struct qrraw_code *raw;
	int spec[5], ret;

	raw = kmalloc(sizeof(*raw), GFP_ATOMIC);
	if (!raw)
		return NULL;

	raw->datacode = qrinput_get_byte_stream(input);
	if (!raw->datacode) {
		kfree(raw);
		return NULL;
	}

	qrspec_get_ecc_spec(input->version, input->level, spec);

	raw->version = input->version;
	raw->b1 = qrspec_rs_block_num1(spec);
	raw->data_length = qrspec_rs_data_length(spec);
	raw->ecc_length = qrspec_rs_ecc_length(spec);
	raw->ecccode = kmalloc(raw->ecc_length, GFP_ATOMIC);
	if (!raw->ecccode) {
		kfree(raw->datacode);
		kfree(raw);
		return NULL;
	}

	raw->blocks = qrspec_rs_block_num(spec);
	raw->rsblock = kcalloc(raw->blocks, sizeof(struct rsblock), GFP_ATOMIC);
	if (!raw->rsblock) {
		qrraw_free(raw);
		return NULL;
	}
	ret = rsblock_init(raw->rsblock, spec, raw->datacode, raw->ecccode);
	if (ret < 0) {
		qrraw_free(raw);
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
static unsigned char qrraw_get_code(struct qrraw_code *raw)
{
	int col, row;
	unsigned char ret;

	if (raw->count < raw->data_length) {
		row = raw->count % raw->blocks;
		col = raw->count / raw->blocks;
		if (col >= raw->rsblock[0].data_length)
			row += raw->b1;
		ret = raw->rsblock[row].data[col];
	} else if (raw->count < raw->data_length + raw->ecc_length) {
		row = (raw->count - raw->data_length) % raw->blocks;
		col = (raw->count - raw->data_length) / raw->blocks;
		ret = raw->rsblock[row].ecc[col];
	} else {
		return 0;
	}
	raw->count++;
	return ret;
}

static void qrraw_free(struct qrraw_code *raw)
{
	if (raw) {
		kfree(raw->datacode);
		kfree(raw->ecccode);
		kfree(raw->rsblock);
		kfree(raw);
	}
}

/******************************************************************************
 * Frame filling
 *****************************************************************************/

struct frame_filler {
	int width;
	unsigned char *frame;
	int x, y;
	int dir;
	int bit;
};

static struct frame_filler *frame_filler_new(int width, unsigned char *frame)
{
	struct frame_filler *filler;

	filler = kmalloc(sizeof(*filler), GFP_ATOMIC);
	if (!filler)
		return NULL;
	filler->width = width;
	filler->frame = frame;
	filler->x = width - 1;
	filler->y = width - 1;
	filler->dir = -1;
	filler->bit = -1;

	return filler;
}

static unsigned char *frame_filler_next(struct frame_filler *filler)
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
		return frame_filler_next(filler);
	}
	return &p[y * w + x];
}

/******************************************************************************
 * QR-code encoding
 *****************************************************************************/

static struct qrcode *qrcode_new(int version, int width, unsigned char *data)
{
	struct qrcode *qrcode;

	qrcode = kmalloc(sizeof(*qrcode), GFP_ATOMIC);
	if (!qrcode)
		return NULL;

	qrcode->version = version;
	qrcode->width = width;
	qrcode->data = data;

	return qrcode;
}

void qrcode_free(struct qrcode *qrcode)
{
	if (!qrcode) {
		kfree(qrcode->data);
		kfree(qrcode);
	}
}
EXPORT_SYMBOL_GPL(qrcode_free);

static struct qrcode *qrcode_encode_mask(struct qrinput *input, int mask)
{
	int width, version;
	struct qrraw_code *raw;
	unsigned char *frame, *masked, *p, code, bit;
	struct frame_filler *filler;
	int i, j;
	struct qrcode *qrcode = NULL;

	if (input->version < 0 || input->version > QRSPEC_VERSION_MAX)
		return NULL;

	if (input->level > QR_ECLEVEL_H)
		return NULL;

	raw = qrraw_new(input);
	if (!raw)
		return NULL;

	version = raw->version;
	width = qrspec_get_width(version);
	frame = qrspec_new_frame(version);
	if (!frame) {
		qrraw_free(raw);
		return NULL;
	}
	filler = frame_filler_new(width, frame);
	if (!filler) {
		qrraw_free(raw);
		kfree(frame);
		return NULL;
	}

	/* inteleaved data and ecc codes */
	for (i = 0; i < raw->data_length + raw->ecc_length; i++) {
		code = qrraw_get_code(raw);
		bit = 0x80;
		for (j = 0; j < 8; j++) {
			p = frame_filler_next(filler);
			if (!p)
				goto EXIT;
			*p = 0x02 | ((bit & code) != 0);
			bit = bit >> 1;
		}
	}
	qrraw_free(raw);
	raw = NULL;
	/* remainder bits */
	j = qrspec_get_remainder(version);
	for (i = 0; i < j; i++) {
		p = frame_filler_next(filler);
		if (!p)
			goto EXIT;
		*p = 0x02;
	}

	/* masking */
	if (mask < 0)
		masked = mask_mask(width, frame, input->level);
	else
		masked = mask_make_mask(width, frame, mask, input->level);

	if (!masked)
		goto EXIT;

	qrcode = qrcode_new(version, width, masked);

EXIT:
	qrraw_free(raw);
	kfree(filler);
	kfree(frame);
	return qrcode;
}

struct qrcode *qrcode_encode_input(struct qrinput *input)
{
	return qrcode_encode_mask(input, -1);
}
EXPORT_SYMBOL_GPL(qrcode_encode_input);

static struct qrcode *qrcode_encode_string_real(const char *string, int version,
						enum qrec_level level,
						enum qrencode_mode hint,
						int casesensitive)
{
	struct qrinput *input;
	struct qrcode *code;
	int ret;

	if (!string)
		return NULL;

	if (hint != QR_MODE_8)
		return NULL;

	input = qrinput_new2(version, level);

	if (!input)
		return NULL;

	ret = split_split_string_to_qrinput(string, input, hint, casesensitive);
	if (ret < 0) {
		qrinput_free(input);
		return NULL;
	}
	code = qrcode_encode_input(input);
	qrinput_free(input);

	return code;
}

struct qrcode *qrcode_encode_string(const char *string,
				    int version,
				    enum qrec_level level,
				    enum qrencode_mode hint,
				    int casesensitive)
{
	return qrcode_encode_string_real(string, version, level, hint,
					 casesensitive);
}
EXPORT_SYMBOL_GPL(qrcode_encode_string);

static struct qrcode *qrcode_encode_data_real(const unsigned char *data,
					      int length, int version,
					      enum qrec_level level)
{
	struct qrinput *input;
	struct qrcode *code;
	int ret;

	if (!data || length == 0)
		return NULL;

	input = qrinput_new2(version, level);

	if (!input)
		return NULL;

	ret = qrinput_append(input, QR_MODE_8, length, data);
	if (ret < 0) {
		qrinput_free(input);
		return NULL;
	}
	code = qrcode_encode_input(input);
	qrinput_free(input);

	return code;
}

struct qrcode *qrcode_encode_data(int size, const unsigned char *data,
				  int version, enum qrec_level level)
{
	return qrcode_encode_data_real(data, size, version, level);
}
EXPORT_SYMBOL_GPL(qrcode_encode_data);

struct qrcode *qrcode_encode_string_8bit(const char *string, int version,
					 enum qrec_level level)
{
	if (!string)
		return NULL;

	return qrcode_encode_data_real((unsigned char *)string, strlen(string),
				       version, level);
}
EXPORT_SYMBOL_GPL(qrcode_encode_string_8bit);

/******************************************************************************
 * Structured QR-code encoding
 *****************************************************************************/

static struct qrcode_list *qrcode_list_new_entry(void)
{
	struct qrcode_list *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return NULL;

	entry->next = NULL;
	entry->code = NULL;

	return entry;
}

static void qrcode_list_free_entry(struct qrcode_list *entry)
{
	if (entry) {
		qrcode_free(entry->code);
		kfree(entry);
	}
}

void qrcode_list_free(struct qrcode_list *qrlist)
{
	struct qrcode_list *list = qrlist, *next;

	while (list) {
		next = list->next;
		qrcode_list_free_entry(list);
		list = next;
	}
}
EXPORT_SYMBOL_GPL(qrcode_list_free);

int qrcode_list_size(struct qrcode_list *qrlist)
{
	struct qrcode_list *list = qrlist;
	int size = 0;

	while (list) {
		size++;
		list = list->next;
	}

	return size;
}
EXPORT_SYMBOL_GPL(qrcode_list_size);

struct qrcode_list *qrcode_encode_input_structured(struct qrinput_struct *s)
{
	struct qrcode_list *head = NULL;
	struct qrcode_list *tail = NULL;
	struct qrcode_list *entry;
	struct qrinput_input_list *list = s->head;

	while (list) {
		if (!head) {
			entry = qrcode_list_new_entry();
			if (!entry)
				goto ABORT;
			head = entry;
			tail = head;
		} else {
			entry = qrcode_list_new_entry();
			if (!entry)
				goto ABORT;
			tail->next = entry;
			tail = tail->next;
		}
		tail->code = qrcode_encode_input(list->input);
		if (!tail->code)
			goto ABORT;
		list = list->next;
	}

	return head;
ABORT:
	qrcode_list_free(head);
	return NULL;
}
EXPORT_SYMBOL_GPL(qrcode_encode_input_structured);

static
struct qrcode_list *qrcode_encode_input_to_structured(struct qrinput *input)
{
	struct qrinput_struct *s;
	struct qrcode_list *codes;

	s = qrinput_split_qrinput_to_struct(input);
	if (!s)
		return NULL;

	codes = qrcode_encode_input_structured(s);
	qrinput_struct_free(s);

	return codes;
}

static struct qrcode_list *qrcode_encode_data_structured_real
						(int size,
						 const unsigned char *data,
						 int version,
						 enum qrec_level level,
						 int eightbit,
						 enum qrencode_mode hint,
						 int casesensitive)
{
	struct qrinput *input;
	struct qrcode_list *codes;
	int ret;

	if (version <= 0)
		return NULL;

	if (!eightbit && hint != QR_MODE_8)
		return NULL;

	input = qrinput_new2(version, level);
	if (!input)
		return NULL;

	if (eightbit) {
		ret = qrinput_append(input, QR_MODE_8, size, data);
	} else {
		ret =
		    split_split_string_to_qrinput((char *)data, input, hint,
						  casesensitive);
	}
	if (ret < 0) {
		qrinput_free(input);
		return NULL;
	}
	codes = qrcode_encode_input_to_structured(input);
	qrinput_free(input);

	return codes;
}

struct qrcode_list *qrcode_encode_data_structured(int size,
						  const unsigned char *data,
						  int version,
						  enum qrec_level level)
{
	return qrcode_encode_data_structured_real(size, data, version, level, 1,
						  QR_MODE_NUL, 0);
}
EXPORT_SYMBOL_GPL(qrcode_encode_data_structured);

struct qrcode_list *qrcode_encode_string_8bit_structured
					(const char *string,
					 int version,
					 enum qrec_level level)
{
	if (!string)
		return NULL;

	return qrcode_encode_data_structured(strlen(string),
					     (unsigned char *)string, version,
					     level);
}
EXPORT_SYMBOL_GPL(qrcode_encode_string_8bit_structured);

struct qrcode_list
*qrcode_encode_string_structured(const char *string, int version,
				 enum qrec_level level, enum qrencode_mode hint,
				 int casesensitive)
{
	if (!string)
		return NULL;

	return qrcode_encode_data_structured_real(strlen(string),
						  (unsigned char *)string,
						  version,
						  level, 0,
						  hint, casesensitive);
}
EXPORT_SYMBOL_GPL(qrcode_encode_string_structured);
