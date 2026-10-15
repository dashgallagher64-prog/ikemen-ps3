#ifndef STUB_IO_PAD_H
#define STUB_IO_PAD_H
/* Host-test stub of PSL1GHT <io/pad.h> (only what main.c uses). */
#define MAX_PADS 127
typedef struct { int status[MAX_PADS]; } padInfo;
typedef struct { int BTN_UP, BTN_DOWN, BTN_CROSS, BTN_CIRCLE, BTN_L1, BTN_R1; } padData;
int ioPadInit(int max);
int ioPadGetInfo(padInfo *info);
int ioPadGetData(int port, padData *data);
#endif
