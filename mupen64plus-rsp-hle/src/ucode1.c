/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - ucode1.cpp                                      *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdint.h>
#include <string.h>

#include "hle.h"
#include "alist_internal.h"

/* alist state */

static struct {
    /* main buffers */
    uint16_t in;
    uint16_t out;
    uint16_t count;

    /* auxiliary buffers */
    uint16_t dry_right;
    uint16_t wet_left;
    uint16_t wet_right;

    /* gains */
    int16_t dry;
    int16_t wet;

    /* envelopes (0:left, 1:right) */
    int16_t vol[2];
    int16_t target[2];
    int32_t rate[2];

    /* ADPCM loop point address */
    uint32_t loop;

    /* storage for ADPCM table and polef coefficients */
    uint16_t table[16 * 8];
} l_alist;

//#include "rsp.h"
//#define SAFE_MEMORY
/*
#ifndef SAFE_MEMORY
#   define wr8 (src , address);
#   define rd8 (dest, address);
#   define wr16 (src, address);
#   define rd16 (dest, address);
#   define wr32 (src, address);
#   define rd32 (dest, address);
#   define wr64 (src, address);
#   define rd64 (dest, address);
#   define dmamem (dest, src, size) memcpy (dest, src, size);
#   define clrmem (dest, size)      memset (dest, 0, size);
#else
    void wr8 (uint8_t src, void *address);
    void rd8 (uint8_t dest, void *address);
    void wr16 (uint16_t src, void *address);
    void rd16 (uint16_t dest, void *address);
    void wr32 (uint16_t src, void *address);
    void rd32 (uint16_t dest, void *address);
    void wr64 (uint16_t src, void *address);
    void rd64 (uint16_t dest, void *address);
    void dmamem (void *dest, void *src, int32_t size);
    void clrmem (void *dest, int32_t size);
#endif
*/
/******** DMEM Memory Map for ABI 1 ***************
Address/Range       Description
-------------       -------------------------------
0x000..0x2BF        UCodeData
    0x000-0x00F     Constants  - 0000 0001 0002 FFFF 0020 0800 7FFF 4000
    0x010-0x02F     Function Jump Table (16 Functions * 2 bytes each = 32) 0x20
    0x030-0x03F     Constants  - F000 0F00 00F0 000F 0001 0010 0100 1000
    0x040-0x03F     Used by the Envelope Mixer (But what for?)
    0x070-0x07F     Used by the Envelope Mixer (But what for?)
0x2C0..0x31F        <Unknown>
0x320..0x35F        Segments
0x360               Audio In Buffer (Location)
0x362               Audio Out Buffer (Location)
0x364               Audio Buffer Size (Location)
0x366               Initial Volume for Left Channel
0x368               Initial Volume for Right Channel
0x36A               Auxillary Buffer #1 (Location)
0x36C               Auxillary Buffer #2 (Location)
0x36E               Auxillary Buffer #3 (Location)
0x370               Loop Value (shared location)
0x370               Target Volume (Left)
0x372               Ramp?? (Left)
0x374               Rate?? (Left)
0x376               Target Volume (Right)
0x378               Ramp?? (Right)
0x37A               Rate?? (Right)
0x37C               Dry??
0x37E               Wet??
0x380..0x4BF        Alist data
0x4C0..0x4FF        ADPCM CodeBook
0x500..0x5BF        <Unknown>
0x5C0..0xF7F        Buffers...
0xF80..0xFFF        <Unknown>
***************************************************/
#ifdef USE_EXPANSION
    #define MEMMASK 0x7FFFFF
#else
    #define MEMMASK 0x3FFFFF
#endif

static void SPNOOP (uint32_t w1, uint32_t w2) {
    //MessageBox (NULL, "Unknown Audio Command in ABI 1", "Audio HLE Error", MB_OK);
}

const uint16_t ResampleLUT [0x200] = {
    0x0C39, 0x66AD, 0x0D46, 0xFFDF, 0x0B39, 0x6696, 0x0E5F, 0xFFD8,
    0x0A44, 0x6669, 0x0F83, 0xFFD0, 0x095A, 0x6626, 0x10B4, 0xFFC8,
    0x087D, 0x65CD, 0x11F0, 0xFFBF, 0x07AB, 0x655E, 0x1338, 0xFFB6,
    0x06E4, 0x64D9, 0x148C, 0xFFAC, 0x0628, 0x643F, 0x15EB, 0xFFA1,
    0x0577, 0x638F, 0x1756, 0xFF96, 0x04D1, 0x62CB, 0x18CB, 0xFF8A,
    0x0435, 0x61F3, 0x1A4C, 0xFF7E, 0x03A4, 0x6106, 0x1BD7, 0xFF71,
    0x031C, 0x6007, 0x1D6C, 0xFF64, 0x029F, 0x5EF5, 0x1F0B, 0xFF56,
    0x022A, 0x5DD0, 0x20B3, 0xFF48, 0x01BE, 0x5C9A, 0x2264, 0xFF3A,
    0x015B, 0x5B53, 0x241E, 0xFF2C, 0x0101, 0x59FC, 0x25E0, 0xFF1E,
    0x00AE, 0x5896, 0x27A9, 0xFF10, 0x0063, 0x5720, 0x297A, 0xFF02,
    0x001F, 0x559D, 0x2B50, 0xFEF4, 0xFFE2, 0x540D, 0x2D2C, 0xFEE8,
    0xFFAC, 0x5270, 0x2F0D, 0xFEDB, 0xFF7C, 0x50C7, 0x30F3, 0xFED0,
    0xFF53, 0x4F14, 0x32DC, 0xFEC6, 0xFF2E, 0x4D57, 0x34C8, 0xFEBD,
    0xFF0F, 0x4B91, 0x36B6, 0xFEB6, 0xFEF5, 0x49C2, 0x38A5, 0xFEB0,
    0xFEDF, 0x47ED, 0x3A95, 0xFEAC, 0xFECE, 0x4611, 0x3C85, 0xFEAB,
    0xFEC0, 0x4430, 0x3E74, 0xFEAC, 0xFEB6, 0x424A, 0x4060, 0xFEAF,
    0xFEAF, 0x4060, 0x424A, 0xFEB6, 0xFEAC, 0x3E74, 0x4430, 0xFEC0,
    0xFEAB, 0x3C85, 0x4611, 0xFECE, 0xFEAC, 0x3A95, 0x47ED, 0xFEDF,
    0xFEB0, 0x38A5, 0x49C2, 0xFEF5, 0xFEB6, 0x36B6, 0x4B91, 0xFF0F,
    0xFEBD, 0x34C8, 0x4D57, 0xFF2E, 0xFEC6, 0x32DC, 0x4F14, 0xFF53,
    0xFED0, 0x30F3, 0x50C7, 0xFF7C, 0xFEDB, 0x2F0D, 0x5270, 0xFFAC,
    0xFEE8, 0x2D2C, 0x540D, 0xFFE2, 0xFEF4, 0x2B50, 0x559D, 0x001F,
    0xFF02, 0x297A, 0x5720, 0x0063, 0xFF10, 0x27A9, 0x5896, 0x00AE,
    0xFF1E, 0x25E0, 0x59FC, 0x0101, 0xFF2C, 0x241E, 0x5B53, 0x015B,
    0xFF3A, 0x2264, 0x5C9A, 0x01BE, 0xFF48, 0x20B3, 0x5DD0, 0x022A,
    0xFF56, 0x1F0B, 0x5EF5, 0x029F, 0xFF64, 0x1D6C, 0x6007, 0x031C,
    0xFF71, 0x1BD7, 0x6106, 0x03A4, 0xFF7E, 0x1A4C, 0x61F3, 0x0435,
    0xFF8A, 0x18CB, 0x62CB, 0x04D1, 0xFF96, 0x1756, 0x638F, 0x0577,
    0xFFA1, 0x15EB, 0x643F, 0x0628, 0xFFAC, 0x148C, 0x64D9, 0x06E4,
    0xFFB6, 0x1338, 0x655E, 0x07AB, 0xFFBF, 0x11F0, 0x65CD, 0x087D,
    0xFFC8, 0x10B4, 0x6626, 0x095A, 0xFFD0, 0x0F83, 0x6669, 0x0A44,
    0xFFD8, 0x0E5F, 0x6696, 0x0B39, 0xFFDF, 0x0D46, 0x66AD, 0x0C39
};

static void CLEARBUFF (uint32_t w1, uint32_t w2) {
    uint32_t addr = (uint32_t)(w1 & 0xffff);
    uint32_t count = (uint32_t)(w2 & 0xffff);
    addr &= 0xFFFC;
    memset(BufferSpace+addr, 0, (count+3)&0xFFFC);
}

//FILE *dfile = fopen ("d:\\envmix.txt", "wt");

static void ENVMIXER (uint32_t w1, uint32_t w2) {
    //static int32_t envmixcnt = 0;
    uint8_t flags = (uint8_t)((w1 >> 16) & 0xff);
    uint32_t addy = (w2 & 0xFFFFFF);// + SEGMENTS[(w2>>24)&0xf];
    //static
// ********* Make sure these conditions are met... ***********
    /*if ((l_alist.in | l_alist.out | AudioAuxA | AudioAuxC | AudioAuxE | l_alist.count) & 0x3) {
        MessageBox (NULL, "Unaligned EnvMixer... please report this to Azimer with the following information: RomTitle, Place in the rom it occurred, and any save state just before the error", "AudioHLE Error", MB_OK);
    }*/
// ------------------------------------------------------------
    int16_t *inp=(int16_t *)(BufferSpace+l_alist.in);
    int16_t *out=(int16_t *)(BufferSpace+l_alist.out);
    int16_t *aux1=(int16_t *)(BufferSpace+ l_alist.dry_right);
    int16_t *aux2=(int16_t *)(BufferSpace+ l_alist.wet_left);
    int16_t *aux3=(int16_t *)(BufferSpace+ l_alist.wet_right);
    int32_t MainR;
    int32_t MainL;
    int32_t AuxR;
    int32_t AuxL;
    int32_t i1,o1,a1,a2=0,a3=0;
    uint16_t AuxIncRate=1;
    int16_t zero[8];
    memset(zero,0,16);
    int32_t LVol, RVol;
    int32_t LAcc, RAcc;
    int32_t LTrg, RTrg;
    int16_t Wet, Dry;
    uint32_t ptr = 0;
    int32_t RRamp, LRamp;
    int32_t LAdderStart, RAdderStart, LAdderEnd, RAdderEnd;
    int32_t oMainR, oMainL, oAuxR, oAuxL;
    int32_t x, y;
    int16_t save_buffer[40];

    //envmixcnt++;

    //fprintf (dfile, "\n----------------------------------------------------\n");
    if (flags & A_INIT) {
        LVol = ((l_alist.vol[0] * (int32_t)l_alist.rate[0]));
        RVol = ((l_alist.vol[1] * (int32_t)l_alist.rate[1]));
        Wet = (int16_t)l_alist.wet;
        Dry = (int16_t)l_alist.dry; // Save Wet/Dry values
        LTrg = (l_alist.target[0] << 16); RTrg = (l_alist.target[1] << 16); // Save Current Left/Right Targets
        LAdderStart = l_alist.vol[0] << 16;
        RAdderStart = l_alist.vol[1] << 16;
        LAdderEnd = LVol;
        RAdderEnd = RVol;
        RRamp = l_alist.rate[1];
        LRamp = l_alist.rate[0];
    } else {
        // Load LVol, RVol, LAcc, and RAcc (all 32bit)
        // Load Wet, Dry, LTrg, RTrg
        memcpy((uint8_t *)save_buffer, (rspInfo.RDRAM+addy), 80);
        Wet  = *(int16_t *)(save_buffer +  0); // 0-1
        Dry  = *(int16_t *)(save_buffer +  2); // 2-3
        LTrg = *(int32_t *)(save_buffer +  4); // 4-5
        RTrg = *(int32_t *)(save_buffer +  6); // 6-7
        LRamp= *(int32_t *)(save_buffer +  8); // 8-9 (save_buffer is a 16bit pointer)
        RRamp= *(int32_t *)(save_buffer + 10); // 10-11
        LAdderEnd = *(int32_t *)(save_buffer + 12); // 12-13
        RAdderEnd = *(int32_t *)(save_buffer + 14); // 14-15
        LAdderStart = *(int32_t *)(save_buffer + 16); // 12-13
        RAdderStart = *(int32_t *)(save_buffer + 18); // 14-15
    }

    if(!(flags&A_AUX)) {
        AuxIncRate=0;
        aux2=aux3=zero;
    }

    oMainL = (Dry * (LTrg>>16) + 0x4000) >> 15;
    oAuxL  = (Wet * (LTrg>>16) + 0x4000)  >> 15;
    oMainR = (Dry * (RTrg>>16) + 0x4000) >> 15;
    oAuxR  = (Wet * (RTrg>>16) + 0x4000)  >> 15;

    for (y = 0; y < l_alist.count; y += 0x10) {

        if (LAdderStart != LTrg) {
            LAcc = LAdderStart;
            LVol = (LAdderEnd - LAdderStart) >> 3;
            LAdderEnd   = (int32_t) (((int64_t)LAdderEnd * (int64_t)LRamp) >> 16);
            LAdderStart = (int32_t) (((int64_t)LAcc * (int64_t)LRamp) >> 16);
        } else {
            LAcc = LTrg;
            LVol = 0;
        }

        if (RAdderStart != RTrg) {
            RAcc = RAdderStart;
            RVol = (RAdderEnd - RAdderStart) >> 3;
            RAdderEnd   = (int32_t) (((int64_t)RAdderEnd * (int64_t)RRamp) >> 16);
            RAdderStart = (int32_t) (((int64_t)RAcc * (int64_t)RRamp) >> 16);
        } else {
            RAcc = RTrg;
            RVol = 0;
        }

    for (x = 0; x < 8; x++) {
        i1=(int)inp[ptr^S];
        o1=(int)out[ptr^S];
        a1=(int)aux1[ptr^S];
        if (AuxIncRate) {
            a2=(int)aux2[ptr^S];
            a3=(int)aux3[ptr^S];
        }
        // TODO: here...
        //LAcc = LTrg;
        //RAcc = RTrg;

        LAcc += LVol;
        RAcc += RVol;

        if (LVol <= 0) { // Decrementing
            if (LAcc < LTrg) {
                LAcc = LTrg;
                LAdderStart = LTrg;
                MainL = oMainL;
                AuxL  = oAuxL;
            } else {
                MainL = (Dry * ((int32_t)LAcc>>16) + 0x4000) >> 15;
                AuxL  = (Wet * ((int32_t)LAcc>>16) + 0x4000)  >> 15;
            }
        } else {
            if (LAcc > LTrg) {
                LAcc = LTrg;
                LAdderStart = LTrg;
                MainL = oMainL;
                AuxL  = oAuxL;
            } else {
                MainL = (Dry * ((int32_t)LAcc>>16) + 0x4000) >> 15;
                AuxL  = (Wet * ((int32_t)LAcc>>16) + 0x4000)  >> 15;
            }
        }

        if (RVol <= 0) { // Decrementing
            if (RAcc < RTrg) {
                RAcc = RTrg;
                RAdderStart = RTrg;
                MainR = oMainR;
                AuxR  = oAuxR;
            } else {
                MainR = (Dry * ((int32_t)RAcc>>16) + 0x4000) >> 15;
                AuxR  = (Wet * ((int32_t)RAcc>>16) + 0x4000)  >> 15;
            }
        } else {
            if (RAcc > RTrg) {
                RAcc = RTrg;
                RAdderStart = RTrg;
                MainR = oMainR;
                AuxR  = oAuxR;
            } else {
                MainR = (Dry * ((int32_t)RAcc>>16) + 0x4000) >> 15;
                AuxR  = (Wet * ((int32_t)RAcc>>16) + 0x4000)  >> 15;
            }
        }

        //fprintf (dfile, "%04X ", (LAcc>>16));

        /*MainL = (((int64_t)Dry*2 * (int64_t)(LAcc>>16)) + 0x8000) >> 16;
        MainR = (((int64_t)Dry*2 * (int64_t)(RAcc>>16)) + 0x8000) >> 16;
        AuxL  = (((int64_t)Wet*2 * (int64_t)(LAcc>>16)) + 0x8000) >> 16;
        AuxR  = (((int64_t)Wet*2 * (int64_t)(RAcc>>16)) + 0x8000) >> 16;*/
/*
        if (MainL>32767) MainL = 32767;
        else if (MainL<-32768) MainL = -32768;
        if (MainR>32767) MainR = 32767;
        else if (MainR<-32768) MainR = -32768;
        if (AuxL>32767) AuxL = 32767;
        else if (AuxL<-32768) AuxR = -32768;
        if (AuxR>32767) AuxR = 32767;
        else if (AuxR<-32768) AuxR = -32768;*/
        /*
        MainR = (Dry * RTrg + 0x10000) >> 15;
        MainL = (Dry * LTrg + 0x10000) >> 15;
        AuxR  = (Wet * RTrg + 0x8000)  >> 16;
        AuxL  = (Wet * LTrg + 0x8000)  >> 16;*/

        o1+=(/*(o1*0x7fff)+*/(i1*MainR)+0x4000)>>15;
        a1+=(/*(a1*0x7fff)+*/(i1*MainL)+0x4000)>>15;

/*      o1=((int64_t)(((int64_t)o1*0xfffe)+((int64_t)i1*MainR*2)+0x8000)>>16);

        a1=((int64_t)(((int64_t)a1*0xfffe)+((int64_t)i1*MainL*2)+0x8000)>>16);*/

        BLARGG_CLAMP16(o1);

        BLARGG_CLAMP16(a1);

        out[ptr^S]=o1;
        aux1[ptr^S]=a1;
        if (AuxIncRate) {
            //a2=((int64_t)(((int64_t)a2*0xfffe)+((int64_t)i1*AuxR*2)+0x8000)>>16);

            //a3=((int64_t)(((int64_t)a3*0xfffe)+((int64_t)i1*AuxL*2)+0x8000)>>16);
            a2+=(/*(a2*0x7fff)+*/(i1*AuxR)+0x4000)>>15;
            a3+=(/*(a3*0x7fff)+*/(i1*AuxL)+0x4000)>>15;

            BLARGG_CLAMP16(a2);
            BLARGG_CLAMP16(a3);

            aux2[ptr^S]=a2;
            aux3[ptr^S]=a3;
        }
        ptr++;
    }
    }

    /*LAcc = LAdderEnd;
    RAcc = RAdderEnd;*/

    *(int16_t *)(save_buffer +  0) = Wet; // 0-1
    *(int16_t *)(save_buffer +  2) = Dry; // 2-3
    *(int32_t *)(save_buffer +  4) = LTrg; // 4-5
    *(int32_t *)(save_buffer +  6) = RTrg; // 6-7
    *(int32_t *)(save_buffer +  8) = LRamp; // 8-9 (save_buffer is a 16bit pointer)
    *(int32_t *)(save_buffer + 10) = RRamp; // 10-11
    *(int32_t *)(save_buffer + 12) = LAdderEnd; // 12-13
    *(int32_t *)(save_buffer + 14) = RAdderEnd; // 14-15
    *(int32_t *)(save_buffer + 16) = LAdderStart; // 12-13
    *(int32_t *)(save_buffer + 18) = RAdderStart; // 14-15
    memcpy(rspInfo.RDRAM+addy, (uint8_t *)save_buffer,80);
}

static void RESAMPLE (uint32_t w1, uint32_t w2)
{
   int32_t i ,x;
    uint8_t Flags=(uint8_t)((w1>>16)&0xff);
    uint32_t Pitch=((w1&0xffff))<<1;
    uint32_t addy = (w2 & 0xffffff);// + SEGMENTS[(w2>>24)&0xf];
    uint32_t Accum=0;
    uint32_t location;
    int16_t *lut/*, *lut2*/;
    int16_t *dst;
    int16_t *src;
    dst=(int16_t *)(BufferSpace);
    src=(int16_t *)(BufferSpace);
    uint32_t srcPtr=(l_alist.in/2);
    uint32_t dstPtr=(l_alist.out/2);
    int32_t temp;
    int32_t accum;
/*
    if (addy > (1024*1024*8))
        addy = (w2 & 0xffffff);
*/
    srcPtr -= 4;

    if ((Flags & 0x1) == 0) {
        //memcpy (src+srcPtr, rspInfo.RDRAM+addy, 0x8);
        for (x=0; x < 4; x++)
            src[(srcPtr+x)^S] = ((uint16_t *)rspInfo.RDRAM)[((addy/2)+x)^S];
        Accum = *(uint16_t *)(rspInfo.RDRAM+addy+10);
    } else {
        for (x=0; x < 4; x++)
            src[(srcPtr+x)^S] = 0;//*(uint16_t *)(rspInfo.RDRAM+((addy+x)^2));
    }

    for(i=0;i < ((l_alist.count+0xf)&0xFFF0)/2;i++)    {
        //location = (((Accum * 0x40) >> 0x10) * 8);
       // location is the fractional position between two samples
        location = (Accum >> 0xa) * 4;
        lut = (int16_t*)ResampleLUT + location;

        // mov eax, dword ptr [src+srcPtr];
        // movsx edx, word ptr [lut];
        // shl edx, 1
        // imul edx
        // test eax, 08000h
        // setz ecx
        // shl ecx, 16
        // xor eax, 08000h
        // add eax, ecx
        // and edx, 0f000h

        // imul
        temp =  ((int32_t)*(int16_t*)(src+((srcPtr+0)^S))*((int32_t)((int16_t)lut[0])));
        accum = (int32_t)(temp >> 15);

        temp = ((int32_t)*(int16_t*)(src+((srcPtr+1)^S))*((int32_t)((int16_t)lut[1])));
        accum += (int32_t)(temp >> 15);

        temp = ((int32_t)*(int16_t*)(src+((srcPtr+2)^S))*((int32_t)((int16_t)lut[2])));
        accum += (int32_t)(temp >> 15);

        temp = ((int32_t)*(int16_t*)(src+((srcPtr+3)^S))*((int32_t)((int16_t)lut[3])));
        accum += (int32_t)(temp >> 15);

        BLARGG_CLAMP16(accum);

        dst[dstPtr^S] = (accum);
        dstPtr++;
        Accum += Pitch;
        srcPtr += (Accum>>16);
        Accum&=0xffff;
    }
    for (x=0; x < 4; x++)
        ((uint16_t *)rspInfo.RDRAM)[((addy/2)+x)^S] = src[(srcPtr+x)^S];
    //memcpy (RSWORK, src+srcPtr, 0x8);
    *(uint16_t *)(rspInfo.RDRAM+addy+10) = Accum;
}

static void SETVOL (uint32_t w1, uint32_t w2) {
// Might be better to unpack these depending on the flags...
    uint8_t flags = (uint8_t)((w1 >> 16) & 0xff);
    uint16_t vol = (int16_t)(w1 & 0xffff);
    //uint16_t voltarg =(uint16_t)((w2 >> 16)&0xffff);
    uint16_t volrate = (uint16_t)((w2 & 0xffff));

    if (flags & A_AUX) {
        l_alist.dry = (int16_t)vol;         // m_MainVol
        l_alist.wet = (int16_t)volrate;     // m_AuxVol
        return;
    }

    if(flags & A_VOL) { // Set the Source(start) Volumes
        if(flags & A_LEFT) {
            l_alist.vol[0] = (int16_t)vol;    // m_LeftVolume
        } else { // A_RIGHT
            l_alist.vol[1] = (int16_t)vol;   // m_RightVolume
        }
        return;
    }

//0x370             Loop Value (shared location)
//0x370             Target Volume (Left)
//uint16_t l_alist.rate[0]; // 0x0012(T8)
    if(flags & A_LEFT) { // Set the Ramping values Target, Ramp
        //l_alist.loop = (((uint32_t)vol << 0x10) | (uint32_t)voltarg);
        l_alist.target[0]  = (int16_t)w1;      // m_LeftVol
        //l_alist.rate[0] = (int32_t)w2;
        l_alist.rate[0] = (int32_t)w2;//(uint16_t)(w2) | (int32_t)(int16_t)(w2 << 0x10);
        //fprintf (dfile, "Ramp Left: %f\n", (float)l_alist.rate[0]/65536.0);
        //fprintf (dfile, "Ramp Left: %08X\n", w2);
        //l_alist.rate[0] = (int16_t)voltarg;  // m_LeftVolTarget
        //VolRate_Left = (int16_t)volrate;  // m_LeftVolRate
    } else { // A_RIGHT
        l_alist.target[1]  = (int16_t)w1;     // m_RightVol
        //l_alist.rate[1] = (int32_t)w2;
        l_alist.rate[1] = (int32_t)w2;//(uint16_t)(w2 >> 0x10) | (int32_t)(int16_t)(w2 << 0x10);
        //fprintf (dfile, "Ramp Right: %f\n", (float)l_alist.rate[1]/65536.0);
        //fprintf (dfile, "Ramp Right: %08X\n", w2);
        //l_alist.rate[1] = (int16_t)voltarg; // m_RightVolTarget
        //VolRate_Right = (int16_t)volrate; // m_RightVolRate
    }
}

static void UNKNOWN (uint32_t w1, uint32_t w2) {}

static void SETLOOP (uint32_t w1, uint32_t w2) {
    l_alist.loop = (w2 & 0xffffff);// + SEGMENTS[(w2>>24)&0xf];
    //l_alist.target[0]  = (int16_t)(l_alist.loop >> 16);        // m_LeftVol
    //l_alist.rate[0] = (int16_t)(l_alist.loop);    // m_LeftVolTarget
}

static void ADPCM (uint32_t w1, uint32_t w2) { // Work in progress! :)
    uint8_t Flags=(uint8_t)(w1>>16)&0xff;
    //uint16_t Gain=(uint16_t)(w1&0xffff);
    uint32_t Address=(w2 & 0xffffff);// + SEGMENTS[(w2>>24)&0xf];
    uint16_t inPtr=0;
    //int16_t *out=(int16_t *)(testbuff+(l_alist.out>>2));
    int16_t *out=(int16_t *)(BufferSpace+l_alist.out);
    //uint8_t *in=(uint8_t*)(BufferSpace+l_alist.in);
    int16_t count=(int16_t)l_alist.count;
    uint8_t icode;
    uint8_t code;
    int32_t vscale;
    uint16_t index;
    uint16_t j;
    int32_t a[8];
    int16_t *book1,*book2;
/*
    if (Address > (1024*1024*8))
        Address = (w2 & 0xffffff);
*/
    memset(out,0,32);

    if(!(Flags&0x1))
    {
        if(Flags&0x2) {
            memcpy(out,&rspInfo.RDRAM[l_alist.loop & MEMMASK],32);
        } else {
            memcpy(out,&rspInfo.RDRAM[Address],32);
        }
    }

    int32_t l1=out[14^S];
    int32_t l2=out[15^S];
    int32_t inp1[8];
    int32_t inp2[8];
    out+=16;
    while(count>0)
    {
                                                    // the first interation through, these values are
                                                    // either 0 in the case of A_INIT, from a special
                                                    // area of memory in the case of A_LOOP or just
                                                    // the values we calculated the last time

        code=BufferSpace[(l_alist.in+inPtr)^S8];
        index=code&0xf;
        index<<=4;                                  // index into the adpcm code table
        book1=(int16_t *)&l_alist.table[index];
        book2=book1+8;
        code>>=4;                                   // upper nibble is scale
        vscale=(0x8000>>((12-code)-1));         // very strange. 0x8000 would be .5 in 16:16 format
                                                    // so this appears to be a fractional scale based
                                                    // on the 12 based inverse of the scale value.  note
                                                    // that this could be negative, in which case we do
                                                    // not use the calculated vscale value... see the
                                                    // if(code>12) check below

        inPtr++;                                    // coded adpcm data lies next
        j=0;
        while(j<8)                                  // loop of 8, for 8 coded nibbles from 4 bytes
                                                    // which yields 8 int16_t pcm values
        {
            icode=BufferSpace[(l_alist.in+inPtr)^S8];
            inPtr++;

            inp1[j]=(int16_t)((icode&0xf0)<<8);         // this will in effect be signed
            if(code<12)
                inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
            /*else
                int32_t catchme=1;*/
            j++;

            inp1[j]=(int16_t)((icode&0xf)<<12);
            if(code<12)
                inp1[j]=((int)((int)inp1[j]*(int)vscale)>>16);
            /*else
                int32_t catchme=1;*/
            j++;
        }
        j=0;
        while(j<8)
        {
            icode=BufferSpace[(l_alist.in+inPtr)^S8];
            inPtr++;

            inp2[j]=(int16_t)((icode&0xf0)<<8);           // this will in effect be signed
            if(code<12)
                inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
            /*else
                int32_t catchme=1;*/
            j++;

            inp2[j]=(int16_t)((icode&0xf)<<12);
            if(code<12)
                inp2[j]=((int)((int)inp2[j]*(int)vscale)>>16);
            /*else
                int32_t catchme=1;*/
            j++;
        }

        a[0]= (int)book1[0]*(int)l1;
        a[0]+=(int)book2[0]*(int)l2;
        a[0]+=(int)inp1[0]*(int)2048;

        a[1] =(int)book1[1]*(int)l1;
        a[1]+=(int)book2[1]*(int)l2;
        a[1]+=(int)book2[0]*inp1[0];
        a[1]+=(int)inp1[1]*(int)2048;

        a[2] =(int)book1[2]*(int)l1;
        a[2]+=(int)book2[2]*(int)l2;
        a[2]+=(int)book2[1]*inp1[0];
        a[2]+=(int)book2[0]*inp1[1];
        a[2]+=(int)inp1[2]*(int)2048;

        a[3] =(int)book1[3]*(int)l1;
        a[3]+=(int)book2[3]*(int)l2;
        a[3]+=(int)book2[2]*inp1[0];
        a[3]+=(int)book2[1]*inp1[1];
        a[3]+=(int)book2[0]*inp1[2];
        a[3]+=(int)inp1[3]*(int)2048;

        a[4] =(int)book1[4]*(int)l1;
        a[4]+=(int)book2[4]*(int)l2;
        a[4]+=(int)book2[3]*inp1[0];
        a[4]+=(int)book2[2]*inp1[1];
        a[4]+=(int)book2[1]*inp1[2];
        a[4]+=(int)book2[0]*inp1[3];
        a[4]+=(int)inp1[4]*(int)2048;

        a[5] =(int)book1[5]*(int)l1;
        a[5]+=(int)book2[5]*(int)l2;
        a[5]+=(int)book2[4]*inp1[0];
        a[5]+=(int)book2[3]*inp1[1];
        a[5]+=(int)book2[2]*inp1[2];
        a[5]+=(int)book2[1]*inp1[3];
        a[5]+=(int)book2[0]*inp1[4];
        a[5]+=(int)inp1[5]*(int)2048;

        a[6] =(int)book1[6]*(int)l1;
        a[6]+=(int)book2[6]*(int)l2;
        a[6]+=(int)book2[5]*inp1[0];
        a[6]+=(int)book2[4]*inp1[1];
        a[6]+=(int)book2[3]*inp1[2];
        a[6]+=(int)book2[2]*inp1[3];
        a[6]+=(int)book2[1]*inp1[4];
        a[6]+=(int)book2[0]*inp1[5];
        a[6]+=(int)inp1[6]*(int)2048;

        a[7] =(int)book1[7]*(int)l1;
        a[7]+=(int)book2[7]*(int)l2;
        a[7]+=(int)book2[6]*inp1[0];
        a[7]+=(int)book2[5]*inp1[1];
        a[7]+=(int)book2[4]*inp1[2];
        a[7]+=(int)book2[3]*inp1[3];
        a[7]+=(int)book2[2]*inp1[4];
        a[7]+=(int)book2[1]*inp1[5];
        a[7]+=(int)book2[0]*inp1[6];
        a[7]+=(int)inp1[7]*(int)2048;

        for(j=0;j<8;j++)
        {
            a[j^S]>>=11;
            BLARGG_CLAMP16(a[j^S]);
            *(out++)=a[j^S];
        }
        l1=a[6];
        l2=a[7];

        a[0]= (int)book1[0]*(int)l1;
        a[0]+=(int)book2[0]*(int)l2;
        a[0]+=(int)inp2[0]*(int)2048;

        a[1] =(int)book1[1]*(int)l1;
        a[1]+=(int)book2[1]*(int)l2;
        a[1]+=(int)book2[0]*inp2[0];
        a[1]+=(int)inp2[1]*(int)2048;

        a[2] =(int)book1[2]*(int)l1;
        a[2]+=(int)book2[2]*(int)l2;
        a[2]+=(int)book2[1]*inp2[0];
        a[2]+=(int)book2[0]*inp2[1];
        a[2]+=(int)inp2[2]*(int)2048;

        a[3] =(int)book1[3]*(int)l1;
        a[3]+=(int)book2[3]*(int)l2;
        a[3]+=(int)book2[2]*inp2[0];
        a[3]+=(int)book2[1]*inp2[1];
        a[3]+=(int)book2[0]*inp2[2];
        a[3]+=(int)inp2[3]*(int)2048;

        a[4] =(int)book1[4]*(int)l1;
        a[4]+=(int)book2[4]*(int)l2;
        a[4]+=(int)book2[3]*inp2[0];
        a[4]+=(int)book2[2]*inp2[1];
        a[4]+=(int)book2[1]*inp2[2];
        a[4]+=(int)book2[0]*inp2[3];
        a[4]+=(int)inp2[4]*(int)2048;

        a[5] =(int)book1[5]*(int)l1;
        a[5]+=(int)book2[5]*(int)l2;
        a[5]+=(int)book2[4]*inp2[0];
        a[5]+=(int)book2[3]*inp2[1];
        a[5]+=(int)book2[2]*inp2[2];
        a[5]+=(int)book2[1]*inp2[3];
        a[5]+=(int)book2[0]*inp2[4];
        a[5]+=(int)inp2[5]*(int)2048;

        a[6] =(int)book1[6]*(int)l1;
        a[6]+=(int)book2[6]*(int)l2;
        a[6]+=(int)book2[5]*inp2[0];
        a[6]+=(int)book2[4]*inp2[1];
        a[6]+=(int)book2[3]*inp2[2];
        a[6]+=(int)book2[2]*inp2[3];
        a[6]+=(int)book2[1]*inp2[4];
        a[6]+=(int)book2[0]*inp2[5];
        a[6]+=(int)inp2[6]*(int)2048;

        a[7] =(int)book1[7]*(int)l1;
        a[7]+=(int)book2[7]*(int)l2;
        a[7]+=(int)book2[6]*inp2[0];
        a[7]+=(int)book2[5]*inp2[1];
        a[7]+=(int)book2[4]*inp2[2];
        a[7]+=(int)book2[3]*inp2[3];
        a[7]+=(int)book2[2]*inp2[4];
        a[7]+=(int)book2[1]*inp2[5];
        a[7]+=(int)book2[0]*inp2[6];
        a[7]+=(int)inp2[7]*(int)2048;

        for(j=0;j<8;j++)
        {
            a[j^S]>>=11;
            BLARGG_CLAMP16(a[j^S]);
            *(out++)=a[j^S];
        }
        l1=a[6];
        l2=a[7];

        count-=32;
    }
    out-=16;
    memcpy(&rspInfo.RDRAM[Address],out,32);
}

static void LOADBUFF (uint32_t w1, uint32_t w2) { // memcpy causes static... endianess issue :(
    uint32_t v0;
    //uint32_t cnt;
    if (l_alist.count == 0)
        return;
    v0 = (w2 & 0xfffffc);// + SEGMENTS[(w2>>24)&0xf];
    memcpy (BufferSpace+(l_alist.in&0xFFFC), rspInfo.RDRAM+v0, (l_alist.count+3)&0xFFFC);
}

static void SAVEBUFF (uint32_t w1, uint32_t w2) { // memcpy causes static... endianess issue :(
    uint32_t v0;
    //uint32_t cnt;
    if (l_alist.count == 0)
        return;
    v0 = (w2 & 0xfffffc);// + SEGMENTS[(w2>>24)&0xf];
    memcpy (rspInfo.RDRAM+v0, BufferSpace+(l_alist.out&0xFFFC), (l_alist.count+3)&0xFFFC);
}

static void SETBUFF (uint32_t w1, uint32_t w2) { // Should work ;-)
    if ((w1 >> 0x10) & 0x8) { // A_AUX - Auxillary Sound Buffer Settings
        l_alist.dry_right      = (uint16_t)(w1);
        l_alist.wet_left       = (uint16_t)((w2 >> 0x10));
        l_alist.wet_right      = (uint16_t)(w2);
    } else {        // A_MAIN - Main Sound Buffer Settings
        l_alist.in   = (uint16_t)(w1); // 0x00
        l_alist.out  = (uint16_t)((w2 >> 0x10)); // 0x02
        l_alist.count      = (uint16_t)(w2); // 0x04
    }
}

static void DMEMMOVE (uint32_t w1, uint32_t w2) { // Doesn't sound just right?... will fix when HLE is ready - 03-11-01
    uint32_t v0, v1;
    uint32_t cnt;
    if ((w2 & 0xffff)==0)
        return;
    v0 = (w1 & 0xFFFF);
    v1 = (w2 >> 0x10);
    //assert ((v1 & 0x3) == 0);
    //assert ((v0 & 0x3) == 0);
    uint32_t count = ((w2+3) & 0xfffc);
    //v0 = (v0) & 0xfffc;
    //v1 = (v1) & 0xfffc;

    //memcpy (BufferSpace+v1, BufferSpace+v0, count-1);
    for (cnt = 0; cnt < count; cnt++) {
        *(uint8_t *)(BufferSpace+((cnt+v1)^S8)) = *(uint8_t *)(BufferSpace+((cnt+v0)^S8));
    }
}

static void LOADADPCM (uint32_t w1, uint32_t w2)
{ // Loads an ADPCM table - Works 100% Now 03-13-01
   uint32_t x;
    uint32_t v0;
    v0 = (w2 & 0xffffff);// + SEGMENTS[(w2>>24)&0xf];
/*  if (v0 > (1024*1024*8))
        v0 = (w2 & 0xffffff);*/
    //memcpy (dmem+0x4c0, rspInfo.RDRAM+v0, w1&0xffff); // Could prolly get away with not putting this in dmem
    //assert ((w1&0xffff) <= 0x80);
    uint16_t *table = (uint16_t *)(rspInfo.RDRAM+v0);
    for (x = 0; x < ((w1&0xffff)>>0x4); x++) {
        l_alist.table[(0x0+(x<<3))^S] = table[0];
        l_alist.table[(0x1+(x<<3))^S] = table[1];

        l_alist.table[(0x2+(x<<3))^S] = table[2];
        l_alist.table[(0x3+(x<<3))^S] = table[3];

        l_alist.table[(0x4+(x<<3))^S] = table[4];
        l_alist.table[(0x5+(x<<3))^S] = table[5];

        l_alist.table[(0x6+(x<<3))^S] = table[6];
        l_alist.table[(0x7+(x<<3))^S] = table[7];
        table += 8;
    }
}


static void INTERLEAVE (uint32_t w1, uint32_t w2)
{ // Works... - 3-11-01
   int32_t x;
    uint32_t inL, inR;
    uint16_t *outbuff = (uint16_t *)(l_alist.out+BufferSpace);
    uint16_t *inSrcR;
    uint16_t *inSrcL;
    uint16_t Left, Right, Left2, Right2;

    inL = w2 & 0xFFFF;
    inR = (w2 >> 16) & 0xFFFF;

    inSrcR = (uint16_t *)(BufferSpace+inR);
    inSrcL = (uint16_t *)(BufferSpace+inL);

    for (x = 0; x < (l_alist.count/4); x++) {
        Left=*(inSrcL++);
        Right=*(inSrcR++);
        Left2=*(inSrcL++);
        Right2=*(inSrcR++);

#ifdef M64P_BIG_ENDIAN
        *(outbuff++)=Right;
        *(outbuff++)=Left;
        *(outbuff++)=Right2;
        *(outbuff++)=Left2;
#else
        *(outbuff++)=Right2;
        *(outbuff++)=Left2;
        *(outbuff++)=Right;
        *(outbuff++)=Left;
#endif
    }
}


static void MIXER (uint32_t w1, uint32_t w2)
{ // Fixed a sign issue... 03-14-01
   int32_t x;
    uint32_t dmemin  = (uint16_t)(w2 >> 0x10);
    uint32_t dmemout = (uint16_t)(w2 & 0xFFFF);
    //uint8_t  flags   = (uint8_t)((w1 >> 16) & 0xff);
    int32_t gain    = (int16_t)(w1 & 0xFFFF);
    int32_t temp;

    if (l_alist.count == 0)
        return;

    for (x = 0; x < l_alist.count; x+=2) { // I think I can do this a lot easier
        temp = (*(int16_t *)(BufferSpace+dmemin+x) * gain) >> 15;
        temp += *(int16_t *)(BufferSpace+dmemout+x);

        BLARGG_CLAMP16(temp);

        *(uint16_t *)(BufferSpace+dmemout+x) = (uint16_t)(temp & 0xFFFF);
    }
}

// TOP Performance Hogs:
//Command: ADPCM    - Calls:  48 - Total Time: 331226 - Avg Time:  6900.54 - Percent: 31.53%
//Command: ENVMIXER - Calls:  48 - Total Time: 408563 - Avg Time:  8511.73 - Percent: 38.90%
//Command: LOADBUFF - Calls:  56 - Total Time:  21551 - Avg Time:   384.84 - Percent:  2.05%
//Command: RESAMPLE - Calls:  48 - Total Time: 225922 - Avg Time:  4706.71 - Percent: 21.51%

//Command: ADPCM    - Calls:  48 - Total Time: 391600 - Avg Time:  8158.33 - Percent: 32.52%
//Command: ENVMIXER - Calls:  48 - Total Time: 444091 - Avg Time:  9251.90 - Percent: 36.88%
//Command: LOADBUFF - Calls:  58 - Total Time:  29945 - Avg Time:   516.29 - Percent:  2.49%
//Command: RESAMPLE - Calls:  48 - Total Time: 276354 - Avg Time:  5757.38 - Percent: 22.95%


static const acmd_callback_t ABI1[0x10] = { // TOP Performace Hogs: MIXER, RESAMPLE, ENVMIXER
    SPNOOP , ADPCM , CLEARBUFF, ENVMIXER  , LOADBUFF, RESAMPLE  , SAVEBUFF, UNKNOWN,
    SETBUFF, SETVOL, DMEMMOVE , LOADADPCM , MIXER   , INTERLEAVE, UNKNOWN , SETLOOP
};

/* global functions */
void alist_process_audio(void)
{
    alist_process(ABI1, 0x10);
}

void alist_process_audio_ge(void)
{
    alist_process(ABI1, 0x10);
}

void alist_process_audio_bc(void)
{
    alist_process(ABI1, 0x10);
}

/*  BACKUPS
void MIXER (uint32_t w1, uint32_t w2) { // Fixed a sign issue... 03-14-01
    uint16_t dmemin  = (uint16_t)(w2 >> 0x10);
    uint16_t dmemout = (uint16_t)(w2 & 0xFFFF);
    uint16_t gain    = (uint16_t)(w1 & 0xFFFF);
    uint8_t  flags   = (uint8_t)((w1 >> 16) & 0xff);
    uint64_t temp;

    if (l_alist.count == 0)
        return;

    for (int32_t x=0; x < l_alist.count; x+=2) { // I think I can do this a lot easier
        temp = (int64_t)(*(int16_t *)(BufferSpace+dmemout+x)) * (int64_t)((int16_t)(0x7FFF)*2);

        if (temp & 0x8000)
            temp = (temp^0x8000) + 0x10000;
        else
            temp = (temp^0x8000);

        temp = (temp & 0xFFFFFFFFFFFF);

        temp += ((*(int16_t *)(BufferSpace+dmemin+x) * (int64_t)((int16_t)gain*2))) & 0xFFFFFFFFFFFF;
            
        temp = (int32_t)(temp >> 16);
        if ((int32_t)temp > 32767) 
            temp = 32767;
        if ((int32_t)temp < -32768) 
            temp = -32768;

        *(uint16_t *)(BufferSpace+dmemout+x) = (uint16_t)(temp & 0xFFFF);
    }
}
*/


