#ifndef _ASM_X86_PRINT_OPS_H
#define _ASM_X86_PRINT_OPS_H

#include <linux/module.h>

#define MAX_QR_BUF_LEN 4096

extern void print_err(const char *fmt, ...);
extern void print_qr_err(void);

#endif /* _PRINT_OPS_H */
