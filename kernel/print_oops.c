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

void print_qr_err(void)
{
	printk("Buffer for QR; len %d:\n%s\n", buf_pos, qr_buffer);
	buf_pos = 0;
}

