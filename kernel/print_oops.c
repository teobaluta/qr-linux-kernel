/*
 * TODO copyright
 *
 */
#include <linux/print_oops.h>
#include <linux/kdebug.h>
#include <linux/bug.h>
#include <linux/qrencode.h>
#include <linux/fb.h>

static char qr_buffer[QR_BUFSIZE];
static int buf_pos = 0;

#define QQQ_WHITE 0x0F
#define QQQ_BLACK 0x00

// XXX: optimizari?
void qr_append(char *text) {
	while (*text != '\0') {
		if (buf_pos == QR_BUFSIZE - 1) {
			qr_buffer[QR_BUFSIZE - 1] = '\0';
			return;
		}
		qr_buffer[buf_pos] = *text;
		buf_pos++;
		text++;
	}
} 

inline static int compute_w(struct fb_info *info, int qrw) {
	int xres  = info->var.xres;
	int yres  = info->var.yres;
	int minxy = (xres < yres) ? xres : yres;
	return minxy / qrw / 3;
}

void print_qr_err(void)
{
	printk("Buffer for QR; len %d:\n%s\n", buf_pos, qr_buffer);
	buf_pos = 0;

	struct fb_info *info;
	struct fb_fillrect rect;
	QRcode *qr;

	int i, j;
	int w;
	int is_black;

	/* Truncate ooops to 300 chars */
	qr_buffer[300] = '\0';

	qr = QRcode_encodeString(
	    qr_buffer, 0, QR_ECLEVEL_H, QR_MODE_8, 1);

	info = registered_fb[0];
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
}

