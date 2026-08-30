// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
#pragma once

// ============================================================================
//  ManagedWrapper.h  —  C++/CLI .NET wrapper for WhiteoutDexTextureBrowser
//
//  This header defines the managed ref classes that MaxScript accesses
//  via dotNet interop.  Compile with /clr.
//
//  MaxScript usage:
//    dotNet.loadAssembly "WhiteoutDexTextureBrowser.dll"
//    local ndx = dotNetClass "WhiteoutDex.WhiteoutDexTextureBrowser"
//    local h = ndx.MPQ_Open "C:\\path\\War3.mpq"
//    local files = ndx.MPQ_ListFiles h
//    local bmp = ndx.DecodeBLP "C:\\temp\\texture.blp"
//
//  All public methods are static (no need to instantiate from MaxScript).
//  Handles are integer indices into internal arrays.
// ============================================================================

#using <System.dll>
#using <System.Drawing.dll>

namespace WhiteoutDex {

    // ========================================================================
    //  ThumbnailEntry  —  returned from GenerateThumbnails
    // ========================================================================
    public ref class ThumbnailEntry {
    public:
        property System::String^           Path;
        property System::Drawing::Bitmap^  Thumbnail;
        property bool                      Success;
        property System::String^           Error;
    };

    // ========================================================================
    //  WhiteoutDexTextureBrowser  —  static façade class for MaxScript
    //
    //  All methods are static so MaxScript can call them directly:
    //    (dotNetClass "WhiteoutDex.WhiteoutDexTextureBrowser").MethodName(args)
    // ========================================================================
    public ref class WhiteoutDexTextureBrowser abstract sealed {
    public:
        // ── MPQ ──────────────────────────────────────────────────────────────
        //  Returns handle (≥0) on success, negative error code on failure.
        static int MPQ_Open(System::String^ path);
        static void MPQ_Close(int handle);
        static void MPQ_CloseAll();
        static int MPQ_Count();

        //  Returns array of file paths inside the archive.
        static array<System::String^>^ MPQ_ListFiles(int handle);
        static array<System::String^>^ MPQ_ListFiles(int handle,
                                                      System::String^ filter);
        //  Returns array of file paths across all open archives.
        static array<System::String^>^ MPQ_ListAllFiles();
        static array<System::String^>^ MPQ_ListAllFiles(System::String^ filter);

        //  Extract file to byte array.
        static array<System::Byte>^ MPQ_Extract(int handle,
                                                 System::String^ innerPath);
        //  Extract file to disk.  Returns true on success.
        static bool MPQ_ExtractToDisk(int handle,
                                      System::String^ innerPath,
                                      System::String^ destPath);
        //  Check if file exists in archive.
        static bool MPQ_HasFile(int handle, System::String^ innerPath);

        // ── CASC ─────────────────────────────────────────────────────────────
        //  Initialize CASC storage.  Returns log string.
        static System::String^ CASC_Open(System::String^ w3path);
        static void CASC_Close();
        static bool CASC_IsOpen();

        //  File listing.
        static array<System::String^>^ CASC_ListFiles();
        static int CASC_FileCount();
        static array<System::String^>^ CASC_SearchFiles(System::String^ pattern);
        static array<System::String^>^ CASC_SearchFilesHD(System::String^ pattern);
        static array<System::String^>^ CASC_SearchFilesSD(System::String^ pattern);
        static System::String^ CASC_GetFileTag(System::String^ cascPath);

        //  Extract file to byte array.
        static array<System::Byte>^ CASC_Extract(System::String^ cascPath);
        //  Extract file to disk.  Returns bytes written, or negative error.
        static int CASC_ExtractToDisk(System::String^ cascPath,
                                       System::String^ outputPath);
        //  Shader type detection.
        static System::String^ CASC_GetShaderType(System::String^ cascPath);

        // ── BLP/DDS Decoding ─────────────────────────────────────────────────
        //  Decode a BLP file from disk → System.Drawing.Bitmap
        static System::Drawing::Bitmap^ DecodeBLP(System::String^ filePath);
        //  Decode a BLP from byte array → Bitmap
        static System::Drawing::Bitmap^ DecodeBLPFromMemory(
            array<System::Byte>^ data);

        //  Decode a DDS file from disk → Bitmap
        static System::Drawing::Bitmap^ DecodeDDS(System::String^ filePath);
        //  Decode a DDS from byte array → Bitmap
        static System::Drawing::Bitmap^ DecodeDDSFromMemory(
            array<System::Byte>^ data);

        //  Auto-detect format (BLP or DDS) and decode → Bitmap
        static System::Drawing::Bitmap^ DecodeTexture(
            array<System::Byte>^ data);

        // ── Thumbnails ───────────────────────────────────────────────────────
        //  Generate thumbnails from MPQ archive.
        //  Returns array of ThumbnailEntry.
        static array<ThumbnailEntry^>^ GenerateThumbnailsMPQ(
            int mpqHandle,
            array<System::String^>^ fileList,
            int thumbSize);

        //  Generate thumbnails from all open MPQ archives.
        static array<ThumbnailEntry^>^ GenerateThumbnailsMPQAll(
            array<System::String^>^ fileList,
            int thumbSize);

        //  Generate thumbnails from CASC storage.
        static array<ThumbnailEntry^>^ GenerateThumbnailsCASC(
            array<System::String^>^ fileList,
            int thumbSize);

        //  Generate thumbnails from files on disk.
        static array<ThumbnailEntry^>^ GenerateThumbnailsDisk(
            array<System::String^>^ filePaths,
            int thumbSize);

        // ── Warcraft III install discovery ───────────────────────────────────
        //  Wraps WhiteoutLib's BlizzardGameFinder and classifies every
        //  Warcraft III hit by what is actually on disk: an install with a
        //  `Data` directory and no War3LegacyInstaller is Reforged, anything
        //  else is Classic.  Entries are "Reforged|<path>" / "Classic|<path>".
        static array<System::String^>^ FindWarcraftInstalls();

        //  Best single hit per flavour; empty string when nothing matched.
        static System::String^ FindWarcraftReforgedPath();
        static System::String^ FindWarcraftClassicPath();

        // ── Thread configuration ─────────────────────────────────────────────
        static void SetThreadCount(int threads);
        static int  GetThreadCount();
    };

} // namespace WhiteoutDex
