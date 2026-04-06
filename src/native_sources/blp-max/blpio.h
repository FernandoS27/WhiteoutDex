// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
#pragma once

#include <max.h>
#include <bmmlib.h>
#include <bitmap.h>

#define BLP_IO_CLASS_ID Class_ID(0x4B4C4201, 0x57433301)

#pragma pack(push, 1)
struct BlpUserData {
    unsigned int structVersion;
    int   jpegQuality;
    int   alphaBits;
    int   encoding;
    int   blpVersion;
    int   dither;
    float ditherStrength;
    BYTE  reserved[28];
};
#pragma pack(pop)

static constexpr unsigned int BLP_USERDATA_VER = 1;

class BlpBitmapIO : public BitmapIO {
public:
    BlpBitmapIO();
    ~BlpBitmapIO() override;

    // Identity
    const MCHAR* LongDesc()          override;
    const MCHAR* ShortDesc()         override;
    const MCHAR* AuthorName()        override;
    const MCHAR* CopyrightMessage()  override;
    unsigned int Version()           override;

    // Extension
    int          ExtCount()          override;
    const MCHAR* Ext(int n)          override;

    // Capabilities — returns int (bitmap.h line 1856)
    int Capability()                 override;

    // About dialog (bitmap.h line ~1867)
    void ShowAbout(HWND hWnd)        override;

    // Image info (bitmap.h line ~2228)
    BMMRES GetImageInfo(BitmapInfo* bi) override;

    // Config persistence
    DWORD EvaluateConfigure()                           override;
    BOOL  LoadConfigure(void* ptr, DWORD piDataSize)    override;
    BOOL  SaveConfigure(void* ptr)                      override;
    BOOL  ShowControl(HWND hWnd, DWORD flag)            override;

    // Load — returns BitmapStorage* (bitmap.h line 2311)
    BitmapStorage* Load(BitmapInfo* pbi, Bitmap* pmap, BMMRES* status) override;

    // Save
    BMMRES OpenOutput(BitmapInfo* pbi, Bitmap* pmap)  override;
    BMMRES Write(int frame)                           override;
    int    Close(int flag)                            override;

private:
    BlpUserData userData_{};
    Bitmap*     outMap_ = nullptr;
    BitmapInfo* outBi_  = nullptr;
    void initDefaults();
};
