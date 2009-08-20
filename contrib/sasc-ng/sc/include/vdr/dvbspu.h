/*
 * SPU decoder for DVB devices
 *
 * Copyright (C) 2001.2002 Andreas Schultz <aschultz@warp10.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 * parts of this file are derived from the OMS program.
 *
 * $Id: dvbspu.h 1.12 2006/04/17 12:47:29 kls Exp $
 */

#ifndef __DVBSPU_H
#define __DVBSPU_H

#include <inttypes.h>
#include "osd.h"
#include "spu.h"
#include "thread.h"

typedef struct sDvbSpuPalDescr {
    uint8_t index;
    uint8_t trans;

    bool operator != (const sDvbSpuPalDescr pd) const {
        return index != pd.index && trans != pd.trans;
    };
} aDvbSpuPalDescr[4];

typedef struct sDvbSpuRect {
    int x1, y1;
    int x2, y2;

    int width() {
        return x2 - x1 + 1;
    };
    int height() {
        return y2 - y1 + 1;
    };

    bool operator != (const sDvbSpuRect r) const {
        return r.x1 != x1 || r.y1 != y1 || r.x2 != x2 || r.y2 != y2;
    };
}

sDvbSpuRect;

// --- cDvbSpuPalette---------------------------------------------------------

class cDvbSpuPalette {
  private:
    uint32_t palette[16];

  private:
    uint32_t yuv2rgb(uint32_t yuv_color);

  public:
    void setPalette(const uint32_t * pal);
    uint32_t getColor(uint8_t idx, uint8_t trans) const;
};

// --- cDvbSpuBitmap----------------------------------------------------------

class cDvbSpuBitmap {

  public:
  private:
    sDvbSpuRect bmpsize;
    sDvbSpuRect minsize[4];
    uint8_t *bmp;

  private:
    void putPixel(int xp, int yp, int len, uint8_t colorid);
    void putFieldData(int field, uint8_t * data, uint8_t * endp);

  public:
     cDvbSpuBitmap(sDvbSpuRect size,
                   uint8_t * fodd, uint8_t * eodd,
                   uint8_t * feven, uint8_t * eeven);
    ~cDvbSpuBitmap();

    bool getMinSize(const aDvbSpuPalDescr paldescr,
                    sDvbSpuRect & size) const;
    cBitmap *getBitmap(const aDvbSpuPalDescr paldescr,
                       const cDvbSpuPalette & pal,
                       sDvbSpuRect & size) const;
};

// --- cDvbSpuDecoder---------------------------------------------------------

class cDvbSpuDecoder:public cSpuDecoder {
  private:
    cOsd *osd;
    cMutex mutex;

    // processing state
    uint8_t *spu;
    uint32_t spupts;
    bool clean;
    bool ready;

    enum spFlag { spNONE, spHIDE, spSHOW, spMENU };
    spFlag state;

     cSpuDecoder::eScaleMode scaleMode;

    //highligh area
    bool highlight;
    sDvbSpuRect hlpsize;
    aDvbSpuPalDescr hlpDescr;

    //palette
    cDvbSpuPalette palette;

    // spu info's
    sDvbSpuRect size;
    aDvbSpuPalDescr palDescr;

    uint16_t DCSQ_offset;
    uint16_t prev_DCSQ_offset;

    cDvbSpuBitmap *spubmp;
    bool allowedShow;
  private:
    int cmdOffs(void) {
        return ((spu[2] << 8) | spu[3]);
    };
    int spuSize(void) {
        return ((spu[0] << 8) | spu[1]);
    };

    sDvbSpuRect CalcAreaSize(sDvbSpuRect fgsize, cBitmap *fgbmp, sDvbSpuRect bgsize, cBitmap *bgbmp);

  public:
    cDvbSpuDecoder();
    ~cDvbSpuDecoder();

    int setTime(uint32_t pts);

    cSpuDecoder::eScaleMode getScaleMode(void) { return scaleMode; }
    void setScaleMode(cSpuDecoder::eScaleMode ScaleMode);
    void setPalette(uint32_t * pal);
    void setHighlight(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey,
                      uint32_t palette);
    void clearHighlight(void);
    void Empty(void);
    void Hide(void);
    void Draw(void);
    bool IsVisible(void) { return osd != NULL; }
    void processSPU(uint32_t pts, uint8_t * buf, bool AllowedShow);
};

// --- cDvbSpuPalette --------------------------------------------------------

inline uint32_t cDvbSpuPalette::yuv2rgb(uint32_t yuv_color)
{
    int Y, Cb, Cr;
    int Ey, Epb, Epr;
    int Eg, Eb, Er;

    Y = (yuv_color >> 16) & 0xff;
    Cb = (yuv_color) & 0xff;
    Cr = (yuv_color >> 8) & 0xff;

    Ey = (Y - 16);
    Epb = (Cb - 128);
    Epr = (Cr - 128);
    /* ITU-R 709
       Eg = (298*Ey - 55*Epb - 137*Epr)/256;
       Eb = (298*Ey + 543*Epb)/256;
       Er = (298*Ey + 460*Epr)/256;
     */
    /* FCC ~= mediaLib */
    Eg = (298 * Ey - 100 * Epb - 208 * Epr) / 256;
    Eb = (298 * Ey + 516 * Epb) / 256;
    Er = (298 * Ey + 408 * Epr) / 256;

    if (Eg > 255)
        Eg = 255;
    if (Eg < 0)
        Eg = 0;

    if (Eb > 255)
        Eb = 255;
    if (Eb < 0)
        Eb = 0;

    if (Er > 255)
        Er = 255;
    if (Er < 0)
        Er = 0;

    return Eb | (Eg << 8) | (Er << 16);
}

inline uint32_t cDvbSpuPalette::getColor(uint8_t idx, uint8_t trans) const
{
    uint8_t t = trans == 0x0f ? 0xff : trans << 4;
    return palette[idx] | (t << 24);
}

#endif                          // __DVBSPU_H
