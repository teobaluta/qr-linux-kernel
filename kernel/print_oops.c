/*
 * TODO copyright
 *
 */
#include <linux/print_oops.h>
#include <linux/kdebug.h>
#include <linux/bug.h>
#include <linux/qrencode.h>
#include <linux/fb.h>

static int remaining_qr_len = MAX_QR_BUF_LEN;
static char qr_buffer[MAX_QR_BUF_LEN];
static char *buf = qr_buffer;

extern void print_err(const char *fmt, ...)
{
	va_list args;
	int written;

	va_start(args, fmt);
	if (remaining_qr_len > 0) {
		written = vsnprintf(buf, remaining_qr_len, fmt, args);
		remaining_qr_len -= written;
		buf += written;
	}
	vprintk(fmt, args);
	va_end(args);
}

extern void print_qr_err(void)
{
}
