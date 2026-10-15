/* gfx.c - PS3 only: RSX framebuffer setup, double buffering, flip.
 * Drawing itself lives in gfx_draw.c (CPU-drawn pixels into the back buffer).
 */
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <rsx/rsx.h>
#include <sysutil/video.h>
#include "ikemen.h"

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)

static gcmContextData *ctx;
static uint32_t *buf[2];
static int cur, W, H;

#define GCM_LABEL_INDEX 255
static uint32_t label_val = 1;

/* same sequence the PSL1GHT samples use before reconfiguring video */
static void wait_rsx_idle(void)
{
    rsxSetWriteBackendLabel(ctx, GCM_LABEL_INDEX, label_val);
    rsxSetWaitLabel(ctx, GCM_LABEL_INDEX, label_val);
    label_val++;
    rsxSetWriteBackendLabel(ctx, GCM_LABEL_INDEX, label_val);
    rsxFlushBuffer(ctx);
    while (*(volatile uint32_t *)gcmGetLabelAddress(GCM_LABEL_INDEX) != label_val) usleep(30);
    label_val++;
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void make_buffer(int id)
{
    uint32_t offset;
    buf[id] = (uint32_t *)rsxMemalign(64, W * H * 4);
    rsxAddressToOffset(buf[id], &offset);
    gcmSetDisplayBuffer(id, offset, W * 4, W, H);
}

int gfx_init(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    if (!host) return -1;
    if (rsxInit(&ctx, CB_SIZE, HOST_SIZE, host) != 0) return -2;

    videoState state;
    videoResolution res;
    videoConfiguration vc;

    if (videoGetState(0, 0, &state) != 0) return -3;
    if (videoGetResolution(state.displayMode.resolution, &res) != 0) return -5;

    memset(&vc, 0, sizeof vc);
    vc.resolution = state.displayMode.resolution;
    vc.format     = VIDEO_BUFFER_FORMAT_XRGB;
    vc.pitch      = res.width * 4;
    vc.aspect     = state.displayMode.aspect;
    wait_rsx_idle();
    if (videoConfigure(0, &vc, NULL, 0) != 0) return -6;
    if (videoGetState(0, 0, &state) != 0) return -7;

    W = res.width;
    H = res.height;
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    make_buffer(0);
    make_buffer(1);
    memset(buf[0], 0, (size_t)W * H * 4);                  /* no garbage on the first flip */
    memset(buf[1], 0, (size_t)W * H * 4);
    gcmResetFlipStatus();

    /* prime the pipeline so the first wait_flip() returns */
    gcmSetFlip(ctx, 1);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur = 0;
    gfx_target(buf[cur], W, H);
    return 0;
}

uint32_t *gfx_begin(void)
{
    wait_flip();
    gfx_target(buf[cur], W, H);
    return buf[cur];
}

void gfx_end(void)
{
    if (gcmSetFlip(ctx, cur) == 0) {
        rsxFlushBuffer(ctx);
        gcmSetWaitFlip(ctx);
    }
    cur ^= 1;
}
