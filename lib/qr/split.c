/*
 * qrencode - QR Code encoder
 *
 * Input data splitter.
 * Copyright (C) 2006-2011 Kentaro Fukuchi <kentaro@fukuchi.org>
 *
 * The following data / specifications are taken from
 * "Two dimensional symbol -- QR-code -- Basic Specification" (JIS X0510:2004)
 *  or
 * "Automatic identification and data capture techniques --
 *  QR Code 2005 bar code symbology specification" (ISO/IEC 18004:2006)
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
#include "qrinput.h"
#include "qrspec.h"
#include "split.h"

#define isdigit(__c__) ((unsigned char)((signed char)(__c__) - '0') < 10)
#define isalnum(__c__) (qrinput_look_an_table(__c__) >= 0)

static enum qrencode_mode split_identify_mode(const char *string,
					      enum qrencode_mode hint)
{
	unsigned char c;

	c = string[0];

	if (c == '\0')
		return QR_MODE_NUL;
	if (isdigit(c))
		return QR_MODE_NUM;
	else if (isalnum(c))
		return QR_MODE_AN;
	return QR_MODE_8;
}

static int split_eat_num(const char *string, struct qrinput *input,
			 enum qrencode_mode hint);

static int split_eat_an(const char *string, struct qrinput *input,
			enum qrencode_mode hint);

static int split_eat8(const char *string, struct qrinput *input,
		      enum qrencode_mode hint);

static int split_eat_num(const char *string, struct qrinput *input,
			 enum qrencode_mode hint)
{
	const char *p;
	int ret;
	int run;
	int dif;
	int ln;
	enum qrencode_mode mode;

	ln = qrspec_length_indicator(QR_MODE_NUM, input->version);

	p = string;
	while (isdigit(*p))
		p++;
	run = p - string;
	mode = split_identify_mode(p, hint);
	if (mode == QR_MODE_8) {
		dif = qrinput_estimate_bits_mode_num(run) + 4 + ln
			+ qrinput_estimate_bits_mode8(1)	/* + 4 + l8 */
			- qrinput_estimate_bits_mode8(run + 1); /* - 4 - l8 */
		if (dif > 0)
			return split_eat8(string, input, hint);
	}
	if (mode == QR_MODE_AN) {
		dif = qrinput_estimate_bits_mode_num(run) + 4 + ln
			+ qrinput_estimate_bits_mode_an(1)	/* + 4 + la */
			- qrinput_estimate_bits_mode_an(run + 1); /* - 4 - la */
		if (dif > 0)
			return split_eat_an(string, input, hint);
	}

	ret = qrinput_append(input, QR_MODE_NUM, run, (unsigned char *)string);
	if (ret < 0)
		return -1;

	return run;
}

static int split_eat_an(const char *string, struct qrinput *input,
			enum qrencode_mode hint)
{
	const char *p, *q;
	int ret;
	int run;
	int dif;
	int la, ln;

	la = qrspec_length_indicator(QR_MODE_AN, input->version);
	ln = qrspec_length_indicator(QR_MODE_NUM, input->version);

	p = string;
	while (isalnum(*p)) {
		if (isdigit(*p)) {
			q = p;
			while (isdigit(*q))
				q++;
			dif = qrinput_estimate_bits_mode_an(p - string)
				/* + 4 + la */
				+ qrinput_estimate_bits_mode_num(q - p) + 4 + ln
				+ (isalnum(*q) ? (4 + ln) : 0)
				- qrinput_estimate_bits_mode_an(q - string)
				/* - 4 - la */
			    ;
			if (dif >= 0)
				p = q;
			else
				break;
		} else {
			p++;
		}
	}

	run = p - string;

	if (*p && !isalnum(*p)) {
		dif = qrinput_estimate_bits_mode_an(run) + 4 + la
			+ qrinput_estimate_bits_mode8(1) /* + 4 + l8 */
			- qrinput_estimate_bits_mode8(run + 1); /* - 4 - l8 */
		if (dif > 0)
			return split_eat8(string, input, hint);
	}

	ret = qrinput_append(input, QR_MODE_AN, run, (unsigned char *)string);
	if (ret < 0)
		return -1;

	return run;
}

static int split_eat8(const char *string, struct qrinput *input,
		      enum qrencode_mode hint)
{
	const char *p, *q;
	enum qrencode_mode mode;
	int ret;
	int run;
	int dif;
	int la, ln, l8;
	int swcost;

	la = qrspec_length_indicator(QR_MODE_AN, input->version);
	ln = qrspec_length_indicator(QR_MODE_NUM, input->version);
	l8 = qrspec_length_indicator(QR_MODE_8, input->version);

	p = string + 1;
	while (*p != '\0') {
		mode = split_identify_mode(p, hint);
		if (mode == QR_MODE_NUM) {
			q = p;
			while (isdigit(*q))
				q++;

			if (split_identify_mode(q, hint) == QR_MODE_8)
				swcost = 4 + l8;
			else
				swcost = 0;
			dif = qrinput_estimate_bits_mode8(p - string)
				/* + 4 + l8 */
				+ qrinput_estimate_bits_mode_num(q - p) + 4 + ln
				+ swcost
				- qrinput_estimate_bits_mode8(q - string);
				/* - 4 - l8 */
			if (dif >= 0)
				p = q;
			else
				break;
		} else if (mode == QR_MODE_AN) {
			q = p;
			while (isalnum(*q))
				q++;
			if (split_identify_mode(q, hint) == QR_MODE_8)
				swcost = 4 + l8;
			else
				swcost = 0;
			dif = qrinput_estimate_bits_mode8(p - string)
				/* + 4 + l8 */
				+ qrinput_estimate_bits_mode_an(q - p) + 4 + la
				+ swcost
				- qrinput_estimate_bits_mode8(q - string);
				/* - 4 - l8 */
			if (dif >= 0)
				p = q;
			else
				break;

		} else {
			p++;
		}
	}

	run = p - string;
	ret = qrinput_append(input, QR_MODE_8, run, (unsigned char *)string);
	if (ret < 0)
		return -1;

	return run;
}

static int split_split_string(const char *string, struct qrinput *input,
			      enum qrencode_mode hint)
{
	int length;
	enum qrencode_mode mode;

	if (*string == '\0')
		return 0;

	mode = split_identify_mode(string, hint);
	if (mode == QR_MODE_NUM)
		length = split_eat_num(string, input, hint);
	else if (mode == QR_MODE_AN)
		length = split_eat_an(string, input, hint);
	else
		length = split_eat8(string, input, hint);
	if (length == 0)
		return 0;
	if (length < 0)
		return -1;
	return split_split_string(&string[length], input, hint);
}

static char *dup_and_to_upper(const char *str, enum qrencode_mode hint)
{
	char *newstr, *p;
	enum qrencode_mode mode;

	newstr = kstrdup(str, GFP_ATOMIC);
	if (!newstr)
		return NULL;

	p = newstr;
	while (*p != '\0') {
		mode = split_identify_mode(p, hint);
		if (*p >= 'a' && *p <= 'z')
			*p = (char)((int)*p - 32);
		p++;
	}

	return newstr;
}

int split_split_string_to_qrinput(const char *string, struct qrinput *input,
				  enum qrencode_mode hint, int casesensitive)
{
	char *newstr;
	int ret;

	if (!string || *string == '\0')
		return -1;
	if (!casesensitive) {
		newstr = dup_and_to_upper(string, hint);
		if (!newstr)
			return -1;
		ret = split_split_string(newstr, input, hint);
		kfree(newstr);
	} else {
		ret = split_split_string(string, input, hint);
	}

	return ret;
}
