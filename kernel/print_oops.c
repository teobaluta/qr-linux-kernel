/*
 *
 * Copyright (C) 2014 Teodora Baluta <teobaluta@gmail.com>
 * Copyright (C) 2014 Levente Kurusa <levex@linux.com>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#include <linux/print_oops.h>
#include <linux/kdebug.h>
#include <linux/bug.h>
#include <linux/qrencode.h>
#include <linux/fb.h>
#include <linux/zlib.h>

#define COMPR_LEVEL 6
#define QQQ_WHITE 0x0F
#define QQQ_BLACK 0x00

static int qr_oops = 1;
core_param(qr_oops, qr_oops, int, 0644); 

static char qr_buffer[QR_BUFSIZE];
static int buf_pos;

static DEFINE_MUTEX(compr_mutex);
static struct z_stream_s stream;

static int bug_in_code;

void qr_append(char *text)
{
	size_t len;

	len = strlen(text);
	if (len + buf_pos >= QR_BUFSIZE - 1) {
		len = QR_BUFSIZE - 1 - buf_pos;
		qr_buffer[QR_BUFSIZE - 1] = '\0';
	}
	memcpy(&qr_buffer[buf_pos], text, len);
	buf_pos += len;
}


static inline int compute_w(struct fb_info *info, int qrw)
{
	int xres  = info->var.xres;
	int yres  = info->var.yres;
	int minxy = (xres < yres) ? xres : yres;
	int ret = minxy / 3;

	/* try to apply scaling */
	ret *= qr_oops;
	if (ret > xres || ret > yres) {
		/* loop until we find the maximum we can use */
		while (qr_oops > 1) {
			ret /= qr_oops;
			qr_oops--;
			ret *= qr_oops;
			if (ret <= xres && ret <= yres)
				goto exit;
		}
		printk(KERN_WARNING "Was unable to find suitable scaling!\n");
		return 0;
	}

exit:
	return ret / qrw;
}

static int qr_compr_init(void)
{
	size_t size = max(zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL),
			zlib_inflate_workspacesize());
	stream.workspace = vmalloc(size);
	if (!stream.workspace)
		return -ENOMEM;
	return 0;
}

static void qr_compr_exit(void)
{
	vfree(stream.workspace);
}

static int qr_compress(void *in, void *out, size_t inlen, size_t outlen)
{
	int ret;

	ret = qr_compr_init();
	if (ret != 0)
		goto error;
	mutex_lock(&compr_mutex);
	ret = zlib_deflateInit(&stream, COMPR_LEVEL);
	if (ret != Z_OK)
		goto error;

	stream.next_in = in;
	stream.avail_in = inlen;
	stream.total_in = 0;
	stream.next_out = out;
	stream.avail_out = outlen;
	stream.total_out = 0;

	ret = zlib_deflate(&stream, Z_FINISH);
	if (ret != Z_STREAM_END)
		goto error;

	ret = zlib_deflateEnd(&stream);
	if (ret != Z_OK)
		goto error;

	if (stream.total_out >= stream.total_in)
		goto error;

	ret = stream.total_out;
error:
	mutex_unlock(&compr_mutex);
	return ret;
}

void print_qr_err(void)
{
	ssize_t compr_len;
	struct fb_info *info;
	struct fb_fillrect rect;
	struct QRcode *qr;
	int i, j;
	int w;
	int is_black;
	char compr_qr_buffer[buf_pos];

	if (!qr_oops)
		return;

	if (bug_in_code) {
		printk(KERN_EMERG "QR encoding triggers an error. Disabling.\n");
		qr_oops = 0;
		return;
	}

	bug_in_code ++;

	info = registered_fb[0];
	if (!info) {
		printk(KERN_WARNING "Unable to get hand of a framebuffer!\n");
		return;
	}

	compr_len = qr_compress(qr_buffer, compr_qr_buffer, buf_pos, buf_pos);
	if (compr_len < 0) {
		printk(KERN_EMERG "Compression of QR code failed compr_len=%zd\n",
			   compr_len);
		return;
	}

	qr = QRcode_encodeData(compr_len, compr_qr_buffer, 0, QR_ECLEVEL_H);
	if (!qr) {
		printk(KERN_EMERG "Failed to encode data as a QR code!\n");
		return;
	}
	w = compute_w(info, qr->width);

	rect.width = w;
	rect.height = w;
	rect.rop = 0;

	/* Print borders: */
	rect.color = QQQ_WHITE;
	for (i = 0; i < qr->width + 2; i++) {
		/* Top */
		rect.dx = 0;
		rect.dy = i * w;
		cfb_fillrect(info, &rect);

		/* Bottom */
		rect.dx = (qr->width + 1) * w;
		rect.dy = i * w;
		cfb_fillrect(info, &rect);

		/* Left */
		rect.dx = i * w;
		rect.dy = 0;
		cfb_fillrect(info, &rect);

		/* Right */
		rect.dx = i * w;
		rect.dy = (qr->width + 1) * w;
		cfb_fillrect(info, &rect);
	}

	/* Print actual QR matrix: */
	for (i = 0; i < qr->width; i++) {
		for (j = 0; j < qr->width; j++) {
			rect.dx = (j + 1) * w;
			rect.dy = (i + 1) * w;
			is_black = qr->data[i * qr->width + j] & 1;
			rect.color = is_black ? QQQ_BLACK : QQQ_WHITE;
			cfb_fillrect(info, &rect);
		}
	}

	QRcode_free(qr);
	qr_compr_exit();
	buf_pos = 0;
	bug_in_code --;
}

