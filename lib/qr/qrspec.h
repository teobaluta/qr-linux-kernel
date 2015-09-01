/*
 * qrencode - QR Code encoder
 *
 * QR Code specification in convenient format.
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

#ifndef __QRSPEC_H__
#define __QRSPEC_H__

#include <linux/qrencode.h>

/******************************************************************************
 * Version and capacity
 *****************************************************************************/

/**
 * Maximum width of a symbol
 */
#define QRSPEC_WIDTH_MAX 177

/**
 * Return maximum data code length (bytes) for the version.
 * @param version
 * @param level
 * @return maximum size (bytes)
 */
int qrspec_get_data_length(int version, enum qrec_level level);

/**
 * Return maximum error correction code length (bytes) for the version.
 * @param version
 * @param level
 * @return ECC size (bytes)
 */
int qrspec_get_ecc_length(int version, enum qrec_level level);

/**
 * Return a version number that satisfies the input code length.
 * @param size input code length (byte)
 * @param level
 * @return version number
 */
int qrspec_get_minimum_version(int size, enum qrec_level level);

/**
 * Return the width of the symbol for the version.
 * @param version
 * @return width
 */
int qrspec_get_width(int version);

/**
 * Return the numer of remainder bits.
 * @param version
 * @return number of remainder bits
 */
int qrspec_get_remainder(int version);

/******************************************************************************
 * Length indicator
 *****************************************************************************/

/**
 * Return the size of length indicator for the mode and version.
 * @param mode
 * @param version
 * @return the size of the appropriate length indicator (bits).
 */
int qrspec_length_indicator(enum qrencode_mode mode, int version);

/**
 * Return the maximum length for the mode and version.
 * @param mode
 * @param version
 * @return the maximum length (bytes)
 */
int qrspec_maximum_words(enum qrencode_mode mode, int version);

/******************************************************************************
 * Error correction code
 *****************************************************************************/

/**
 * Return an array of ECC specification.
 * @param version
 * @param level
 * @param spec an array of ECC specification contains as following:
 * {# of type1 blocks, # of data code, # of ecc code,
 *  # of type2 blocks, # of data code}
 */
void qrspec_get_ecc_spec(int version, enum qrec_level level, int spec[5]);

#define qrspec_rs_block_num(__spec__) (__spec__[0] + __spec__[3])
#define qrspec_rs_block_num1(__spec__) (__spec__[0])
#define qrspec_rs_data_codes1(__spec__) (__spec__[1])
#define qrspec_rs_ecc_codes1(__spec__) (__spec__[2])
#define qrspec_rs_block_num2(__spec__) (__spec__[3])
#define qrspec_rs_data_codes2(__spec__) (__spec__[4])
#define qrspec_rs_ecc_codes2(__spec__) (__spec__[2])

#define qrspec_rs_data_length(__spec__) \
	((qrspec_rs_block_num1(__spec__) * qrspec_rs_data_codes1(__spec__)) + \
	 (qrspec_rs_block_num2(__spec__) * qrspec_rs_data_codes2(__spec__)))
#define qrspec_rs_ecc_length(__spec__) \
	(qrspec_rs_block_num(__spec__) * qrspec_rs_ecc_codes1(__spec__))

/******************************************************************************
 * Version information pattern
 *****************************************************************************/

/**
 * Return BCH encoded version information pattern that is used for the symbol
 * of version 7 or greater. Use lower 18 bits.
 * @param version
 * @return BCH encoded version information pattern
 */
unsigned int qrspec_get_version_pattern(int version);

/******************************************************************************
 * Format information
 *****************************************************************************/

/**
 * Return BCH encoded format information pattern.
 * @param mask
 * @param level
 * @return BCH encoded format information pattern
 */
unsigned int qrspec_get_format_info(int mask, enum qrec_level level);

/******************************************************************************
 * Frame
 *****************************************************************************/

/**
 * Return a copy of initialized frame.
 * When the same version is requested twice or more, a copy of cached frame
 * is returned.
 * @param version
 * @return Array of unsigned char. You can free it by free().
 */
unsigned char *qrspec_new_frame(int version);

/**
 * Clear the frame cache. Typically for debug.
 */
void qrspec_clear_cache(void);

/******************************************************************************
 * Mode indicator
 *****************************************************************************/

/**
 * Mode indicator. See Table 2 of JIS X0510:2004, pp.16.
 */
#define QRSPEC_MODEID_ECI        7
#define QRSPEC_MODEID_NUM        1
#define QRSPEC_MODEID_AN         2
#define QRSPEC_MODEID_8          4
#define QRSPEC_MODEID_FNC1FIRST  5
#define QRSPEC_MODEID_FNC1SECOND 9
#define QRSPEC_MODEID_STRUCTURE  3
#define QRSPEC_MODEID_TERMINATOR 0

#endif /* __QRSPEC_H__ */
