/*
 * Copyright 2004 by Costas Stylianou <costas.stylianou@psion.com> +44(0)7850 394095
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Costas Sylianou not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. Costas Stylianou makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * COSTAS STYLIANOU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL COSTAS STYLIANOU BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * epson13806draw.c - Implementation of hardware accelerated functions for
 *                    Epson S1D13806 Graphic controller.
 *
 * History:
 * 28-Jan-04  C.Stylianou       PRJ NBL: Created from chipsdraw.c
 *
 */

#include    "exa.h"
#include    "exa_priv.h"

#include    "epson13806.h"
#include    "epson13806reg.h"

//#define __DEBUG_EPSON__

#ifdef __DEBUG_EPSON__
    #define EPSON_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
    #define EPSON_DEBUG(...) do { } while (0)
#endif

#define EpsonExaPriv(pPix) \
    ScreenPtr pScreen = pPix->drawable.pScreen; \
    KdScreenPriv(pScreen); \
    KdScreenInfo *screen = pScreenPriv->screen; \
    EpsonScrPriv *scrpriv = screen->driver; \
    EpsonExaPriv *exaPriv = scrpriv->exaPriv; \

// Functionality of BitBLT ROP register for Epson S1D13806 Graphics controller
CARD8 epson13806Rop[16] = {
    /* GXclear      */      0x00,         /* 0 */
    /* GXand        */      0x08,         /* src AND dst */
    /* GXandReverse */      0x04,         /* src AND NOT dst */
    /* GXcopy       */      0x0C,         /* src */
    /* GXandInverted*/      0x02,         /* NOT src AND dst */
    /* GXnoop       */      0x0A,         /* dst */
    /* GXxor        */      0x06,         /* src XOR dst */
    /* GXor         */      0x0E,         /* src OR dst */
    /* GXnor        */      0x01,         /* NOT src AND NOT dst */
    /* GXequiv      */      0x09,         /* NOT src XOR dst */
    /* GXinvert     */      0x05,         /* NOT dst */
    /* GXorReverse  */      0x0D,         /* src OR NOT dst */
    /* GXcopyInverted*/     0x03,         /* NOT src */
    /* GXorInverted */      0x0B,         /* NOT src OR dst */
    /* GXnand       */      0x07,         /* NOT src OR NOT dst */
    /* GXset        */      0x0F,         /* 1 */
};

static unsigned char *regbase;

static inline void
epsonBg (Pixel bg)
{
    EPSON13806_REG16(EPSON13806_BLTBGCOLOR) = bg;
}

static inline void
epsonFg (Pixel fg)
{
    EPSON13806_REG16(EPSON13806_BLTFGCOLOR) = fg;
}

static inline void
epsonWaitForHwBltDone (void)
{
    while (EPSON13806_REG (EPSON13806_BLTCTRL0) & EPSON13806_BLTCTRL0_ACTIVE) {}
}

static void
epsonWaitMarker (ScreenPtr pScreen, int marker)
{
    EPSON_DEBUG ("%s\n", __func__);

    epsonWaitForHwBltDone ();
}

static Bool
epsonPrepareSolid (PixmapPtr pPix,
                   int       alu,
                   Pixel     pm,
                   Pixel     fg)
{
    EpsonExaPriv (pPix);

    EPSON_DEBUG ("%s, alu [0x%x] bpp [%d]\n", __func__, alu,
                 pPix->drawable.bitsPerPixel);

    if (pPix->drawable.bitsPerPixel != (exaPriv->bytesPerPixel << 3))
        return FALSE;

    if (!EXA_PM_IS_SOLID (&(pPix->drawable), pm))
        return FALSE;

    fg &= 0xffff;
    epsonFg (fg);
    epsonBg (fg);

    epsonWaitForHwBltDone ();

    EPSON13806_REG(EPSON13806_BLTROP) = epson13806Rop[alu];

    if (alu == GXnoop)
    {
        EPSON13806_REG(EPSON13806_BLTOPERATION) = EPSON13806_BLTOPERATION_PATFILLROP;
    }
    else
    {
        EPSON13806_REG(EPSON13806_BLTOPERATION) = EPSON13806_BLTOPERATION_SOLIDFILL;
    }

    return TRUE;
}

static void
epsonSolid (PixmapPtr pPix, int x1, int y1, int x2, int y2)
{
    int pitch = exaGetPixmapPitch (pPix);
    CARD32  dst_addr;
    int width, height;

    EpsonExaPriv (pPix);

    EPSON_DEBUG ("%s Solid X1 [%d] Y1 [%d] X2 [%d] Y2 [%d]\n",
                 __func__, x1, y1, x2, y2);

    dst_addr = exaGetPixmapOffset (pPix);
    dst_addr += (y1 * pitch) + (x1 * exaPriv->bytesPerPixel);

    width = ((x2 - x1) - 1);
    height = ((y2 - y1) - 1);

    // Destination is linear
    EPSON13806_REG (EPSON13806_BLTCTRL0) &= 0x2;

    // program BLIT memory offset
    EPSON13806_REG16(EPSON13806_BLTSTRIDE) = pitch / exaPriv->bytesPerPixel;

    // program dst address
    EPSON13806_REG16(EPSON13806_BLTDSTSTART01) = dst_addr;
    EPSON13806_REG(EPSON13806_BLTDSTSTART2) = dst_addr >> 16;

    // program width and height of blit
    EPSON13806_REG16(EPSON13806_BLTWIDTH) = width;
    EPSON13806_REG16(EPSON13806_BLTHEIGHT) = height;

    EPSON13806_REG(EPSON13806_BLTCTRL0) = EPSON13806_BLTCTRL0_ACTIVE;

    // Wait for operation to complete
    epsonWaitForHwBltDone ();
}

static void
epsonDoneSolid (PixmapPtr pPix)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;

    EPSON_DEBUG ("%s\n", __func__);

    // Read from BitBLT data offset 0 to shut it down
    (void)EPSON13806_REG (EPSON13806_BITBLTDATA);

    exaMarkSync(pScreen);
}

static Bool
epsonPrepareCopy (PixmapPtr pSrc,
                  PixmapPtr pDst,
                  int       xdir,
                  int       ydir,
                  int       alu,
                  Pixel     pm)
{
    EpsonExaPriv (pDst);
    unsigned int bitsPerPixel = exaPriv->bytesPerPixel << 3;

    // We're using EXA_TWO_BITBLT_DIRECTIONS xdir should always equal ydir
    assert (xdir == ydir);

    EPSON_DEBUG ("%s, neg_dir [%d] alu [0x%x]\n", __func__, (xdir < 0), alu);
    EPSON_DEBUG ("%s, src_off [0x%.6x] src_pitch [%d] src_bpp [%d]\n", __func__,
                 (unsigned int)exaGetPixmapOffset(pSrc), (int)exaGetPixmapPitch (pSrc),
                 pSrc->drawable.bitsPerPixel);
    EPSON_DEBUG ("%s, dst_off [0x%.6x] dst_pitch [%d] dst_bpp [%d]\n", __func__,
                 (unsigned int)exaGetPixmapOffset(pDst), (int)exaGetPixmapPitch (pDst),
                 pDst->drawable.bitsPerPixel);

    if (pSrc->drawable.bitsPerPixel != bitsPerPixel ||
        pDst->drawable.bitsPerPixel != bitsPerPixel)
        return FALSE;

    if (!EXA_PM_IS_SOLID (&(pDst->drawable), pm))
        return FALSE;

    if (exaGetPixmapPitch (pSrc) != exaGetPixmapPitch (pDst))
        return FALSE;

    exaPriv->negative_dir = (xdir < 0);
    exaPriv->pSrc = pSrc;
    exaPriv->pDst = pDst;

    epsonWaitForHwBltDone ();
	exaMarkSync(pDst->drawable.pScreen);
    EPSON13806_REG(EPSON13806_BLTROP) = epson13806Rop[alu];

    return TRUE;
}

static void
epsonCopy (PixmapPtr pDst,
           int       sx,
           int       sy,
           int       dx,
           int       dy,
           int       width,
           int       height)
{
    EpsonExaPriv (pDst);
    CARD32 src_addr = exaGetPixmapOffset (exaPriv->pSrc);
    CARD32 dst_addr = exaGetPixmapOffset (exaPriv->pDst);
    CARD32 src_pitch = exaGetPixmapPitch (exaPriv->pSrc);
    CARD32 dst_pitch = exaGetPixmapPitch (exaPriv->pDst);
    unsigned int bytesPerPixel = exaPriv->bytesPerPixel;

    EPSON_DEBUG ("%s %dx%d (%d, %d)->(%d, %d)\n", __func__,
                 width, height, sx, sy, dx, dy);

    if (!width || !height)
        return;

    if (exaPriv->negative_dir) {
        src_addr += (((sy + height - 1) * src_pitch) + (bytesPerPixel * (sx + width - 1)));
        dst_addr += (((dy + height - 1) * dst_pitch) + (bytesPerPixel * (dx + width - 1)));
    } else {
        src_addr += (sy * src_pitch) + (bytesPerPixel * sx);
        dst_addr += (dy * dst_pitch) + (bytesPerPixel * dx);
    }

    // Src and Dst are linear
    EPSON13806_REG (EPSON13806_BLTCTRL0) &= 0x3;

    // program BLIT memory offset
    EPSON13806_REG16(EPSON13806_BLTSTRIDE) = src_pitch / bytesPerPixel;

    // program src and dst addresses
    EPSON13806_REG16(EPSON13806_BLTSRCSTART01) = src_addr;
    EPSON13806_REG(EPSON13806_BLTSRCSTART2) = src_addr >> 16;
    EPSON13806_REG16(EPSON13806_BLTDSTSTART01) = dst_addr;
    EPSON13806_REG(EPSON13806_BLTDSTSTART2) = dst_addr >> 16;

    // program width and height of blit
    EPSON13806_REG16(EPSON13806_BLTWIDTH) = width - 1;
    EPSON13806_REG16(EPSON13806_BLTHEIGHT) = height - 1;

    if (exaPriv->negative_dir) {
        EPSON13806_REG(EPSON13806_BLTOPERATION) = EPSON13806_BLTOPERATION_MOVENEGROP;
    } else {
        EPSON13806_REG(EPSON13806_BLTOPERATION) = EPSON13806_BLTOPERATION_MOVEPOSROP;
    }

    EPSON13806_REG(EPSON13806_BLTCTRL0) = EPSON13806_BLTCTRL0_ACTIVE;

    // Wait for operation to complete
    epsonWaitForHwBltDone ();
}

static void
epsonDoneCopy (PixmapPtr pDst)
{
    EpsonExaPriv (pDst);
    EPSON_DEBUG ("%s\n", __func__);

    exaPriv->pSrc = NULL;
    exaPriv->pDst = NULL;

    // Read from BitBLT data offset 0 to shut it down
    (void)EPSON13806_REG (EPSON13806_BITBLTDATA);

    exaMarkSync(pScreen);
}

Bool
epsonUploadToScreen(PixmapPtr pDst,
                    int       x,
                    int       y,
                    int       w,
                    int       h,
                    char      *src,
                    int       src_pitch)
{
    EPSON_DEBUG ("%s, x [%d] y [%d] w [%d] h [%d] src [%p] src_pitch [%d]\n",
                 __func__, x, y, w, h, src, src_pitch);
    EPSON_DEBUG ("%s, DST: off [0x%x] pitch [%d]\n", __func__,
                 (unsigned int)exaGetPixmapOffset (pDst),
                 (int)exaGetPixmapPitch (pDst));
    return FALSE;
}

Bool
epsonDownloadFromScreen(PixmapPtr pSrc,
                        int       x,
                        int       y,
                        int       w,
                        int       h,
                        char      *dst,
                        int       dst_pitch)
{
    EPSON_DEBUG ("%s, x [%d] y [%d] w [%d] h [%d] dst [%p] dst_pitch [%d]\n",
                 __func__, x, y, w, h, dst, dst_pitch);
    EPSON_DEBUG ("%s, SRC: off [0x%x] pitch [%d]\n", __func__,
                 (unsigned int)exaGetPixmapOffset (pSrc),
                 (int)exaGetPixmapPitch (pSrc));
    return FALSE;
}

Bool
epsonDrawInit (ScreenPtr pScreen)
{
    //KdScreenPriv(pScreen);
    KdPrivScreenPtr pScreenPriv = ((KdPrivScreenPtr) dixLookupPrivate(&(pScreen->devPrivates), kdScreenPrivateKey));

    KdScreenInfo *screen = pScreenPriv->screen;
    EpsonScrPriv *epsons = screen->driver;
    EpsonPriv    *priv = screen->card->driver;
    EpsonExaPriv *exaPriv = NULL;
    static int addr = 0x00000000;

    EPSON_DEBUG ("%s\n", __func__);

    exaPriv = calloc(1, sizeof(EpsonExaPriv));
    if (exaPriv == NULL) {
        perror("Failed to allocate EXA private data structure\n");
        return FALSE;
    }

    exaPriv->screenStride = screen->fb.byteStride;
    exaPriv->bytesPerPixel = screen->fb.bitsPerPixel >> 3;

#if 0
    EPSON13806_REG(EPSON13806_MISC) = 0x00;
    EPSON13806_REG(EPSON13806_DISPMODE) = 0x00;
    EPSON13806_REG16(EPSON13806_GPIOCFG) = 0xffff;
    EPSON13806_REG16(EPSON13806_GPIOCTRL) = 0x0001;

    EPSON13806_REG(EPSON13806_MEMCLKCFG) = 0x01;
    EPSON13806_REG(EPSON13806_LCDPCLKCFG) = 0x00;
    EPSON13806_REG(EPSON13806_CRTPCLKCFG) = 0x02;
    EPSON13806_REG(EPSON13806_MPCLKCFG) = 0x02;
    EPSON13806_REG(EPSON13806_CPUMEMWAITSEL) = 0x01;
    EPSON13806_REG(EPSON13806_MEMCFG) = 0x80;
    EPSON13806_REG(EPSON13806_DRAMREFRESH) = 0x03;
    EPSON13806_REG16(EPSON13806_DRAMTIMINGCTRL) = 0x0100;

    // 5ms delay for internal LCD SDRAM to initialize
    usleep(5000);

    EPSON13806_REG(EPSON13806_PANELTYPE) = 0x25;
    EPSON13806_REG(EPSON13806_MODRATE) = 0x00;
    EPSON13806_REG(EPSON13806_LCDHDP) = 0x63;
    EPSON13806_REG(EPSON13806_LCDHNDP) = 0x1f;
    EPSON13806_REG(EPSON13806_TFTFPLINESTART) = 0x01;
    EPSON13806_REG(EPSON13806_TFTFPLINEPULSE) = 0x0b;
    EPSON13806_REG16(EPSON13806_LCDVDP0) = 0x0257;
    EPSON13806_REG(EPSON13806_LCDVNDP) = 0x1b;
    EPSON13806_REG(EPSON13806_TFTFPFRAMESTART) = 0x0a;
    EPSON13806_REG(EPSON13806_TFTFPFRAMEPULSE) = 0x01;
    EPSON13806_REG(EPSON13806_LCDDISPMODE) = 0x85;
    EPSON13806_REG(EPSON13806_LCDMISC) = 0x00;
    EPSON13806_REG16(EPSON13806_LCDSTART01) = 0x0000;
    EPSON13806_REG(EPSON13806_LCDSTART2) = 0x00;
    EPSON13806_REG16(EPSON13806_LCDSTRIDE) = byteStride>>1;
    EPSON13806_REG(EPSON13806_LCDPIXELPAN) = 0x00;
    EPSON13806_REG(EPSON13806_LCDFIFOHIGH) = 0x00;
    EPSON13806_REG(EPSON13806_LCDFIFOLOW) = 0x00;
#endif

    EPSON13806_REG(EPSON13806_BLTCTRL0) = 0x00;
    EPSON13806_REG(EPSON13806_BLTCTRL1) = 0x01;     // We're using 16 bpp
    EPSON13806_REG16(EPSON13806_BLTSTRIDE) = exaPriv->screenStride >> 1; // program BLIT memory offset

#if 0
    EPSON13806_REG(EPSON13806_LUTMODE) = 0x00;
    EPSON13806_REG(EPSON13806_LUTADDR) = 0x00;
    EPSON13806_REG(EPSON13806_PWRSAVECFG) = 0x10;
    EPSON13806_REG(EPSON13806_PWRSAVESTATUS) = 0x00;
    EPSON13806_REG(EPSON13806_CPUMEMWATCHDOG) = 0x00;
    EPSON13806_REG(EPSON13806_DISPMODE) = 0x01;

    // Enable backlight voltage
    EPSON13806_REG16(EPSON13806_GPIOCTRL) |= 1<<1;
    // 10ms delay after turning on LCD.
    usleep(10000);
#endif

    // Instruct the BitBLT unit to fill the screen with black, i.e clear fb.
    EPSON13806_REG16(EPSON13806_BLTDSTSTART01) = addr;
    EPSON13806_REG(EPSON13806_BLTDSTSTART2) = addr >> 16;
    EPSON13806_REG16(EPSON13806_BLTFGCOLOR) = 0x0000;
    EPSON13806_REG(EPSON13806_BLTOPERATION) = EPSON13806_BLTOPERATION_SOLIDFILL; // solid fill blt
    EPSON13806_REG16(EPSON13806_BLTWIDTH) = (0x0320-1);
    EPSON13806_REG16(EPSON13806_BLTHEIGHT) = (0x0258-1);
    EPSON13806_REG(EPSON13806_BLTCTRL0) = EPSON13806_BLTCTRL0_ACTIVE;

#if 0
    // Enable LCD data
    EPSON13806_REG(EPSON13806_LCDDISPMODE) &= ~(1<<7);

    // Turn on backlight full
    EPSON13806_REG16(EPSON13806_GPIOCTRL) |= 0x00fc;
#endif

    exaPriv->exa.exa_major = 2;
    exaPriv->exa.exa_minor = 0;

    exaPriv->exa.memoryBase    = (CARD8 *) (priv->fb);
    exaPriv->exa.offScreenBase = screen->fb.byteStride * screen->height;
    exaPriv->exa.memorySize    = priv->fix.smem_len;

    EPSON_DEBUG ("Memory Base = 0x%x\n", (unsigned int)exaPriv->exa.memoryBase);
    EPSON_DEBUG ("Memory Size = 0x%x\n", (unsigned int)exaPriv->exa.memorySize);
    EPSON_DEBUG ("Offscreen Base = 0x%x\n", (unsigned int)exaPriv->exa.offScreenBase);

    exaPriv->exa.PrepareSolid = epsonPrepareSolid;
    exaPriv->exa.Solid        = epsonSolid;
    exaPriv->exa.DoneSolid    = epsonDoneSolid;

    exaPriv->exa.PrepareCopy  = epsonPrepareCopy;
    exaPriv->exa.Copy         = epsonCopy;
    exaPriv->exa.DoneCopy     = epsonDoneCopy;

    exaPriv->exa.UploadToScreen     = epsonUploadToScreen;
    exaPriv->exa.DownloadFromScreen = epsonDownloadFromScreen;

    exaPriv->exa.WaitMarker   = epsonWaitMarker;

    exaPriv->exa.maxX         = screen->width - 1;
    exaPriv->exa.maxY         = screen->height - 1;

    exaPriv->exa.pixmapOffsetAlign = 4;
    exaPriv->exa.pixmapPitchAlign  = 4;

    exaPriv->exa.flags        = EXA_OFFSCREEN_PIXMAPS | EXA_TWO_BITBLT_DIRECTIONS;

    if (!exaDriverInit (pScreen, &(exaPriv->exa))) {
        ErrorF("Failed to initialize EXA\n");
        return FALSE;
    } else {
		ErrorF("Initialized EXA acceleration\n");
        epsons->exaPriv = exaPriv;
    }

    return TRUE;
}

void
epsonDrawEnable (ScreenPtr pScreen)
{
    EPSON_DEBUG ("%s\n", __func__);
    exaWaitSync (pScreen);
    exaMarkSync (pScreen);
}

void
epsonDrawDisable (ScreenPtr pScreen)
{
    EPSON_DEBUG ("%s\n", __func__);
}

void
epsonDrawFini (ScreenPtr pScreen)
{
    EPSON_DEBUG ("%s\n", __func__);
}

void
initEpson13806(void)
{
    EPSON_DEBUG ("%s\n", __func__);

    // Map Epson S1D13806 registers
    regbase = epsonMapDevice (EPSON13806_PHYSICAL_REG_ADDR, EPSON13806_GPIO_REGSIZE);
    if (!regbase)
        perror("ERROR: regbase\n");   // Sets up register mappings in header files.
#if 0
    CARD8 rev_code;
    rev_code = EPSON13806_REG (EPSON13806_REVCODE);
    if ((rev_code >> 2) != 0x07)
        perror("ERROR: EPSON13806 Display Controller NOT FOUND!\n");
#endif
}

void
exaDDXDriverInit(ScreenPtr pScreen)
{
}