/**
 * qrencode - QR Code encoder
 *
 * Copyright (C) 2006-2012 Kentaro Fukuchi <kentaro@fukuchi.org>
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
/** \mainpage
 * Libqrencode is a library for encoding data in a QR Code symbol, a kind of 2D
 * symbology.
 *
 * \section encoding Encoding
 *
 * There are two methods to encode data: <b>encoding a string/data</b> or
 * <b>encoding a structured data</b>.
 *
 * \subsection encoding-string Encoding a string/data
 * You can encode a string by calling qrcode_encode_string().
 * The given string is parsed automatically and encoded. If you want to encode
 * data that can be represented as a C string style (NUL terminated), you can
 * simply use this way.
 *
 * If the input data contains Kanji (Shift-JIS) characters and you want to
 * encode them as Kanji in QR Code, you should give QR_MODE_KANJI as a hint.
 * Otherwise, all of non-alphanumeric characters are encoded as 8 bit data.
 * If you want to encode a whole string in 8 bit mode, you can use
 * qrcode_encode_string_8bit() instead.
 *
 * Please note that a C string can not contain NUL characters. If your data
 * contains NUL, you must use qrcode_encode_data().
 *
 * \subsection encoding-input Encoding a structured data
 * You can construct a structured input data manually. If the structure of the
 * input data is known, you can use this way.
 * At first, create a ::qrinput object by qrinput_new(). Then add input data
 * to the qrinput object by qrinput_append(). Finally call qrcode_encode_input()
 * to encode the qrinput data.
 * You can reuse the qrinput data again to encode it in other symbols with
 * different parameters.
 *
 * \section result Result
 * The encoded symbol is resulted as a ::qrcode object. It will contain
 * its version number, width of the symbol and an array represents the symbol.
 * See ::qrcode for the details. You can free the object by qrcode_free().
 *
 * Please note that the version of the result may be larger than specified.
 * In such cases, the input data would be too large to be encoded in a
 * symbol of the specified version.
 *
 * \section structured Structured append
 * Libqrencode can generate "Structured-appended" symbols that enables to split
 * a large data set into mulitple QR codes. A QR code reader concatenates
 * multiple QR code symbols into a string.
 * Just like qrcode_encode_string(), you can use
 * qrcode_encode_string_structured() to generate structured-appended symbols.
 * This functions returns an instance of ::qrcode_list. The returned list is a
 * singly-linked list of qrcode: you
 * can retrieve each QR code in this way:
 *
 * \code
 * qrcode_list *qrcodes;
 * qrcode_list *entry;
 * qrcode *qrcode;
 *
 * qrcodes = qrcode_encode_string_structured(...);
 * entry = qrcodes;
 * while(entry != NULL) {
 *     qrcode = entry->code;
 *     // do something
 *     entry = entry->next;
 * }
 * qrcode_list_free(entry);
 * \endcode
 *
 * Instead of using auto-parsing functions, you can construct your own
 * structured input. At first, instantiate an object of ::qrinput_struct
 * by calling qrinput_struct_new(). This object can hold multiple ::qrinput,
 * and one QR code is generated for a ::qrinput.
 * qrinput_struct_append_input() appends a ::qrinput to a ::qrinput_struct
 * object. In order to generate structured-appended symbols, it is required to
 * embed headers to each symbol. You can use
 * qrinput_struct_insert_structured_append_headers() to insert appropriate
 * headers to each symbol. You should call this function just once before
 * encoding symbols.
 */

#ifndef __QRENCODE_H__
#define __QRENCODE_H__

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Encoding mode.
 */
enum qrencode_mode {
	QR_MODE_NUL = -1,   /* Terminator (NUL character). Internal use only */
	QR_MODE_NUM = 0,    /* Numeric mode */
	QR_MODE_AN,         /* Alphabet-numeric mode */
	QR_MODE_8,          /* 8-bit data mode */
	QR_MODE_STRUCTURE,  /* Internal use only */
	QR_MODE_ECI,        /* ECI mode */
	QR_MODE_FNC1FIRST,  /* FNC1, first position */
	QR_MODE_FNC1SECOND, /* FNC1, second position */
};

/**
 * Level of error correction.
 */
enum qrec_level {
	QR_ECLEVEL_L = 0, /* lowest */
	QR_ECLEVEL_M,
	QR_ECLEVEL_Q,
	QR_ECLEVEL_H      /* highest */
};

/**
 * Maximum version (size) of QR-code symbol.
 */
#define QRSPEC_VERSION_MAX 40

/**
 * Maximum version (size) of QR-code symbol.
 */
#define MQRSPEC_VERSION_MAX 4

/******************************************************************************
 * Input data (qrinput.c)
 *****************************************************************************/
/**
 * Instantiate an input data object. The version is set to 0 (auto-select)
 * and the error correction level is set to QR_ECLEVEL_L.
 * @return an input object (initialized). On error, NULL is returned and errno
 *         is set to indicate the error.
 * @throw ENOMEM unable to allocate memory.
 */
struct qrinput *qrinput_new(void);

/**
 * Instantiate an input data object.
 * @param version version number.
 * @param level Error correction level.
 * @return an input object (initialized). On error, NULL is returned and errno
 *         is set to indicate the error.
 * @throw ENOMEM unable to allocate memory for input objects.
 * @throw EINVAL invalid arguments.
 */
struct qrinput *qrinput_new2(int version, enum qrec_level level);

/**
 * Instantiate an input data object. Object's Micro QR Code flag is set.
 * Unlike with full-sized QR Code, version number must be specified (>0).
 * @param version version number (1--4).
 * @param level Error correction level.
 * @return an input object (initialized). On error, NULL is returned and errno
 *         is set to indicate the error.
 * @throw ENOMEM unable to allocate memory for input objects.
 * @throw EINVAL invalid arguments.
 */
struct qrinput *qrinput_new_micro(int version, enum qrec_level level);

/**
 * Append data to an input object.
 * The data is copied and appended to the input object.
 * @param input input object.
 * @param mode encoding mode.
 * @param size size of data (byte).
 * @param data a pointer to the memory area of the input data.
 * @retval 0 success.
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ENOMEM unable to allocate memory.
 * @throw EINVAL input data is invalid.
 *
 */
int qrinput_append(struct qrinput *input, enum qrencode_mode mode,
		   int size, const unsigned char *data);

/**
 * Append ECI header.
 * @param input input object.
 * @param ecinum ECI indicator number (0 - 999999)
 * @retval 0 success.
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw ENOMEM unable to allocate memory.
 * @throw EINVAL input data is invalid.
 *
 */
int qrinput_append_eci_header(struct qrinput *input, unsigned int ecinum);

/**
 * Get current version.
 * @param input input object.
 * @return current version.
 */
int qrinput_get_version(struct qrinput *input);

/**
 * Set version of the QR code that is to be encoded.
 * This function cannot be applied to Micro QR Code.
 * @param input input object.
 * @param version version number (0 = auto)
 * @retval 0 success.
 * @retval -1 invalid argument.
 */
int qrinput_set_version(struct qrinput *input, int version);

/**
 * Get current error correction level.
 * @param input input object.
 * @return Current error correcntion level.
 */
enum qrec_level qrinput_get_error_correction_level(struct qrinput *input);

/**
 * Set error correction level of the QR code that is to be encoded.
 * This function cannot be applied to Micro QR Code.
 * @param input input object.
 * @param level Error correction level.
 * @retval 0 success.
 * @retval -1 invalid argument.
 */
int qrinput_set_error_correction_level(struct qrinput *input,
				       enum qrec_level level);

/**
 * Set version and error correction level of the QR code at once.
 * This function is recommened for Micro QR Code.
 * @param input input object.
 * @param version version number (0 = auto)
 * @param level Error correction level.
 * @retval 0 success.
 * @retval -1 invalid argument.
 */
int
qrinput_set_version_and_error_correction_level(struct qrinput *input,
					       int version,
					       enum qrec_level level);

/**
 * Free the input object.
 * All of data chunks in the input object are freed too.
 * @param input input object.
 */
void qrinput_free(struct qrinput *input);

/**
 * Validate the input data.
 * @param mode encoding mode.
 * @param size size of data (byte).
 * @param data a pointer to the memory area of the input data.
 * @retval 0 success.
 * @retval -1 invalid arguments.
 */
int qrinput_check(enum qrencode_mode mode, int size,
		  const unsigned char *data);

/**
 * Instantiate a set of input data object.
 * @return an instance of qrinput_struct. On error, NULL is returned and errno
 *         is set to indicate the error.
 * @throw ENOMEM unable to allocate memory.
 */
struct qrinput_struct *qrinput_struct_new(void);

/**
 * Set parity of structured symbols.
 * @param s structured input object.
 * @param parity parity of s.
 */
void qrinput_struct_set_parity(struct qrinput_struct *s,
			       unsigned char parity);

/**
 * Append a qrinput object to the set. qrinput created by qrinput_new_micro()
 * will be rejected.
 * @warning never append the same qrinput object twice or more.
 * @param s structured input object.
 * @param input an input object.
 * @retval >0 number of input objects in the structure.
 * @retval -1 an error occurred. See Exceptions for the details.
 * @throw ENOMEM unable to allocate memory.
 * @throw EINVAL invalid arguments.
 */
int qrinput_struct_append_input(struct qrinput_struct *s,
				struct qrinput *input);

/**
 * Free all of qrinput in the set.
 * @param s a structured input object.
 */
void qrinput_struct_free(struct qrinput_struct *s);

/**
 * Split a qrinput to qrinput_struct. It calculates a parity, set it, then
 * insert structured-append headers. qrinput created by qrinput_new_micro() will
 * be rejected.
 * @param input input object. Version number and error correction level must be
 *        set.
 * @return a set of input data. On error, NULL is returned, and errno is set
 *         to indicate the error. See Exceptions for the details.
 * @throw ERANGE input data is too large.
 * @throw EINVAL invalid input data.
 * @throw ENOMEM unable to allocate memory.
 */
struct qrinput_struct *qrinput_split_qrinput_to_struct(struct qrinput *input);

/**
 * Insert structured-append headers to the input structure. It calculates
 * a parity and set it if the parity is not set yet.
 * @param s input structure
 * @retval 0 success.
 * @retval -1 an error occurred and errno is set to indeicate the error.
 *            See Execptions for the details.
 * @throw EINVAL invalid input object.
 * @throw ENOMEM unable to allocate memory.
 */
int qrinput_struct_insert_structured_append_headers(struct qrinput_struct *s);

/**
 * Set FNC1-1st position flag.
 */
int qrinput_set_fnc1_first(struct qrinput *input);

/**
 * Set FNC1-2nd position flag and application identifier.
 */
int qrinput_set_fnc1_second(struct qrinput *input, unsigned char appid);

/******************************************************************************
 * qrcode output (qrencode.c)
 *****************************************************************************/

/**
 * qrcode class.
 * Symbol data is represented as an array contains width*width uchars.
 * Each uchar represents a module (dot). If the less significant bit of
 * the uchar is 1, the corresponding module is black. The other bits are
 * meaningless for usual applications, but here its specification is described.
 *
 * <pre>
 * MSB 76543210 LSB
 *     |||||||`- 1=black/0=white
 *     ||||||`-- data and ecc code area
 *     |||||`--- format information
 *     ||||`---- version information
 *     |||`----- timing pattern
 *     ||`------ alignment pattern
 *     |`------- finder pattern and separator
 *     `-------- non-data modules (format, timing, etc.)
 * </pre>
 */
struct qrcode {
	int version;         /* version of the symbol */
	int width;           /* width of the symbol */
	unsigned char *data; /* symbol data */
};

/**
 * Singly-linked list of qrcode. Used to represent a structured symbols.
 * A list is terminated with NULL.
 */
struct qrcode_list {
	struct qrcode *code;
	struct qrcode_list *next;
};

/**
 * Create a symbol from the input data.
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 * @param input input data.
 * @return an instance of qrcode class. The version of the result qrcode may
 *         be larger than the designated version. On error, NULL is returned,
 *         and errno is set to indicate the error. See Exceptions for the
 *         details.
 * @throw EINVAL invalid input object.
 * @throw ENOMEM unable to allocate memory for input objects.
 */
struct qrcode *qrcode_encode_input(struct qrinput *input);

/**
 * Create a symbol from the string. The library automatically parses the input
 * string and encodes in a QR Code symbol.
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 * @param string input string. It must be NUL terminated.
 * @param version version of the symbol. If 0, the library chooses the minimum
 *                version for the given input data.
 * @param level error correction level.
 * @param hint tell the library how Japanese Kanji characters should be
 *             encoded. If QR_MODE_KANJI is given, the library assumes that the
 *             given string contains Shift-JIS characters and encodes them in
 *             Kanji-mode. If QR_MODE_8 is given, all of non-alphanumerical
 *             characters will be encoded as is. If you want to embed UTF-8
 *             string, choose this. Other mode will cause EINVAL error.
 * @param casesensitive case-sensitive(1) or not(0).
 * @return an instance of qrcode class. The version of the result qrcode may
 *         be larger than the designated version. On error, NULL is returned,
 *         and errno is set to indicate the error. See Exceptions for the
 *         details.
 * @throw EINVAL invalid input object.
 * @throw ENOMEM unable to allocate memory for input objects.
 * @throw ERANGE input data is too large.
 */
struct qrcode
*qrcode_encode_string(const char *string, int version,
		      enum qrec_level level, enum qrencode_mode hint,
		      int casesensitive);

/**
 * Same to qrcode_encode_string(), but encode whole data in 8-bit mode.
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 */
struct qrcode
*qrcode_encode_string_8bit(const char *string, int version,
			 enum qrec_level level);

/**
 * Micro QR Code version of qrcode_encode_string().
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 */
struct qrcode
*qrcode_encode_string_micro(const char *string,
			    int version, enum qrec_level level,
			    enum qrencode_mode hint, int casesensitive);

/**
 * Micro QR Code version of qrcode_encode_string_8bit().
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 */
struct qrcode
*qrcode_encode_string_8bit_micro(const char *string, int version,
				 enum qrec_level level);

/**
 * Encode byte stream (may include '\0') in 8-bit mode.
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 * @param size size of the input data.
 * @param data input data.
 * @param version version of the symbol. If 0, the library chooses the minimum
 *                version for the given input data.
 * @param level error correction level.
 * @throw EINVAL invalid input object.
 * @throw ENOMEM unable to allocate memory for input objects.
 * @throw ERANGE input data is too large.
 */
struct qrcode
*qrcode_encode_data(int size, const unsigned char *data,
		    int version, enum qrec_level level);

/**
 * Micro QR Code version of qrcode_encode_data().
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 */
struct qrcode
*qrcode_encode_data_micro(int size, const unsigned char *data,
			  int version, enum qrec_level level);

/**
 * Free the instance of qrcode class.
 * @param qrcode an instance of qrcode class.
 */
void qrcode_free(struct qrcode *qrcode);

/**
 * Create structured symbols from the input data.
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 * @param s
 * @return a singly-linked list of qrcode.
 */
struct qrcode_list
*qrcode_encode_input_structured(struct qrinput_struct *s);

/**
 * Create structured symbols from the string. The library automatically parses
 * the input string and encodes in a QR Code symbol.
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 * @param string input string. It must be NUL terminated.
 * @param version version of the symbol.
 * @param level error correction level.
 * @param hint tell the library how Japanese Kanji characters should be
 *             encoded. If QR_MODE_KANJI is given, the library assumes that the
 *             given string contains Shift-JIS characters and encodes them in
 *             Kanji-mode. If QR_MODE_8 is given, all of non-alphanumerical
 *             characters will be encoded as is. If you want to embed UTF-8
 *             string, choose this. Other mode will cause EINVAL error.
 * @param casesensitive case-sensitive(1) or not(0).
 * @return a singly-linked list of qrcode. On error, NULL is returned, and
 *         errno is set to indicate the error. See Exceptions for the details.
 * @throw EINVAL invalid input object.
 * @throw ENOMEM unable to allocate memory for input objects.
 */
struct qrcode_list
*qrcode_encode_string_structured(const char *string, int version,
				 enum qrec_level level, enum qrencode_mode hint,
				 int casesensitive);

/**
 * Same to qrcode_encode_string_structured(), but encode whole data in 8-bit
 * mode.
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 */
struct qrcode_list
*qrcode_encode_string_8bit_structured(const char *string, int version,
				      enum qrec_level level);

/**
 * Create structured symbols from byte stream (may include '\0'). Wholde data
 * are encoded in 8-bit mode.
 * @warning This function is THREAD UNSAFE when pthread is disabled.
 * @param size size of the input data.
 * @param data input dat.
 * @param version version of the symbol.
 * @param level error correction level.
 * @return a singly-linked list of qrcode. On error, NULL is returned, and
 *         errno is set to indicate the error. See Exceptions for the details.
 * @throw EINVAL invalid input object.
 * @throw ENOMEM unable to allocate memory for input objects.
 */
struct qrcode_list
*qrcode_encode_data_structured(int size, const unsigned char *data,
			       int version, enum qrec_level level);

/**
 * Return the number of symbols included in a qrcode_list.
 * @param qrlist a head entry of a qrcode_list.
 * @return number of symbols in the list.
 */
int qrcode_list_size(struct qrcode_list *qrlist);

/**
 * Free the qrcode_list.
 * @param qrlist a head entry of a qrcode_list.
 */
void qrcode_list_free(struct qrcode_list *qrlist);

#endif /* __QRENCODE_H__ */
