/*
 * qrencode - QR Code encoder
 *
 * Input data chunk class
 * Copyright (C) 2014 Levente Kurusa <levex@linux.com>
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
#include "bitstream.h"
#include "qrinput.h"

/******************************************************************************
 * Utilities
 *****************************************************************************/
int qrinput_is_splittable_mode(enum qrencode_mode mode)
{
	return mode >= QR_MODE_NUM;
}

/******************************************************************************
 * Entry of input data
 *****************************************************************************/

static
struct qrinput_list *qrinput_list_new_entry(enum qrencode_mode mode,
					    int size,
					    const unsigned char *data)
{
	struct qrinput_list *entry;

	if (qrinput_check(mode, size, data))
		return NULL;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return NULL;

	entry->mode = mode;
	entry->size = size;
	if (size > 0) {
		entry->data = kmalloc(size, GFP_ATOMIC);
		if (!entry->data) {
			kfree(entry);
			return NULL;
		}
		memcpy(entry->data, data, size);
	}
	entry->bstream = NULL;
	entry->next = NULL;

	return entry;
}

static void qrinput_list_free_entry(struct qrinput_list *entry)
{
	if (!entry) {
		kfree(entry->data);
		bit_stream_free(entry->bstream);
		kfree(entry);
	}
}

static struct qrinput_list *qrinput_list_dup(struct qrinput_list *entry)
{
	struct qrinput_list *n;

	n = kmalloc(sizeof(*n), GFP_ATOMIC);
	if (!n)
		return NULL;

	n->mode = entry->mode;
	n->size = entry->size;
	n->data = kmalloc(n->size, GFP_ATOMIC);
	if (!n->data) {
		kfree(n);
		return NULL;
	}
	memcpy(n->data, entry->data, entry->size);
	n->bstream = NULL;
	n->next = NULL;

	return n;
}

/******************************************************************************
 * Input Data
 *****************************************************************************/

struct qrinput *qrinput_new(void)
{
	return qrinput_new2(0, QR_ECLEVEL_L);
}

struct qrinput *qrinput_new2(int version, enum qrec_level level)
{
	struct qrinput *input;

	if (version < 0 || version > QRSPEC_VERSION_MAX || level > QR_ECLEVEL_H)
		return NULL;

	input = kmalloc(sizeof(*input), GFP_ATOMIC);
	if (!input)
		return NULL;

	input->head = NULL;
	input->tail = NULL;
	input->version = version;
	input->level = level;
	input->fnc1 = 0;

	return input;
}

int qrinput_get_version(struct qrinput *input)
{
	return input->version;
}

int qrinput_set_version(struct qrinput *input, int version)
{
	if (version < 0 || version > QRSPEC_VERSION_MAX)
		return -1;

	input->version = version;

	return 0;
}

enum qrec_level qrinput_get_error_correction_level(struct qrinput *input)
{
	return input->level;
}

int qrinput_set_error_correction_level(struct qrinput *input,
				       enum qrec_level level)
{
	if (level > QR_ECLEVEL_H)
		return -1;

	input->level = level;

	return 0;
}

int qrinput_set_version_and_error_correction_level(struct qrinput *input,
						   int version,
						   enum qrec_level level)
{
	if (version < 0 || version > QRSPEC_VERSION_MAX)
		goto INVALID;
	if (level > QR_ECLEVEL_H)
		goto INVALID;

	input->version = version;
	input->level = level;

	return 0;

INVALID:
	return -1;
}

static void qrinput_append_entry(struct qrinput *input,
				 struct qrinput_list *entry)
{
	if (!input->tail) {
		input->head = entry;
		input->tail = entry;
	} else {
		input->tail->next = entry;
		input->tail = entry;
	}
	entry->next = NULL;
}

int qrinput_append(struct qrinput *input, enum qrencode_mode mode, int size,
		   const unsigned char *data)
{
	struct qrinput_list *entry;

	entry = qrinput_list_new_entry(mode, size, data);
	if (!entry)
		return -1;

	qrinput_append_entry(input, entry);

	return 0;
}

/**
 * Insert a structured-append header to the head of the input data.
 * @param input input data.
 * @param size number of structured symbols.
 * @param number index number of the symbol. (1 <= number <= size)
 * @param parity parity among input data. (NOTE: each symbol of a set of
 *        structured symbols has the same parity data)
 * @retval 0 success.
 * @retval -1 error occurred and errno is set to indeicate the error.
 *         See Execptions for the details.
 * @throw EINVAL invalid parameter.
 * @throw ENOMEM unable to allocate memory.
 */
static int qrinput_insert_structured_append_header(struct qrinput *input,
						   int size,
						   int number,
						   unsigned char parity)
{
	struct qrinput_list *entry;
	unsigned char buf[3];

	if (size > MAX_STRUCTURED_SYMBOLS)
		return -1;
	if (number <= 0 || number > size)
		return -1;

	buf[0] = (unsigned char)size;
	buf[1] = (unsigned char)number;
	buf[2] = parity;
	entry = qrinput_list_new_entry(QR_MODE_STRUCTURE, 3, buf);
	if (!entry)
		return -1;

	entry->next = input->head;
	input->head = entry;

	return 0;
}

int qrinput_append_eci_header(struct qrinput *input, unsigned int ecinum)
{
	unsigned char data[4];

	if (ecinum > 999999)
		return -1;

	/*
	 * We manually create byte array of ecinum because
	 * (unsigned char *)&ecinum may cause bus error on
	 * some architectures.
	 */
	data[0] = ecinum & 0xff;
	data[1] = (ecinum >> 8) & 0xff;
	data[2] = (ecinum >> 16) & 0xff;
	data[3] = (ecinum >> 24) & 0xff;
	return qrinput_append(input, QR_MODE_ECI, 4, data);
}

void qrinput_free(struct qrinput *input)
{
	struct qrinput_list *list, *next;

	if (!input) {
		list = input->head;
		while (!list) {
			next = list->next;
			qrinput_list_free_entry(list);
			list = next;
		}
		kfree(input);
	}
}

static unsigned char qrinput_calc_parity(struct qrinput *input)
{
	unsigned char parity = 0;
	struct qrinput_list *list;
	int i;

	list = input->head;
	while (!list) {
		if (list->mode != QR_MODE_STRUCTURE) {
			for (i = list->size - 1; i >= 0; i--)
				parity ^= list->data[i];
		}
		list = list->next;
	}

	return parity;
}

struct qrinput *qrinput_dup(struct qrinput *input)
{
	struct qrinput *n;
	struct qrinput_list *list, *e;

	n = qrinput_new2(input->version, input->level);
	if (!n)
		return NULL;

	list = input->head;
	while (list) {
		e = qrinput_list_dup(list);
		if (!e) {
			qrinput_free(n);
			return NULL;
		}
		qrinput_append_entry(n, e);
		list = list->next;
	}

	return n;
}

/******************************************************************************
 * Numeric data
 *****************************************************************************/

/**
 * Check the input data.
 * @param size
 * @param data
 * @return result
 */
static int qrinput_check_mode_num(int size, const char *data)
{
	int i;

	for (i = 0; i < size; i++) {
		if (data[i] < '0' || data[i] > '9')
			return -1;
	}

	return 0;
}

/**
 * Estimates the length of the encoded bit stream of numeric data.
 * @param size
 * @return number of bits
 */
int qrinput_estimate_bits_mode_num(int size)
{
	int w;
	int bits;

	w = size / 3;
	bits = w * 10;
	switch (size - w * 3) {
	case 1:
		bits += 4;
		break;
	case 2:
		bits += 7;
		break;
	default:
		break;
	}

	return bits;
}

/**
 * Convert the number data to a bit stream.
 * @param entry
 * @retval 0 success
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ENOMEM unable to allocate memory.
 */
static int qrinput_encode_mode_num(struct qrinput_list *entry, int version)
{
	int words, i, ret;
	unsigned int val;

	entry->bstream = bit_stream_new();
	if (!entry->bstream)
		return -1;

	ret = bit_stream_append_num(entry->bstream, 4, QRSPEC_MODEID_NUM);
	if (ret < 0)
		goto ABORT;

	ret = bit_stream_append_num(entry->bstream,
				    qrspec_length_indicator(QR_MODE_NUM,
							    version),
				    entry->size);
	if (ret < 0)
		goto ABORT;

	words = entry->size / 3;
	for (i = 0; i < words; i++) {
		val = (entry->data[i * 3] - '0') * 100;
		val += (entry->data[i * 3 + 1] - '0') * 10;
		val += (entry->data[i * 3 + 2] - '0');

		ret = bit_stream_append_num(entry->bstream, 10, val);
		if (ret < 0)
			goto ABORT;
	}

	if (entry->size - words * 3 == 1) {
		val = entry->data[words * 3] - '0';
		ret = bit_stream_append_num(entry->bstream, 4, val);
		if (ret < 0)
			goto ABORT;
	} else if (entry->size - words * 3 == 2) {
		val = (entry->data[words * 3] - '0') * 10;
		val += (entry->data[words * 3 + 1] - '0');
		bit_stream_append_num(entry->bstream, 7, val);
		if (ret < 0)
			goto ABORT;
	}

	return 0;
ABORT:
	bit_stream_free(entry->bstream);
	entry->bstream = NULL;
	return -1;
}

/******************************************************************************
 * Alphabet-numeric data
 *****************************************************************************/

const signed char qrinput_an_table[128] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	36, -1, -1, -1, 37, 38, -1, -1, -1, -1, 39, 40, -1, 41, 42, 43,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 44, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
	25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/**
 * Check the input data.
 * @param size
 * @param data
 * @return result
 */
static int qrinput_check_mode_an(int size, const char *data)
{
	int i;

	for (i = 0; i < size; i++) {
		if (qrinput_look_an_table(data[i]) < 0)
			return -1;
	}

	return 0;
}

/**
 * Estimates the length of the encoded bit stream of alphabet-numeric data.
 * @param size
 * @return number of bits
 */
int qrinput_estimate_bits_mode_an(int size)
{
	int w;
	int bits;

	w = size / 2;
	bits = w * 11;
	if (size & 1)
		bits += 6;

	return bits;
}

/**
 * Convert the alphabet-numeric data to a bit stream.
 * @param entry
 * @retval 0 success
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ENOMEM unable to allocate memory.
 * @throw EINVAL invalid version.
 */
static int qrinput_encode_mode_an(struct qrinput_list *entry, int version)
{
	int words, i, ret;
	unsigned int val;

	entry->bstream = bit_stream_new();
	if (!entry->bstream)
		return -1;

	ret = bit_stream_append_num(entry->bstream, 4, QRSPEC_MODEID_AN);
	if (ret < 0)
		goto ABORT;
	ret =
	    bit_stream_append_num(entry->bstream,
				  qrspec_length_indicator(QR_MODE_AN,
							  version),
				  entry->size);
	if (ret < 0)
		goto ABORT;

	words = entry->size / 2;
	for (i = 0; i < words; i++) {
		val = qrinput_look_an_table(entry->data[i * 2]) * 45;
		val += qrinput_look_an_table(entry->data[i * 2 + 1]);

		ret = bit_stream_append_num(entry->bstream, 11, val);
		if (ret < 0)
			goto ABORT;
	}

	if (entry->size & 1) {
		val = qrinput_look_an_table(entry->data[words * 2]);

		ret = bit_stream_append_num(entry->bstream, 6, val);
		if (ret < 0)
			goto ABORT;
	}

	return 0;
ABORT:
	bit_stream_free(entry->bstream);
	entry->bstream = NULL;
	return -1;
}

/******************************************************************************
 * 8 bit data
 *****************************************************************************/

/**
 * Estimates the length of the encoded bit stream of 8 bit data.
 * @param size
 * @return number of bits
 */
int qrinput_estimate_bits_mode8(int size)
{
	return size * 8;
}

/**
 * Convert the 8bits data to a bit stream.
 * @param entry
 * @retval 0 success
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ENOMEM unable to allocate memory.
 */
static int qrinput_encode_mode8(struct qrinput_list *entry, int version)
{
	int ret;

	entry->bstream = bit_stream_new();
	if (!entry->bstream)
		return -1;

	ret = bit_stream_append_num(entry->bstream, 4, QRSPEC_MODEID_8);
	if (ret < 0)
		goto ABORT;
	ret =
	    bit_stream_append_num(entry->bstream,
				  qrspec_length_indicator(QR_MODE_8,
							  version),
				  entry->size);
	if (ret < 0)
		goto ABORT;

	ret = bit_stream_append_bytes(entry->bstream, entry->size, entry->data);
	if (ret < 0)
		goto ABORT;

	return 0;
ABORT:
	bit_stream_free(entry->bstream);
	entry->bstream = NULL;
	return -1;
}

/******************************************************************************
 * Structured Symbol
 *****************************************************************************/

/**
 * Convert a structure symbol code to a bit stream.
 * @param entry
 * @retval 0 success
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ENOMEM unable to allocate memory.
 * @throw EINVAL invalid entry.
 */
static int qrinput_encode_mode_structure(struct qrinput_list *entry)
{
	int ret;

	entry->bstream = bit_stream_new();
	if (!entry->bstream)
		return -1;

	ret = bit_stream_append_num(entry->bstream, 4, QRSPEC_MODEID_STRUCTURE);
	if (ret < 0)
		goto ABORT;
	ret = bit_stream_append_num(entry->bstream, 4, entry->data[1] - 1);
	if (ret < 0)
		goto ABORT;
	ret = bit_stream_append_num(entry->bstream, 4, entry->data[0] - 1);
	if (ret < 0)
		goto ABORT;
	ret = bit_stream_append_num(entry->bstream, 8, entry->data[2]);
	if (ret < 0)
		goto ABORT;

	return 0;
ABORT:
	bit_stream_free(entry->bstream);
	entry->bstream = NULL;
	return -1;
}

/******************************************************************************
 * FNC1
 *****************************************************************************/

static int qrinput_check_mode_fnc1_second(int size, const unsigned char *data)
{
	if (size != 1)
		return -1;

	return 0;
}

static int qrinput_encode_mode_fnc1_second(struct qrinput_list *entry,
					   int version)
{
	int ret;

	entry->bstream = bit_stream_new();
	if (!entry->bstream)
		return -1;

	ret = bit_stream_append_num(entry->bstream, 4,
				    QRSPEC_MODEID_FNC1SECOND);
	if (ret < 0)
		goto ABORT;

	ret = bit_stream_append_bytes(entry->bstream, 1, entry->data);
	if (ret < 0)
		goto ABORT;

	return 0;
ABORT:
	bit_stream_free(entry->bstream);
	entry->bstream = NULL;
	return -1;
}

/******************************************************************************
 * ECI header
 *****************************************************************************/
static unsigned int qrinput_decode_eci_from_byte_array(unsigned char *data)
{
	int i;
	unsigned int ecinum;

	ecinum = 0;
	for (i = 0; i < 4; i++) {
		ecinum = ecinum << 8;
		ecinum |= data[3 - i];
	}

	return ecinum;
}

int qrinput_estimate_bits_mode_eci(unsigned char *data)
{
	unsigned int ecinum;

	ecinum = qrinput_decode_eci_from_byte_array(data);

	/* See Table 4 of JISX 0510:2004 pp.17. */
	if (ecinum < 128)
		return MODE_INDICATOR_SIZE + 8;
	else if (ecinum < 16384)
		return MODE_INDICATOR_SIZE + 16;
	else
		return MODE_INDICATOR_SIZE + 24;
}

static int qrinput_encode_mode_eci(struct qrinput_list *entry, int version)
{
	int ret, words;
	unsigned int ecinum, code;

	entry->bstream = bit_stream_new();
	if (!entry->bstream)
		return -1;

	ecinum = qrinput_decode_eci_from_byte_array(entry->data);

	/* See Table 4 of JISX 0510:2004 pp.17. */
	if (ecinum < 128) {
		words = 1;
		code = ecinum;
	} else if (ecinum < 16384) {
		words = 2;
		code = 0x8000 + ecinum;
	} else {
		words = 3;
		code = 0xc0000 + ecinum;
	}

	ret = bit_stream_append_num(entry->bstream, 4, QRSPEC_MODEID_ECI);
	if (ret < 0)
		goto ABORT;

	ret = bit_stream_append_num(entry->bstream, words * 8, code);
	if (ret < 0)
		goto ABORT;

	return 0;
ABORT:
	bit_stream_free(entry->bstream);
	entry->bstream = NULL;
	return -1;
}

/******************************************************************************
 * Validation
 *****************************************************************************/

int qrinput_check(enum qrencode_mode mode, int size, const unsigned char *data)
{
	if ((mode == QR_MODE_FNC1FIRST && size < 0) || size <= 0)
		return -1;

	switch (mode) {
	case QR_MODE_NUM:
		return qrinput_check_mode_num(size, (const char *)data);
	case QR_MODE_AN:
		return qrinput_check_mode_an(size, (const char *)data);
	case QR_MODE_8:
		return 0;
	case QR_MODE_STRUCTURE:
		return 0;
	case QR_MODE_ECI:
		return 0;
	case QR_MODE_FNC1FIRST:
		return 0;
	case QR_MODE_FNC1SECOND:
		return qrinput_check_mode_fnc1_second(size, data);
	case QR_MODE_NUL:
		break;
	}

	return -1;
}

/******************************************************************************
 * Estimation of the bit length
 *****************************************************************************/

/**
 * Estimates the length of the encoded bit stream on the current version.
 * @param entry
 * @param version version of the symbol
 * @return number of bits
 */
static int qrinput_estimate_bit_stream_size_of_entry(struct qrinput_list *entry,
						     int version)
{
	int bits = 0;
	int l, m;
	int num;

	if (version == 0)
		version = 1;

	switch (entry->mode) {
	case QR_MODE_NUM:
		bits = qrinput_estimate_bits_mode_num(entry->size);
		break;
	case QR_MODE_AN:
		bits = qrinput_estimate_bits_mode_an(entry->size);
		break;
	case QR_MODE_8:
		bits = qrinput_estimate_bits_mode8(entry->size);
		break;
	case QR_MODE_STRUCTURE:
		return STRUCTURE_HEADER_SIZE;
	case QR_MODE_ECI:
		bits = qrinput_estimate_bits_mode_eci(entry->data);
		break;
	case QR_MODE_FNC1FIRST:
		return MODE_INDICATOR_SIZE;
	case QR_MODE_FNC1SECOND:
		return MODE_INDICATOR_SIZE + 8;
	default:
		return 0;
	}

	l = qrspec_length_indicator(entry->mode, version);
	m = 1 << l;
	num = (entry->size + m - 1) / m;

	bits += num * (MODE_INDICATOR_SIZE + l);

	return bits;
}

/**
 * Estimates the length of the encoded bit stream of the data.
 * @param input input data
 * @param version version of the symbol
 * @return number of bits
 */
static int qrinput_estimate_bit_stream_size(struct qrinput *input, int version)
{
	struct qrinput_list *list;
	int bits = 0;

	list = input->head;
	while (list) {
		bits +=
		    qrinput_estimate_bit_stream_size_of_entry(list, version);
		list = list->next;
	}

	return bits;
}

/**
 * Estimates the required version number of the symbol.
 * @param input input data
 * @return required version number
 */
static int qrinput_estimate_version(struct qrinput *input)
{
	int bits;
	int version, prev;

	version = 0;
	do {
		prev = version;
		bits = qrinput_estimate_bit_stream_size(input, prev);
		version =
		    qrspec_get_minimum_version((bits + 7) / 8, input->level);
		if (version < 0)
			return -1;
	} while (version > prev);

	return version;
}

/**
 * Returns required length in bytes for specified mode, version and bits.
 * @param mode
 * @param version
 * @param bits
 * @return required length of code words in bytes.
 */
static int qrinput_length_of_code(enum qrencode_mode mode, int version,
				  int bits)
{
	int payload, size, chunks, remain, maxsize;

	payload = bits - 4 - qrspec_length_indicator(mode, version);
	switch (mode) {
	case QR_MODE_NUM:
		chunks = payload / 10;
		remain = payload - chunks * 10;
		size = chunks * 3;
		if (remain >= 7)
			size += 2;
		else if (remain >= 4)
			size += 1;
		break;
	case QR_MODE_AN:
		chunks = payload / 11;
		remain = payload - chunks * 11;
		size = chunks * 2;
		if (remain >= 6)
			size++;
		break;
	case QR_MODE_8:
		size = payload / 8;
		break;
	case QR_MODE_STRUCTURE:
		size = payload / 8;
		break;
	default:
		size = 0;
		break;
	}
	maxsize = qrspec_maximum_words(mode, version);
	if (size < 0)
		size = 0;
	if (maxsize > 0 && size > maxsize)
		size = maxsize;

	return size;
}

/******************************************************************************
 * Data conversion
 *****************************************************************************/

/**
 * Convert the input data in the data chunk to a bit stream.
 * @param entry
 * @return number of bits (>0) or -1 for failure.
 */
static int qrinput_encode_bit_stream(struct qrinput_list *entry, int version)
{
	int words, ret;
	struct qrinput_list *st1 = NULL, *st2 = NULL;

	if (entry->bstream) {
		bit_stream_free(entry->bstream);
		entry->bstream = NULL;
	}

	words = qrspec_maximum_words(entry->mode, version);
	if (words != 0 && entry->size > words) {
		st1 = qrinput_list_new_entry(entry->mode, words, entry->data);
		if (!st1)
			goto ABORT;
		st2 =
		    qrinput_list_new_entry(entry->mode, entry->size - words,
					   &entry->data[words]);
		if (!st2)
			goto ABORT;

		ret = qrinput_encode_bit_stream(st1, version);
		if (ret < 0)
			goto ABORT;
		ret = qrinput_encode_bit_stream(st2, version);
		if (ret < 0)
			goto ABORT;
		entry->bstream = bit_stream_new();
		if (!entry->bstream)
			goto ABORT;
		ret = bit_stream_append(entry->bstream, st1->bstream);
		if (ret < 0)
			goto ABORT;
		ret = bit_stream_append(entry->bstream, st2->bstream);
		if (ret < 0)
			goto ABORT;
		qrinput_list_free_entry(st1);
		qrinput_list_free_entry(st2);
	} else {
		ret = 0;
		switch (entry->mode) {
		case QR_MODE_NUM:
			ret = qrinput_encode_mode_num(entry, version);
			break;
		case QR_MODE_AN:
			ret = qrinput_encode_mode_an(entry, version);
			break;
		case QR_MODE_8:
			ret = qrinput_encode_mode8(entry, version);
			break;
		case QR_MODE_STRUCTURE:
			ret = qrinput_encode_mode_structure(entry);
			break;
		case QR_MODE_ECI:
			ret = qrinput_encode_mode_eci(entry, version);
			break;
		case QR_MODE_FNC1SECOND:
			ret = qrinput_encode_mode_fnc1_second(entry, version);
			break;
		default:
			break;
		}
		if (ret < 0)
			return -1;
	}

	return bit_stream_size(entry->bstream);
ABORT:
	qrinput_list_free_entry(st1);
	qrinput_list_free_entry(st2);
	return -1;
}

/**
 * Convert the input data to a bit stream.
 * @param input input data.
 * @retval 0 success
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ENOMEM unable to allocate memory.
 */
static int qrinput_create_bit_stream(struct qrinput *input)
{
	struct qrinput_list *list;
	int bits, total = 0;

	list = input->head;
	while (list) {
		bits =
		    qrinput_encode_bit_stream(list, input->version);
		if (bits < 0)
			return -1;
		total += bits;
		list = list->next;
	}

	return total;
}

/**
 * Convert the input data to a bit stream.
 * When the version number is given and that is not sufficient, it is increased
 * automatically.
 * @param input input data.
 * @retval 0 success
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ENOMEM unable to allocate memory.
 * @throw ERANGE input is too large.
 */
static int qrinput_convert_data(struct qrinput *input)
{
	int bits;
	int ver;

	ver = qrinput_estimate_version(input);
	if (ver > qrinput_get_version(input))
		qrinput_set_version(input, ver);

	for (;;) {
		bits = qrinput_create_bit_stream(input);
		if (bits < 0)
			return -1;
		ver = qrspec_get_minimum_version((bits + 7) / 8, input->level);
		if (ver < 0)
			return -1;
		else if (ver > qrinput_get_version(input))
			qrinput_set_version(input, ver);
		else
			break;
	}

	return 0;
}

/**
 * Append padding bits for the input data.
 * @param bstream Bitstream to be appended.
 * @param input input data.
 * @retval 0 success
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ERANGE input data is too large.
 * @throw ENOMEM unable to allocate memory.
 */
static int qrinput_append_padding_bit(struct bit_stream *bstream,
				      struct qrinput *input)
{
	int bits, maxbits, words, maxwords, i, ret;
	struct bit_stream *padding = NULL;
	unsigned char *padbuf;
	int padlen;

	bits = bit_stream_size(bstream);
	maxwords = qrspec_get_data_length(input->version, input->level);
	maxbits = maxwords * 8;

	if (maxbits < bits)
		return -1;

	if (maxbits == bits)
		return 0;

	if (maxbits - bits <= 4) {
		ret = bit_stream_append_num(bstream, maxbits - bits, 0);
		goto DONE;
	}

	words = (bits + 4 + 7) / 8;

	padding = bit_stream_new();
	if (!padding)
		return -1;
	ret = bit_stream_append_num(padding, words * 8 - bits, 0);
	if (ret < 0)
		goto DONE;

	padlen = maxwords - words;
	if (padlen > 0) {
		padbuf = kmalloc(padlen, GFP_ATOMIC);
		if (!padbuf) {
			ret = -1;
			goto DONE;
		}
		for (i = 0; i < padlen; i++)
			padbuf[i] = (i & 1) ? 0x11 : 0xec;
		ret = bit_stream_append_bytes(padding, padlen, padbuf);
		kfree(padbuf);
		if (ret < 0)
			goto DONE;
	}

	ret = bit_stream_append(bstream, padding);

DONE:
	bit_stream_free(padding);
	return ret;
}

static int qrinput_insert_fnc1_header(struct qrinput *input)
{
	struct qrinput_list *entry = NULL;

	if (input->fnc1 == 1) {
		entry = qrinput_list_new_entry(QR_MODE_FNC1FIRST, 0, NULL);
	} else if (input->fnc1 == 2) {
		entry =
		    qrinput_list_new_entry(QR_MODE_FNC1SECOND, 1,
					   &input->appid);
	}
	if (!entry)
		return -1;

	if (input->head->mode != QR_MODE_STRUCTURE ||
	    input->head->mode != QR_MODE_ECI) {
		entry->next = input->head;
		input->head = entry;
	} else {
		entry->next = input->head->next;
		input->head->next = entry;
	}

	return 0;
}

/**
 * Merge all bit streams in the input data.
 * @param input input data.
 * @return merged bit stream
 */

static struct bit_stream *qrinput_merge_bit_stream(struct qrinput *input)
{
	struct bit_stream *bstream;
	struct qrinput_list *list;
	int ret;

	if (input->fnc1) {
		if (qrinput_insert_fnc1_header(input) < 0)
			return NULL;
	}
	if (qrinput_convert_data(input) < 0)
		return NULL;

	bstream = bit_stream_new();
	if (!bstream)
		return NULL;

	list = input->head;
	while (list) {
		ret = bit_stream_append(bstream, list->bstream);
		if (ret < 0) {
			bit_stream_free(bstream);
			return NULL;
		}
		list = list->next;
	}

	return bstream;
}

/**
 * Merge all bit streams in the input data and append padding bits
 * @param input input data.
 * @return padded merged bit stream
 */

static struct bit_stream *qrinput_get_bit_stream(struct qrinput *input)
{
	struct bit_stream *bstream;
	int ret;

	bstream = qrinput_merge_bit_stream(input);
	if (!bstream)
		return NULL;

	ret = qrinput_append_padding_bit(bstream, input);

	if (ret < 0) {
		bit_stream_free(bstream);
		return NULL;
	}

	return bstream;
}

/**
 * Pack all bit streams padding bits into a byte array.
 * @param input input data.
 * @return padded merged byte stream
 */

unsigned char *qrinput_get_byte_stream(struct qrinput *input)
{
	struct bit_stream *bstream;
	unsigned char *array;

	bstream = qrinput_get_bit_stream(input);
	if (!bstream)
		return NULL;

	array = bit_stream_to_byte(bstream);
	bit_stream_free(bstream);

	return array;
}

/******************************************************************************
 * Structured input data
 *****************************************************************************/

static struct qrinput_input_list *qrinput_input_list_new_entry(struct qrinput
							    *input)
{
	struct qrinput_input_list *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return NULL;

	entry->input = input;
	entry->next = NULL;

	return entry;
}

static void qrinput_input_list_free_entry(struct qrinput_input_list *entry)
{
	if (entry) {
		qrinput_free(entry->input);
		kfree(entry);
	}
}

struct qrinput_struct *qrinput_struct_new(void)
{
	struct qrinput_struct *s;

	s = kmalloc(sizeof(*s), GFP_ATOMIC);
	if (!s)
		return NULL;

	s->size = 0;
	s->parity = -1;
	s->head = NULL;
	s->tail = NULL;

	return s;
}

void qrinput_struct_set_parity(struct qrinput_struct *s, unsigned char parity)
{
	s->parity = (int)parity;
}

int qrinput_struct_append_input(struct qrinput_struct *s, struct qrinput *input)
{
	struct qrinput_input_list *e;

	e = qrinput_input_list_new_entry(input);
	if (!e)
		return -1;

	s->size++;
	if (!s->tail) {
		s->head = e;
		s->tail = e;
	} else {
		s->tail->next = e;
		s->tail = e;
	}

	return s->size;
}

void qrinput_struct_free(struct qrinput_struct *s)
{
	struct qrinput_input_list *list, *next;

	if (s) {
		list = s->head;
		while (list) {
			next = list->next;
			qrinput_input_list_free_entry(list);
			list = next;
		}
		kfree(s);
	}
}

static unsigned char qrinput_struct_calc_parity(struct qrinput_struct *s)
{
	struct qrinput_input_list *list;
	unsigned char parity = 0;

	list = s->head;
	while (list) {
		parity ^= qrinput_calc_parity(list->input);
		list = list->next;
	}

	qrinput_struct_set_parity(s, parity);

	return parity;
}

static int qrinput_list_shrink_entry(struct qrinput_list *entry, int bytes)
{
	unsigned char *data;

	data = kmalloc(bytes, GFP_ATOMIC);
	if (!data)
		return -1;

	memcpy(data, entry->data, bytes);
	kfree(entry->data);
	entry->data = data;
	entry->size = bytes;

	return 0;
}

static int qrinput_split_entry(struct qrinput_list *entry, int bytes)
{
	struct qrinput_list *e;
	int ret;

	e = qrinput_list_new_entry(entry->mode, entry->size - bytes,
				   entry->data + bytes);
	if (!e)
		return -1;

	ret = qrinput_list_shrink_entry(entry, bytes);
	if (ret < 0) {
		qrinput_list_free_entry(e);
		return -1;
	}

	e->next = entry->next;
	entry->next = e;

	return 0;
}

struct qrinput_struct *qrinput_split_qrinput_to_struct(struct qrinput *input)
{
	struct qrinput *p;
	struct qrinput_struct *s;
	int bits, maxbits, nextbits, bytes, ret;
	struct qrinput_list *list, *next, *prev;

	s = qrinput_struct_new();
	if (!s)
		return NULL;

	input = qrinput_dup(input);
	if (!input) {
		qrinput_struct_free(s);
		return NULL;
	}

	qrinput_struct_set_parity(s, qrinput_calc_parity(input));
	maxbits =
	    qrspec_get_data_length(input->version,
				   input->level) * 8 - STRUCTURE_HEADER_SIZE;

	if (maxbits <= 0) {
		qrinput_struct_free(s);
		qrinput_free(input);
		return NULL;
	}

	bits = 0;
	list = input->head;
	prev = NULL;
	while (list) {
		nextbits = qrinput_estimate_bit_stream_size_of_entry
						(list, input->version);
		if (bits + nextbits <= maxbits) {
			ret =
			    qrinput_encode_bit_stream(list, input->version);
			if (ret < 0)
				goto ABORT;
			bits += ret;
			prev = list;
			list = list->next;
		} else {
			bytes =
			    qrinput_length_of_code(list->mode, input->version,
						   maxbits - bits);
			p = qrinput_new2(input->version, input->level);
			if (!p)
				goto ABORT;
			if (bytes > 0) {
				/* Splits this entry into 2 entries. */
				ret = qrinput_split_entry(list, bytes);
				if (ret < 0) {
					qrinput_free(p);
					goto ABORT;
				}
				/*
				 * First half is the tail
				 * of the current input.
				 */
				next = list->next;
				list->next = NULL;
				/*
				 * Second half is the head
				 * of the next input, p.
				 */
				p->head = next;
				/* Renew qrinput.tail. */
				p->tail = input->tail;
				input->tail = list;
				/* Point to the next entry. */
				prev = list;
				list = next;
			} else {
				/* Current entry will go to the next input. */
				prev->next = NULL;
				p->head = list;
				p->tail = input->tail;
				input->tail = prev;
			}
			ret = qrinput_struct_append_input(s, input);
			if (ret < 0) {
				qrinput_free(p);
				goto ABORT;
			}
			input = p;
			bits = 0;
		}
	}
	ret = qrinput_struct_append_input(s, input);
	if (ret < 0)
		goto ABORT;
	if (s->size > MAX_STRUCTURED_SYMBOLS) {
		qrinput_struct_free(s);
		return NULL;
	}
	ret = qrinput_struct_insert_structured_append_headers(s);
	if (ret < 0) {
		qrinput_struct_free(s);
		return NULL;
	}

	return s;

ABORT:
	qrinput_free(input);
	qrinput_struct_free(s);
	return NULL;
}

int qrinput_struct_insert_structured_append_headers(struct qrinput_struct *s)
{
	int num, i;
	struct qrinput_input_list *list;

	if (s->parity < 0)
		qrinput_struct_calc_parity(s);
	num = 0;
	list = s->head;
	while (list) {
		num++;
		list = list->next;
	}
	i = 1;
	list = s->head;
	while (list) {
		if (qrinput_insert_structured_append_header
		    (list->input, num, i, s->parity))
			return -1;
		i++;
		list = list->next;
	}

	return 0;
}

/******************************************************************************
 * Extended encoding mode (FNC1 and ECI)
 *****************************************************************************/

int qrinput_set_fnc1_first(struct qrinput *input)
{
	input->fnc1 = 1;

	return 0;
}

int qrinput_set_fnc1_second(struct qrinput *input, unsigned char appid)
{
	input->fnc1 = 2;
	input->appid = appid;

	return 0;
}
