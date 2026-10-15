#ifndef STUB_SYSUTIL_H
#define STUB_SYSUTIL_H
/* Host-test stub of PSL1GHT <sysutil/sysutil.h> (only what main.c uses). */
#include <stdint.h>
#define SYSUTIL_EXIT_GAME 0x0101
typedef void (*sysutilCallback)(uint64_t status, uint64_t param, void *usrdata);
int sysUtilRegisterCallback(int slot, sysutilCallback cb, void *usrdata);
int sysUtilCheckCallback(void);
#endif
