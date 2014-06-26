/*
 * SPU Decoder Prototype
 *
 * Copyright (C) 2001.2002 Andreas Schultz <aschultz@warp10.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 * $Id: spu.h 1.5 2006/04/17 12:48:55 kls Exp $
 */

#ifndef __SPU_VDR_H
#define __SPU_VDR_H

#include <inttypes.h>

// --- cSpuDecoder -----------------------------------------------------------

class cSpuDecoder {
  public:
    typedef enum { eSpuNormal, eSpuLetterBox, eSpuPanAndScan } eScaleMode;
  public:
    //    cSpuDecoder();
    virtual ~cSpuDecoder();

    virtual int setTime(uint32_t pts) = 0;

    virtual cSpuDecoder::eScaleMode getScaleMode(void) = 0;
    virtual void setScaleMode(cSpuDecoder::eScaleMode ScaleMode) = 0;
    virtual void setPalette(uint32_t * pal) = 0;
    virtual void setHighlight(uint16_t sx, uint16_t sy,
                              uint16_t ex, uint16_t ey,
                              uint32_t palette) = 0;
    virtual void clearHighlight(void) = 0;
    virtual void Empty(void) = 0;
    virtual void Hide(void) = 0;
    virtual void Draw(void) = 0;
    virtual bool IsVisible(void) = 0;
    virtual void processSPU(uint32_t pts, uint8_t * buf, bool AllowedShow = true) = 0;
};

#endif                          // __SPU_VDR_H
