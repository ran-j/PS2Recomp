#pragma once

#include <array>
#include "types.h"

// GS General Purpose Registers

// Alpha blending
// Cv = ((A-B) * C) >> 7 + D
union GSAlphaReg
{
    // A specification
    Bitfield<u64, 0, 2> a;

    // B specification
    Bitfield<u64, 2, 2> b;

    // C specification
    Bitfield<u64, 4, 2> c;

    // D specification
    Bitfield<u64, 6, 2> d;

    // Fix value
    // 0-255
    // 128 = 1.0
    Bitfield<u64, 32, 8> fix;

    u64 data{ };
};

// Buffer transmission
union GSBitBltBufReg
{
    // Source base pointer
    // word address / 64
    Bitfield<u64, 0, 14> sbp;

    // Source buffer width
    // pixels / 64
    // 1-32
    Bitfield<u64, 16, 6> sbw;

    // Source pixel storage mode
    // see: GSPsm
    Bitfield<u64, 24, 6> spsm;

    // Destination base pointer
    // word address / 64
    Bitfield<u64, 32, 14> dbp;

    // Destination buffer width
    // pixels / 64
    // 1-32
    Bitfield<u64, 48, 6> dbw;

    // Destination pixel storage mode
    // see: GSPsm
    // Different bpp from spsm is undefined behavior
    Bitfield<u64, 56, 6> dpsm;

    u64 data{ };
};

// Texture wrapping
union GSClampReg
{
    // Wrap S (horizontal)
    Bitfield<u64, 0, 2> wms;

    // Wrap T (vertical)
    Bitfield<u64, 2, 2> wmt;

    // Clamp U min (horizontal)
    Bitfield<u64, 4, 10> minu;

    // Clamp U max (horizontal)
    Bitfield<u64, 14, 10> maxu;

    // Clamp V min (vertical)
    Bitfield<u64, 24, 10> minv;

    // Clamp V max (vertical)
    Bitfield<u64, 34, 10> maxv;

    u64 data{ };
};

// Color clamping
union GSColClampReg
{
    // Color clamping mode
    Bitfield<u64, 0, 1, bool> clamp;

    u64 data{ };
};

// Dither matrix
union GSDimxReg
{
    Bitfield<u64, 0, 3, s8> dm00;
    Bitfield<u64, 4, 3, s8> dm01;
    Bitfield<u64, 8, 3, s8> dm02;
    Bitfield<u64, 12, 3, s8> dm03;
    Bitfield<u64, 16, 3, s8> dm10;
    Bitfield<u64, 20, 3, s8> dm11;
    Bitfield<u64, 24, 3, s8> dm12;
    Bitfield<u64, 28, 3, s8> dm13;
    Bitfield<u64, 32, 3, s8> dm20;
    Bitfield<u64, 36, 3, s8> dm21;
    Bitfield<u64, 40, 3, s8> dm22;
    Bitfield<u64, 44, 3, s8> dm23;
    Bitfield<u64, 48, 3, s8> dm30;
    Bitfield<u64, 52, 3, s8> dm31;
    Bitfield<u64, 56, 3, s8> dm32;
    Bitfield<u64, 60, 3, s8> dm33;

    u64 data{ };
};

// Dither enable
union GSDtheReg
{
    Bitfield<u64, 0, 1, bool> dthe;

    u64 data{ };
};

// Alpha correction
union GSFbaReg
{
    // msb of alpha
    Bitfield<u64, 0, 1> fba;

    u64 data{ };
};

// Request finish event
union GSFinishReg
{
    u64 data{ };
};

// Vertex fog
union GSFogReg
{
    // fog value
    Bitfield<u64, 56, 8> f;

    u64 data{ };
};

// Distant fog color
union GSFogColReg
{
    // red
    Bitfield<u64, 0, 8> fcr;

    // green
    Bitfield<u64, 8, 8> fcg;

    // blue
    Bitfield<u64, 16, 8> fcb;

    u64 data{ };
};

// Framebuffer
union GSFrameReg
{
    // framebuffer base pointer
    // word address / 2048
    Bitfield<u64, 0, 9> fbp;

    // framebuffer buffer width
    // pixels / 64
    Bitfield<u64, 16, 6> fbw;

    // framebuffer pixel storage format
    Bitfield<u64, 24, 6> psm;

    // framebuffer write mask
    Bitfield<u64, 32, 32> fbmsk;

    u64 data{ };
};

// Texture port
union GSHwReg
{
    u64 data{ };
};

// Request label event
union GSLabelReg
{
    Bitfield<u64, 0, 32> id;
    Bitfield<u64, 32, 32> idmsk;

    u64 data{ };
};

// Mipmap
union GSMipTbp1Reg
{
    // level 1 base pointer
    // word address / 64
    Bitfield<u64, 0, 14> tbp1;

    // level 1 buffer width
    // pixels / 64
    Bitfield<u64, 14, 6> tbw1;

    // level 2 base pointer
    // word address / 64
    Bitfield<u64, 20, 14> tbp2;

    // level 2 buffer width
    // pixels / 64
    Bitfield<u64, 34, 6> tbw2;

    // level 3 base pointer
    // word address / 64
    Bitfield<u64, 40, 14> tbp3;

    // level 3 buffer width
    // pixels / 64
    Bitfield<u64, 54, 6> tbw3;

    u64 data{ };
};

union GSMipTbp2Reg
{
    // level 4 base pointer
    // word address / 64
    Bitfield<u64, 0, 14> tbp4;

    // level 4 buffer width
    // pixels / 64
    Bitfield<u64, 14, 6> tbw4;

    // level 5 base pointer
    // word address / 64
    Bitfield<u64, 20, 14> tbp5;

    // level 5 buffer width
    // pixels / 64
    Bitfield<u64, 34, 6> tbw5;

    // level 6 base pointer
    // word address / 64
    Bitfield<u64, 40, 14> tbp6;

    // level 6 buffer width
    // pixels / 64
    Bitfield<u64, 54, 6> tbw6;

    u64 data{ };
};

// Pixel alpha blending
union GSPabeReg
{
    Bitfield<u64, 0, 1, bool> pabe;

    u64 data{ };
};

// Drawing primitive
union GSPrimReg
{
    // primitive type
    // 000 point
    // 001 line
    // 010 line strip
    // 011 triangle
    // 100 triangle strip
    // 101 triangle fan
    // 110 sprite
    // 111 reserved
    Bitfield<u64, 0, 3> prim;

    // enable gouraud shading
    Bitfield<u64, 3, 1, bool> iip;

    // enable texture mapping
    Bitfield<u64, 4, 1, bool> tme;

    // enable vertex fogging
    Bitfield<u64, 5, 1, bool> fge;

    // enable alpha blending
    Bitfield<u64, 6, 1, bool> abe;

    // enable AA
    Bitfield<u64, 7, 1, bool> aa1;

    // enable UV
    Bitfield<u64, 8, 1, bool> fst;

    // drawing context
    Bitfield<u64, 9, 1> ctxt;

    // fragment value fix
    Bitfield<u64, 10, 1, bool> fix;

    u64 data{ };
};

// Drawing primitive
union GSPrmodeReg
{
    // enable gouraud shading
    Bitfield<u64, 3, 1, bool> iip;

    // enable texture mapping
    Bitfield<u64, 4, 1, bool> tme;

    // enable vertex fogging
    Bitfield<u64, 5, 1, bool> fge;

    // enable alpha blending
    Bitfield<u64, 6, 1, bool> abe;

    // enable AA
    Bitfield<u64, 7, 1, bool> aa1;

    // enable UV
    Bitfield<u64, 8, 1, bool> fst;

    // drawing context
    Bitfield<u64, 9, 1> ctxt;

    // fragment value fix
    Bitfield<u64, 10, 1, bool> fix;

    u64 data{ };
};

// Which register to use for primitives
union GSPremodeContReg
{
    // enable the prim register (off = PRMODE)
    Bitfield<u64, 0, 1, bool> ac;

    u64 data{ };
};

// Vertex color
union GSRgbaqReg
{
    // red
    Bitfield<u64, 0, 8> r;

    // green
    Bitfield<u64, 8, 8> g;

    // blue
    Bitfield<u64, 16, 8> b;

    // alpha
    Bitfield<u64, 24, 8> a;

    // q
    Bitfield<u64, 32, 32, f32> q;

    u64 data{ };
};

// Raster masking
union GSScanMskReg
{
    // mask
    // 00 normal
    // 01 reserved
    // 10 even masked
    // 11 odd masked
    Bitfield<u64, 0, 2> msk;

    u64 data{ };
};

// Scissor
union GSScissorReg
{
    // upper left x
    Bitfield<u64, 0, 11> x0;

    // lower right x
    Bitfield<u64, 16, 11> x1;

    // upper left y
    Bitfield<u64, 32, 11> y0;

    // lower right y
    Bitfield<u64, 48, 11> y1;

    u64 data{ };
};

// Request signal event
union GSSignalReg
{
    Bitfield<u64, 0, 32> id;
    Bitfield<u64, 32, 32> idmsk;

    u64 data{ };
};

// ST texture coords
union GSStReg
{
    // S
    Bitfield<u64, 0, 32, f32> s;

    // T
    Bitfield<u64, 32, 32, f32> t;

    u64 data{ };
};

// Pixel test
union GSTestReg
{
    // alpha test enabled
    Bitfield<u64, 0, 1, bool> ate;

    // alpha test method
    Bitfield<u64, 1, 3> atst;

    // alpha test reference value
    Bitfield<u64, 4, 8> aref;

    // alpha test fail method
    Bitfield<u64, 12, 2> afail;

    // destination alpha test
    Bitfield<u64, 14, 1, bool> date;

    // destination alpha method
    Bitfield<u64, 15, 1> datm;

    // depth test
    Bitfield<u64, 16, 1> zte;

    // dpeth test method
    Bitfield<u64, 17, 2> ztst;

    u64 data{ };
};

// Texture buffer
union GSTex0Reg
{
    // texture base pointer
    // word address / 64
    Bitfield<u64, 0, 14> tbp0;

    // texture buffer width
    // pixels / 64
    Bitfield<u64, 14, 6> tbw;

    // texture pixel stoage format
    Bitfield<u64, 20, 6> psm;

    // texture width
    // log2(pixels)
    Bitfield<u64, 26, 4> tw;

    // texture height
    // log2(pixels)
    Bitfield<u64, 30, 4> th;

    // texture color component for 16/24 bit
    // 0 RGB
    // 1 RGBA
    Bitfield<u64, 34, 1> tcc;

    // texture function
    // 00 modulate
    // 01 decal
    // 10 highlight
    // 11 highlight2
    Bitfield<u64, 35, 2> tfx;

    // clut base pointer
    // word address / 64
    Bitfield<u64, 37, 14> cbp;

    // clut pixel storage format
    Bitfield<u64, 51, 4> cpsm;

    // clut storage mode
    // 0 CSM1
    // 1 CSM2
    Bitfield<u64, 55, 1> csm;

    // clut entry offset
    // pixels / 16
    // not valid for CSM2
    Bitfield<u64, 56, 5> csa;

    // clut load behavior
    // 000 nop
    // 001 load from CSA
    // 010 load from CSA (CBP0 updated)
    // 011 load from CSA (CBP1 updated)
    // 100 load from CSA if CBP != CBP0 (CBP0 updated)
    // 101 load from CSA if CBP != CBP1 (CBP1 updated)
    Bitfield<u64, 61, 3> cld;

    u64 data{ };
};

// Texture filtering
union GSTex1Reg
{
    // LOD calculation method
    Bitfield<u64, 0, 1, bool> lcm;

    // max mip level
    Bitfield<u64, 2, 3> mxl;

    // filter mode when LOD < 0
    // 0 nearest
    // 1 linear
    Bitfield<u64, 5, 1> mmag;

    // filter mode when LOD >= 0
    // 000 nearest
    // 001 linear
    // 010 nearest mipmap nearest
    // 011 nearest mipmap linear
    // 100 linear mipmap nearest
    // 101 linear mipmap linear
    Bitfield<u64, 6, 3> mmin;

    // automatic mipmap address calculation enabled
    Bitfield<u64, 9, 1, bool> mtba;

    // L
    Bitfield<u64, 19, 2> l;

    // K
    Bitfield<u64, 32, 12, s32> k;

    u64 data{ };
};

// Texture buffer
union GSTex2Reg
{
    // texture pixel storage format
    Bitfield<u64, 20, 6> psm;

    // clut base pointer
    // word address / 64
    Bitfield<u64, 37, 14> cbp;

    // clut pixel storage format
    Bitfield<u64, 51, 4> cpsm;

    // clut storage mode
    Bitfield<u64, 55, 1> csm;

    // clut offset
    Bitfield<u64, 56, 5> csa;

    // clut load behavior
    Bitfield<u64, 61, 3> cld;

    u64 data{ };
};

// Texture alpha
union GSTexaReg
{
    // As when A=0 for 16/24 bit
    Bitfield<u64, 0, 8> ta0;

    // alpha expansion mode when RGB=0
    // 0 normal
    // 1 transparent
    Bitfield<u64, 15, 1> aem;

    // As when A=1 for 16 bit
    Bitfield<u64, 32, 8> ta1;

    u64 data{ };
};

// Clut CSM2 buffer
union GSTexClutReg
{
    // clut buffer width
    // pixels / 64
    Bitfield<u64, 0, 6> cbw;

    // clut offset u
    // pixels / 16
    Bitfield<u64, 6, 6> cou;

    // clut offset v
    // pixels
    Bitfield<u64, 12, 10> cov;

    u64 data{ };
};

// Invalidate the texture buffer cache
union GSTexFlushReg
{
    u64 data{ };
};

// Activate transfer
union GSTrxDirReg
{
    // direction
    // 00 EE -> GS
    // 01 GS -> EE
    // 10 GS -> GS
    // 11 disable
    Bitfield<u64, 0, 2> xdir;

    u64 data{ };
};

// Transmission area offset
union GSTrxPosReg
{
    // source upper left x
    Bitfield<u64, 0, 11> ssax;
    
    // source upper left y
    Bitfield<u64, 16, 11> ssay;

    // destination upper left x
    Bitfield<u64, 32, 11> dsax;

    // destination upper left y
    Bitfield<u64, 48, 11> dsay;

    // direction (GS->GS)
    // 00 upper left -> lower right
    // 01 lower left -> upper right
    // 10 upper right -> lower left
    // 11 lower right -> upper left
    Bitfield<u64, 59, 2> dir;

    u64 data{ };
};

// Transmission area size
union GSTrxReg
{
    // width
    Bitfield<u64, 0, 12> rrw;

    // height
    Bitfield<u64, 32, 12> rrh;

    u64 data{ };
};

// UV texture coords
union GSUvReg
{
    // U
    Bitfield<u64, 0, 14> u;

    // V
    Bitfield<u64, 16, 14> v;

    u64 data{ };
};

// Window coord offset
union GSXYOffsetReg
{
    // x offset
    Bitfield<u64, 0, 16> ofx;

    // y offset
    Bitfield<u64, 32, 16> ofy;

    u64 data{ };
};

// Vertex coords
union GSXYZReg
{
    // x
    Bitfield<u64, 0, 16> x;

    // y
    Bitfield<u64, 16, 16> y;

    // z (32 bit)
    Bitfield<u64, 32, 32> z;

    u64 data{ };
};

// Vertex coord
union GSXYZFReg
{
    // x
    Bitfield<u64, 0, 16> x;

    // y
    Bitfield<u64, 16, 16> y;

    // z (24 bit)
    Bitfield<u64, 32, 24> z;

    // fog
    Bitfield<u64, 56, 8> f;

    u64 data{ };
};

// Depth buffer
union GSZbufReg
{
    // depth base pointer
    // word address / 2048
    Bitfield<u64, 0, 9> zbp;

    // depth pixel storage format
    Bitfield<u64, 24, 4> psm;

    // disable depth write
    Bitfield<u64, 32, 1, bool> zmsk;

    u64 data{ };
};