#ifndef _ASM_X86_PRINT_OOPS_H
#define _ASM_X86_PRINT_OOPS_H

#include <linux/module.h>

#define QR_BUFSIZE 4096

void qr_append(char *text);
void print_qr_err(void);

#endif /* _PRINT_OOPS_H */
