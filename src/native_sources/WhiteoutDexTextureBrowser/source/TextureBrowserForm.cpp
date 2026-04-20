// SPDX-License-Identifier: BSD-3-Clause
#include "TextureBrowserForm.h"
#include "ManagedWrapper.h"

#include <windows.h>
#undef GetTempPath

#define LVM_FIRST            0x1000
#define LVM_SETICONSPACING   (LVM_FIRST + 53)

#pragma managed(push, off)
static void NativeSetIconSpacing(void* hwnd, int cx, int cy)
{
    ::SendMessageW((HWND)hwnd, LVM_SETICONSPACING, 0, MAKELPARAM(cx, cy));
}
#pragma managed(pop)

namespace WhiteoutDex {

    using namespace System;
    using namespace System::Drawing;
    using namespace System::Drawing::Imaging;
    using namespace System::Windows::Forms;
    using namespace System::IO;
    using namespace System::Collections::Generic;
    using namespace System::Security::Cryptography;
    using namespace System::Text;

    TextureBrowserForm::TextureBrowserForm()
    {
        allPaths_       = gcnew List<String^>();
        filteredPaths_  = gcnew List<String^>();
        thumbMap_       = gcnew Dictionary<int,int>();
        thumbsLoaded_   = 0;
        selectedPath_   = "";
        selectedSource_ = "";
        thumbSize_      = 64;
        batchSize_      = 200;
        isLoading_      = false;
        currentSourceIdx_ = 0;
        isListView_     = false;
        slotFilter_     = "";
        initialCategory_ = 0;

        tempDir_ = Path::Combine(Path::GetTempPath(), "WhiteoutDex_TexBrowser");
        Directory::CreateDirectory(tempDir_);

        cacheDir_ = Path::Combine(Path::GetTempPath(), "WhiteoutDex_ThumbCache");
        Directory::CreateDirectory(cacheDir_);

        InitializeUI();
    }

    TextureBrowserForm::~TextureBrowserForm()
    {
        if (bgWorker_ != nullptr && bgWorker_->IsBusy)
            bgWorker_->CancelAsync();
    }

    void TextureBrowserForm::InitializeUI()
    {
        this->Text = "WhiteoutDex Texture Browser";
        this->Size = Drawing::Size(880, 680);
        this->StartPosition = FormStartPosition::CenterScreen;
        this->BackColor = Color::FromArgb(30, 30, 30);
        this->ForeColor = Color::FromArgb(210, 210, 210);
        this->Font = gcnew Drawing::Font("Segoe UI", 9);
        this->MinimumSize = Drawing::Size(650, 400);

        // ── Top Panel ────────────────────────────────────────────────
        topPanel_ = gcnew Panel();
        topPanel_->Dock = DockStyle::Top;
        topPanel_->Height = 110;
        topPanel_->BackColor = Color::FromArgb(38, 38, 38);

        // ── Row 1: Search ────────────────────────────────────────────
        int y1 = 10;
        auto lblSearch = gcnew Label();
        lblSearch->Text = "Search:";
        lblSearch->Location = Point(14, y1 + 3);
        lblSearch->AutoSize = true;

        searchBox_ = gcnew TextBox();
        searchBox_->Location = Point(75, y1);
        searchBox_->Width = 480;
        searchBox_->BackColor = Color::FromArgb(50, 50, 50);
        searchBox_->ForeColor = Color::FromArgb(230, 230, 230);
        searchBox_->BorderStyle = BorderStyle::FixedSingle;
        searchBox_->KeyDown += gcnew KeyEventHandler(this, &TextureBrowserForm::OnSearchKeyDown);

        btnSearch_ = gcnew Button();
        btnSearch_->Text = "Search";
        btnSearch_->Location = Point(570, y1 - 1);
        btnSearch_->Size = Drawing::Size(80, 26);
        btnSearch_->FlatStyle = FlatStyle::Flat;
        btnSearch_->BackColor = Color::FromArgb(50, 90, 145);
        btnSearch_->ForeColor = Color::White;
        btnSearch_->Click += gcnew EventHandler(this, &TextureBrowserForm::OnSearchClick);

        // ── Row 2: Source, Format, Category ──────────────────────────
        int y2 = 42;

        auto lblSrc = gcnew Label();
        lblSrc->Text = "Source:";
        lblSrc->Location = Point(14, y2 + 3);
        lblSrc->AutoSize = true;

        cmbSource_ = gcnew ComboBox();
        cmbSource_->Location = Point(75, y2);
        cmbSource_->Width = 80;
        cmbSource_->DropDownStyle = ComboBoxStyle::DropDownList;
        cmbSource_->BackColor = Color::FromArgb(50, 50, 50);
        cmbSource_->ForeColor = Color::FromArgb(220, 220, 220);
        cmbSource_->Items->AddRange(gcnew array<Object^>{"All", "MPQ", "CASC"});
        cmbSource_->SelectedIndex = 0;

        auto lblFmt = gcnew Label();
        lblFmt->Text = "Format:";
        lblFmt->Location = Point(180, y2 + 3);
        lblFmt->AutoSize = true;

        cmbFormat_ = gcnew ComboBox();
        cmbFormat_->Location = Point(245, y2);
        cmbFormat_->Width = 70;
        cmbFormat_->DropDownStyle = ComboBoxStyle::DropDownList;
        cmbFormat_->BackColor = Color::FromArgb(50, 50, 50);
        cmbFormat_->ForeColor = Color::FromArgb(220, 220, 220);
        cmbFormat_->Items->AddRange(gcnew array<Object^>{"All", "BLP", "DDS", "PNG/JPG"});
        cmbFormat_->SelectedIndex = 0;

        auto lblCat = gcnew Label();
        lblCat->Text = "Category:";
        lblCat->Location = Point(340, y2 + 3);
        lblCat->AutoSize = true;

        cmbCategory_ = gcnew ComboBox();
        cmbCategory_->Location = Point(420, y2);
        cmbCategory_->Width = 115;
        cmbCategory_->DropDownStyle = ComboBoxStyle::DropDownList;
        cmbCategory_->BackColor = Color::FromArgb(50, 50, 50);
        cmbCategory_->ForeColor = Color::FromArgb(220, 220, 220);
        for each (String^ s in categoryLabels_) cmbCategory_->Items->Add(s);
        cmbCategory_->SelectedIndex = 0;

        // ── Row 3: Show, Load More, View, Count ─────────────────────
        int y3 = 76;

        auto lblShow = gcnew Label();
        lblShow->Text = "Show:";
        lblShow->Location = Point(14, y3 + 3);
        lblShow->AutoSize = true;

        cmbShow_ = gcnew ComboBox();
        cmbShow_->Location = Point(75, y3);
        cmbShow_->Width = 70;
        cmbShow_->DropDownStyle = ComboBoxStyle::DropDownList;
        cmbShow_->BackColor = Color::FromArgb(50, 50, 50);
        cmbShow_->ForeColor = Color::FromArgb(220, 220, 220);
        for each (String^ s in showLabels_) cmbShow_->Items->Add(s);
        cmbShow_->SelectedIndex = 2;

        btnLoadMore_ = gcnew Button();
        btnLoadMore_->Text = "Load More";
        btnLoadMore_->Location = Point(165, y3);
        btnLoadMore_->Size = Drawing::Size(90, 24);
        btnLoadMore_->FlatStyle = FlatStyle::Flat;
        btnLoadMore_->BackColor = Color::FromArgb(55, 55, 55);
        btnLoadMore_->Click += gcnew EventHandler(this, &TextureBrowserForm::OnLoadMoreClick);

        btnViewToggle_ = gcnew Button();
        btnViewToggle_->Text = "List View";
        btnViewToggle_->Location = Point(270, y3);
        btnViewToggle_->Size = Drawing::Size(90, 24);
        btnViewToggle_->FlatStyle = FlatStyle::Flat;
        btnViewToggle_->BackColor = Color::FromArgb(55, 55, 55);
        btnViewToggle_->Click += gcnew EventHandler(this, &TextureBrowserForm::OnViewToggleClick);

        lblCount_ = gcnew Label();
        lblCount_->Text = "";
        lblCount_->Location = Point(480, y3 + 3);
        lblCount_->AutoSize = true;

        btnClearCache_ = gcnew Button();
        btnClearCache_->Text = "Clear Cache";
        btnClearCache_->Location = Point(375, y3);
        btnClearCache_->Size = Drawing::Size(90, 24);
        btnClearCache_->FlatStyle = FlatStyle::Flat;
        btnClearCache_->BackColor = Color::FromArgb(55, 55, 55);
        btnClearCache_->Click += gcnew EventHandler(this, &TextureBrowserForm::OnClearCacheClick);

        topPanel_->Controls->AddRange(gcnew array<Control^>{
            lblSearch, searchBox_, btnSearch_,
            lblSrc, cmbSource_, lblFmt, cmbFormat_, lblCat, cmbCategory_,
            lblShow, cmbShow_, btnLoadMore_, btnViewToggle_, btnClearCache_, lblCount_
        });

        // ── Slot Targeting Panel (hidden by default) ─────────────────
        slotPanel_ = gcnew Panel();
        slotPanel_->Dock = DockStyle::Top;
        slotPanel_->Height = 28;
        slotPanel_->BackColor = Color::FromArgb(50, 90, 145);
        slotPanel_->Visible = false;

        lblSlotInfo_ = gcnew Label();
        lblSlotInfo_->Text = "";
        lblSlotInfo_->Location = Point(14, 5);
        lblSlotInfo_->AutoSize = true;
        lblSlotInfo_->Font = gcnew Drawing::Font("Segoe UI", 9, FontStyle::Bold);
        lblSlotInfo_->ForeColor = Color::White;
        slotPanel_->Controls->Add(lblSlotInfo_);

        // ── ImageList (64x64 for icons) ──────────────────────────────
        imageList_ = gcnew ImageList();
        imageList_->ImageSize = Drawing::Size(thumbSize_, thumbSize_);
        imageList_->ColorDepth = ColorDepth::Depth32Bit;
        CreatePlaceholder();

        // ── SmallImageList (24x24 for list) ──────────────────────────
        smallImageList_ = gcnew ImageList();
        smallImageList_->ImageSize = Drawing::Size(24, 24);
        smallImageList_->ColorDepth = ColorDepth::Depth32Bit;
        {
            auto sb = gcnew Bitmap(24, 24);
            auto sg = Graphics::FromImage(sb);
            sg->Clear(Color::FromArgb(45, 45, 45));
            sg->~Graphics();
            smallImageList_->Images->Add(sb);
        }

        // ── ListView ─────────────────────────────────────────────────
        listView_ = gcnew ListView();
        listView_->Dock = DockStyle::Fill;
        listView_->View = View::LargeIcon;
        listView_->LargeImageList = imageList_;
        listView_->SmallImageList = smallImageList_;
        listView_->VirtualMode = true;
        listView_->VirtualListSize = 0;
        listView_->BackColor = Color::FromArgb(25, 25, 25);
        listView_->ForeColor = Color::FromArgb(200, 200, 200);
        listView_->BorderStyle = BorderStyle::None;
        listView_->MultiSelect = false;
        listView_->HideSelection = false;
        listView_->FullRowSelect = true;
        listView_->Font = gcnew Drawing::Font("Segoe UI", 8);
        listView_->RetrieveVirtualItem += gcnew RetrieveVirtualItemEventHandler(
            this, &TextureBrowserForm::OnRetrieveVirtualItem);
        listView_->ItemSelectionChanged += gcnew ListViewItemSelectionChangedEventHandler(
            this, &TextureBrowserForm::OnItemSelectionChanged);
        listView_->DoubleClick += gcnew EventHandler(this, &TextureBrowserForm::OnItemDoubleClick);

        // Icon spacing applied in Shown event (after control is fully rendered)

        listView_->Columns->Add("Filename", 180);
        listView_->Columns->Add("Source", 55);
        listView_->Columns->Add("Path", 500);

        // ── Bottom Panel ─────────────────────────────────────────────
        bottomPanel_ = gcnew Panel();
        bottomPanel_->Dock = DockStyle::Bottom;
        bottomPanel_->Height = 64;
        bottomPanel_->BackColor = Color::FromArgb(38, 38, 38);

        lblInfo_ = gcnew Label();
        lblInfo_->Text = "Set filters and click Search";
        lblInfo_->Location = Point(14, 13);
        lblInfo_->AutoSize = true;

        // Buttons positioned from right: 3 buttons × 90px + spacing
        btnSavePNG_ = gcnew Button();
        btnSavePNG_->Text = "Save PNG";
        btnSavePNG_->Size = Drawing::Size(85, 28);
        btnSavePNG_->Location = Point(665, 8);
        btnSavePNG_->FlatStyle = FlatStyle::Flat;
        btnSavePNG_->BackColor = Color::FromArgb(55, 55, 55);
        btnSavePNG_->Anchor = static_cast<AnchorStyles>(AnchorStyles::Bottom | AnchorStyles::Right);
        btnSavePNG_->Click += gcnew EventHandler(this, &TextureBrowserForm::OnSavePNGClick);

        btnApplyToSel_ = gcnew Button();
        btnApplyToSel_->Text = "Apply to Sel";
        btnApplyToSel_->Size = Drawing::Size(95, 28);
        btnApplyToSel_->Location = Point(560, 8);
        btnApplyToSel_->FlatStyle = FlatStyle::Flat;
        btnApplyToSel_->BackColor = Color::FromArgb(55, 55, 55);
        btnApplyToSel_->Anchor = static_cast<AnchorStyles>(AnchorStyles::Bottom | AnchorStyles::Right);
        btnApplyToSel_->Click += gcnew EventHandler(this, &TextureBrowserForm::OnApplyToSelClick);

        btnMtlSlot_ = gcnew Button();
        btnMtlSlot_->Text = "To Mtl Slot";
        btnMtlSlot_->Size = Drawing::Size(85, 28);
        btnMtlSlot_->Location = Point(465, 8);
        btnMtlSlot_->FlatStyle = FlatStyle::Flat;
        btnMtlSlot_->BackColor = Color::FromArgb(55, 55, 55);
        btnMtlSlot_->Anchor = static_cast<AnchorStyles>(AnchorStyles::Bottom | AnchorStyles::Right);
        btnMtlSlot_->Click += gcnew EventHandler(this, &TextureBrowserForm::OnMtlSlotClick);

        chkCopyPath_ = gcnew CheckBox();
        chkCopyPath_->Text = "Auto Prefix Path";
        chkCopyPath_->Location = Point(14, 38);
        chkCopyPath_->AutoSize = true;
        chkCopyPath_->ForeColor = Color::FromArgb(200, 200, 200);
        chkCopyPath_->Checked = true;

        bottomPanel_->Controls->AddRange(gcnew array<Control^>{
            lblInfo_, chkCopyPath_, btnMtlSlot_, btnApplyToSel_, btnSavePNG_
        });

        // ── BackgroundWorker ─────────────────────────────────────────
        bgWorker_ = gcnew BackgroundWorker();
        bgWorker_->WorkerReportsProgress = true;
        bgWorker_->WorkerSupportsCancellation = true;
        bgWorker_->DoWork += gcnew DoWorkEventHandler(this, &TextureBrowserForm::OnBgDoWork);
        bgWorker_->ProgressChanged += gcnew ProgressChangedEventHandler(this, &TextureBrowserForm::OnBgProgressChanged);
        bgWorker_->RunWorkerCompleted += gcnew RunWorkerCompletedEventHandler(this, &TextureBrowserForm::OnBgRunWorkerCompleted);

        // ── Assemble ─────────────────────────────────────────────────
        this->Controls->Add(listView_);
        this->Controls->Add(slotPanel_);
        this->Controls->Add(topPanel_);
        this->Controls->Add(bottomPanel_);

        // Apply compact icon spacing AFTER form is fully shown
        this->Shown += gcnew EventHandler(this, &TextureBrowserForm::OnFormShown);
    }

    // ========================================================================
    //  Helpers
    // ========================================================================

    void TextureBrowserForm::CreatePlaceholder() {
        auto b = gcnew Bitmap(thumbSize_, thumbSize_);
        auto g = Graphics::FromImage(b);
        g->Clear(Color::FromArgb(45, 45, 45));
        g->~Graphics();
        imageList_->Images->Add(b);
    }

    void TextureBrowserForm::OnFormShown(Object^ s, EventArgs^ e) {
        NativeSetIconSpacing(listView_->Handle.ToPointer(), thumbSize_ + 20, thumbSize_ + 30);
        listView_->Invalidate();
        // Apply initial category if set
        if (initialCategory_ > 0 && initialCategory_ < cmbCategory_->Items->Count) {
            cmbCategory_->SelectedIndex = initialCategory_;
        }
    }

    String^ TextureBrowserForm::GetSource(String^ p) {
        if (p->StartsWith("[MPQ] ")) return "MPQ";
        if (p->StartsWith("[CASC] ")) return "CASC";
        return "";
    }

    String^ TextureBrowserForm::GetClean(String^ p) {
        if (p->StartsWith("[MPQ] ")) return p->Substring(6);
        if (p->StartsWith("[CASC] ")) return p->Substring(7);
        return p;
    }

    int TextureBrowserForm::GetMaxTiles() {
        int i = cmbShow_->SelectedIndex;
        if (i < 0 || i >= showOptions_->Length) i = 2;
        return showOptions_[i];
    }

    String^ TextureBrowserForm::ExtractToTemp(String^ taggedPath) {
        auto clean = GetClean(taggedPath);
        auto src = GetSource(taggedPath);
        auto fname = Path::GetFileName(clean);
        auto outPath = Path::Combine(tempDir_, fname);
        if (File::Exists(outPath)) return outPath;

        if (src == "CASC") {
            if (WhiteoutDexTextureBrowser::CASC_ExtractToDisk(clean, outPath) > 0) return outPath;
        } else if (src == "MPQ") {
            for (int h = 0; h < WhiteoutDexTextureBrowser::MPQ_Count(); h++)
                if (WhiteoutDexTextureBrowser::MPQ_HasFile(h, clean))
                    if (WhiteoutDexTextureBrowser::MPQ_ExtractToDisk(h, clean, outPath)) return outPath;
        }
        return nullptr;
    }

    // ========================================================================
    //  Cache
    // ========================================================================

    String^ TextureBrowserForm::GetCachePath(String^ taggedPath) {
        auto md5 = MD5::Create();
        auto hash = md5->ComputeHash(Encoding::UTF8->GetBytes(taggedPath));
        auto sb = gcnew StringBuilder();
        for (int i = 0; i < hash->Length; i++) sb->Append(hash[i].ToString("x2"));
        return Path::Combine(cacheDir_, sb->ToString() + ".png");
    }

    Bitmap^ TextureBrowserForm::LoadFromCache(String^ taggedPath) {
        auto cp = GetCachePath(taggedPath);
        if (!File::Exists(cp)) return nullptr;
        try { return gcnew Bitmap(gcnew MemoryStream(File::ReadAllBytes(cp))); }
        catch (...) { return nullptr; }
    }

    void TextureBrowserForm::SaveToCache(String^ taggedPath, Bitmap^ thumb) {
        if (!thumb) return;
        try { thumb->Save(GetCachePath(taggedPath), ImageFormat::Png); } catch (...) {}
    }

    // ========================================================================
    //  Category
    // ========================================================================

    bool TextureBrowserForm::MatchesCategory(String^ pl, int c) {
        if (c == 0) return true;
        switch (c) {
        case 1: return pl->Contains("units\\") || pl->Contains("units/");
        case 2: return pl->Contains("buildings\\") || pl->Contains("buildings/");
        case 3: return pl->Contains("doodads\\") || pl->Contains("doodads/");
        case 4: return pl->Contains("abilities\\") || pl->Contains("abilities/") ||
                       pl->Contains("spells\\") || pl->Contains("spells/");
        case 5: return pl->Contains("environment\\") || pl->Contains("environment/") ||
                       pl->Contains("terrain\\") || pl->Contains("terrain/") ||
                       pl->Contains("tilesets\\") || pl->Contains("tilesets/") ||
                       pl->Contains("cliff") || pl->Contains("water");
        case 6: return pl->Contains("icon") || pl->Contains("commandbuttons") ||
                       pl->Contains("passivebuttons") || pl->Contains("\\btn") || pl->Contains("/btn");
        default: return true;
        }
    }

    bool TextureBrowserForm::MatchesSlotFilter(String^ pl) {
        if (!slotFilter_ || slotFilter_->Length == 0 || slotFilter_ == "diffuse") return true;
        if (slotFilter_ == "normal")     return pl->Contains("_normal");
        if (slotFilter_ == "orm")        return pl->Contains("_orm");
        if (slotFilter_ == "emissive")   return pl->Contains("_emissive");
        if (slotFilter_ == "reflection") return pl->Contains("_environment") || pl->Contains("_env");
        return true;
    }

    String^ TextureBrowserForm::GetSlotLabel() {
        if (!slotFilter_ || slotFilter_->Length == 0) return "";
        if (slotFilter_ == "diffuse")    return "Diffuse Texture";
        if (slotFilter_ == "normal")     return "Normal Map";
        if (slotFilter_ == "orm")        return "ORM Map";
        if (slotFilter_ == "emissive")   return "Emissive Map";
        if (slotFilter_ == "reflection") return "Environment Map";
        return slotFilter_;
    }

    void TextureBrowserForm::UpdateSlotUI() {
        if (!slotPanel_ || !lblSlotInfo_) return;
        if (slotFilter_ && slotFilter_->Length > 0) {
            lblSlotInfo_->Text = "Targeting: " + GetSlotLabel();
            slotPanel_->Visible = true;
            if (btnApplyToSel_) btnApplyToSel_->Text = "Assign";
            if (btnMtlSlot_)    btnMtlSlot_->Text = "Assign";
        } else {
            slotPanel_->Visible = false;
            if (btnApplyToSel_) btnApplyToSel_->Text = "Apply to Sel";
            if (btnMtlSlot_)    btnMtlSlot_->Text = "To Mtl Slot";
        }
        // Re-filter if paths are already loaded
        if (this->Visible && allPaths_ && allPaths_->Count > 0) {
            FilterAndDisplay();
        }
    }

    // ========================================================================
    //  Collect + Filter
    // ========================================================================

    void TextureBrowserForm::CollectPaths(int sourceIdx) {
        allPaths_->Clear();
        if ((sourceIdx == 0 || sourceIdx == 1) && WhiteoutDexTextureBrowser::MPQ_Count() > 0) {
            auto b = WhiteoutDexTextureBrowser::MPQ_ListAllFiles(".blp");
            if (b) for each (String^ p in b) allPaths_->Add("[MPQ] " + p);
            auto d = WhiteoutDexTextureBrowser::MPQ_ListAllFiles(".dds");
            if (d) for each (String^ p in d) allPaths_->Add("[MPQ] " + p);
            auto p1 = WhiteoutDexTextureBrowser::MPQ_ListAllFiles(".png");
            if (p1) for each (String^ p in p1) allPaths_->Add("[MPQ] " + p);
            auto j = WhiteoutDexTextureBrowser::MPQ_ListAllFiles(".jpg");
            if (j) for each (String^ p in j) allPaths_->Add("[MPQ] " + p);
            auto bm = WhiteoutDexTextureBrowser::MPQ_ListAllFiles(".bmp");
            if (bm) for each (String^ p in bm) allPaths_->Add("[MPQ] " + p);
            auto tg = WhiteoutDexTextureBrowser::MPQ_ListAllFiles(".tga");
            if (tg) for each (String^ p in tg) allPaths_->Add("[MPQ] " + p);
        }
        if ((sourceIdx == 0 || sourceIdx == 2) && WhiteoutDexTextureBrowser::CASC_IsOpen()) {
            auto b = WhiteoutDexTextureBrowser::CASC_SearchFiles(".blp");
            if (b) for each (String^ p in b) allPaths_->Add("[CASC] " + p);
            auto d = WhiteoutDexTextureBrowser::CASC_SearchFiles(".dds");
            if (d) for each (String^ p in d) allPaths_->Add("[CASC] " + p);
            auto p1 = WhiteoutDexTextureBrowser::CASC_SearchFiles(".png");
            if (p1) for each (String^ p in p1) allPaths_->Add("[CASC] " + p);
            auto j = WhiteoutDexTextureBrowser::CASC_SearchFiles(".jpg");
            if (j) for each (String^ p in j) allPaths_->Add("[CASC] " + p);
            auto bm = WhiteoutDexTextureBrowser::CASC_SearchFiles(".bmp");
            if (bm) for each (String^ p in bm) allPaths_->Add("[CASC] " + p);
            auto tg = WhiteoutDexTextureBrowser::CASC_SearchFiles(".tga");
            if (tg) for each (String^ p in tg) allPaths_->Add("[CASC] " + p);
        }
        allPaths_->Sort();
    }

    void TextureBrowserForm::FilterAndDisplay() {
        if (bgWorker_->IsBusy) {
            bgWorker_->CancelAsync();
            while (bgWorker_->IsBusy) Application::DoEvents();
        }

        auto search = searchBox_->Text->ToLower();
        int fmtIdx = cmbFormat_->SelectedIndex;
        int catIdx = cmbCategory_->SelectedIndex;
        int maxT = GetMaxTiles();
        filteredPaths_->Clear();

        for each (String^ p in allPaths_) {
            auto pl = p->ToLower();
            if (fmtIdx == 1 && !pl->Contains(".blp")) continue;
            if (fmtIdx == 2 && !pl->Contains(".dds")) continue;
            if (fmtIdx == 3 && !pl->Contains(".png") && !pl->Contains(".jpg")
                             && !pl->Contains(".bmp") && !pl->Contains(".tga")) continue;
            if (!MatchesCategory(pl, catIdx)) continue;
            if (!MatchesSlotFilter(pl)) continue;
            if (search->Length > 0 && !pl->Contains(search)) continue;
            filteredPaths_->Add(p);
            if (filteredPaths_->Count >= maxT) break;
        }

        thumbsLoaded_ = 0;
        thumbMap_->Clear();
        while (imageList_->Images->Count > 1)
            imageList_->Images->RemoveAt(imageList_->Images->Count - 1);
        while (smallImageList_->Images->Count > 1)
            smallImageList_->Images->RemoveAt(smallImageList_->Images->Count - 1);

        listView_->VirtualListSize = 0;
        listView_->VirtualListSize = filteredPaths_->Count;
        lblCount_->Text = "0/" + filteredPaths_->Count.ToString();
        lblInfo_->Text = allPaths_->Count + " total, " + filteredPaths_->Count + " matched";

        if (filteredPaths_->Count > 0 && !bgWorker_->IsBusy) {
            isLoading_ = true;
            bgWorker_->RunWorkerAsync();
        }
    }

    // ========================================================================
    //  Thumbnail Batch
    // ========================================================================

    void TextureBrowserForm::LoadThumbnailBatch() {
        int limit = filteredPaths_->Count;
        if (thumbsLoaded_ >= limit) return;
        int s = thumbsLoaded_, e = Math::Min(s + batchSize_, limit);

        auto cached = gcnew Dictionary<int, Bitmap^>();
        auto cL = gcnew List<String^>(); auto cI = gcnew List<int>();
        auto mL = gcnew List<String^>(); auto mI = gcnew List<int>();

        for (int i = s; i < e; i++) {
            auto tp = filteredPaths_[i];
            auto cb = LoadFromCache(tp);
            if (cb) { cached[i] = cb; continue; }
            auto src = GetSource(tp);
            auto cl = GetClean(tp);
            if (src == "CASC") { cL->Add(cl); cI->Add(i); }
            else if (src == "MPQ") { mL->Add(cl); mI->Add(i); }
        }

        auto fresh = gcnew Dictionary<int, Bitmap^>();
        if (cL->Count > 0) {
            auto t = WhiteoutDexTextureBrowser::GenerateThumbnailsCASC(cL->ToArray(), thumbSize_);
            if (t) for (int i = 0; i < t->Length; i++)
                if (t[i]->Success && t[i]->Thumbnail) {
                    fresh[cI[i]] = t[i]->Thumbnail;
                    SaveToCache(filteredPaths_[cI[i]], t[i]->Thumbnail);
                }
        }
        if (mL->Count > 0) {
            auto t = WhiteoutDexTextureBrowser::GenerateThumbnailsMPQAll(mL->ToArray(), thumbSize_);
            if (t) for (int i = 0; i < t->Length; i++)
                if (t[i]->Success && t[i]->Thumbnail) {
                    fresh[mI[i]] = t[i]->Thumbnail;
                    SaveToCache(filteredPaths_[mI[i]], t[i]->Thumbnail);
                }
        }

        auto all = gcnew Dictionary<int, Bitmap^>();
        for each (auto kv in cached) all[kv.Key] = kv.Value;
        for each (auto kv in fresh) all[kv.Key] = kv.Value;
        thumbsLoaded_ = e;
        bgWorker_->ReportProgress((int)(100.0 * thumbsLoaded_ / limit), all);
    }

    // ========================================================================
    //  BackgroundWorker
    // ========================================================================

    void TextureBrowserForm::OnBgDoWork(Object^ s, DoWorkEventArgs^ e) {
        while (thumbsLoaded_ < filteredPaths_->Count) {
            if (bgWorker_->CancellationPending) { e->Cancel = true; return; }
            LoadThumbnailBatch();
        }
    }

    void TextureBrowserForm::OnBgProgressChanged(Object^ s, ProgressChangedEventArgs^ e) {
        auto r = safe_cast<Dictionary<int, Bitmap^>^>(e->UserState);
        if (!r) return;
        for each (auto kv in r) {
            // Force alpha to 255 (many WC3 textures have alpha=0 for team color masking)
            auto bmp = kv.Value;
            auto rect = System::Drawing::Rectangle(0, 0, bmp->Width, bmp->Height);
            auto bd = bmp->LockBits(rect, ImageLockMode::ReadWrite, PixelFormat::Format32bppArgb);
            unsigned char* ptr = static_cast<unsigned char*>(bd->Scan0.ToPointer());
            int total = bmp->Width * bmp->Height;
            for (int i = 0; i < total; i++)
                ptr[i * 4 + 3] = 255;
            bmp->UnlockBits(bd);

            int idx = imageList_->Images->Count;
            imageList_->Images->Add(bmp);
            smallImageList_->Images->Add(gcnew Bitmap(bmp, 24, 24));
            thumbMap_[kv.Key] = idx;
        }
        lblCount_->Text = thumbsLoaded_ + "/" + filteredPaths_->Count;
        listView_->Invalidate();
    }

    void TextureBrowserForm::OnBgRunWorkerCompleted(Object^ s, RunWorkerCompletedEventArgs^ e) {
        isLoading_ = false;
        lblCount_->Text = thumbsLoaded_ + "/" + filteredPaths_->Count;
        listView_->Invalidate();
    }

    // ========================================================================
    //  VirtualMode
    // ========================================================================

    void TextureBrowserForm::OnRetrieveVirtualItem(Object^ s, RetrieveVirtualItemEventArgs^ e) {
        int i = e->ItemIndex;
        if (i < 0 || i >= filteredPaths_->Count) { e->Item = gcnew ListViewItem("?"); return; }

        auto tp = filteredPaths_[i];
        auto cl = GetClean(tp);
        auto src = GetSource(tp);
        int img = thumbMap_->ContainsKey(i) ? thumbMap_[i] : 0;

        auto item = gcnew ListViewItem(Path::GetFileName(cl), img);
        item->Tag = tp;
        item->ForeColor = Color::FromArgb(200, 200, 200);
        item->SubItems->Add(src);
        item->SubItems->Add(cl);
        e->Item = item;
    }

    // ========================================================================
    //  Event Handlers
    // ========================================================================

    void TextureBrowserForm::OnSearchClick(Object^ s, EventArgs^ e) {
        this->Cursor = Cursors::WaitCursor;
        int si = cmbSource_->SelectedIndex;
        if (allPaths_->Count == 0 || si != currentSourceIdx_) {
            currentSourceIdx_ = si;
            CollectPaths(si);
        }
        FilterAndDisplay();
        this->Cursor = Cursors::Default;
    }

    void TextureBrowserForm::OnSearchKeyDown(Object^ s, KeyEventArgs^ e) {
        if (e->KeyCode == Keys::Enter) { OnSearchClick(s, e); e->SuppressKeyPress = true; }
    }

    void TextureBrowserForm::OnLoadMoreClick(Object^ s, EventArgs^ e) {
        if (!bgWorker_->IsBusy && thumbsLoaded_ < filteredPaths_->Count)
            bgWorker_->RunWorkerAsync();
    }

    void TextureBrowserForm::OnViewToggleClick(Object^ s, EventArgs^ e) {
        isListView_ = !isListView_;
        if (isListView_) {
            listView_->View = View::Details;
            btnViewToggle_->Text = "Icon View";
        } else {
            listView_->View = View::LargeIcon;
            btnViewToggle_->Text = "List View";
            NativeSetIconSpacing(listView_->Handle.ToPointer(), thumbSize_ + 20, thumbSize_ + 30);
        }
        listView_->Invalidate();
    }

    void TextureBrowserForm::OnItemSelectionChanged(Object^ s,
        ListViewItemSelectionChangedEventArgs^ e)
    {
        if (e->IsSelected && e->ItemIndex >= 0 && e->ItemIndex < filteredPaths_->Count) {
            auto tp = filteredPaths_[e->ItemIndex];
            selectedPath_ = GetClean(tp);
            selectedSource_ = GetSource(tp);
            lblInfo_->Text = "[" + selectedSource_ + "] " + selectedPath_;
            TextureSelected(this, gcnew TextureEventArgs(selectedPath_, nullptr, selectedSource_));
        }
    }

    void TextureBrowserForm::OnItemDoubleClick(Object^ s, EventArgs^ e) {
        if (!selectedPath_ || selectedPath_->Length == 0) return;
        auto tf = ExtractToTemp("[" + selectedSource_ + "] " + selectedPath_);
        TextureApply(this, gcnew TextureEventArgs(selectedPath_, tf, selectedSource_));
    }

    void TextureBrowserForm::OnMtlSlotClick(Object^ s, EventArgs^ e) {
        if (!selectedPath_ || selectedPath_->Length == 0) return;
        auto tf = ExtractToTemp("[" + selectedSource_ + "] " + selectedPath_);
        TextureMtlSlot(this, gcnew TextureEventArgs(selectedPath_, tf, selectedSource_));
    }

    void TextureBrowserForm::OnApplyToSelClick(Object^ s, EventArgs^ e) {
        if (!selectedPath_ || selectedPath_->Length == 0) return;
        auto tf = ExtractToTemp("[" + selectedSource_ + "] " + selectedPath_);
        TextureApply(this, gcnew TextureEventArgs(selectedPath_, tf, selectedSource_));
    }

    void TextureBrowserForm::OnSavePNGClick(Object^ s, EventArgs^ e) {
        if (!selectedPath_ || selectedPath_->Length == 0) return;
        auto tf = ExtractToTemp("[" + selectedSource_ + "] " + selectedPath_);
        if (!tf) { lblInfo_->Text = "Extract failed."; return; }

        auto dlg = gcnew SaveFileDialog();
        dlg->Filter = "PNG (*.png)|*.png";
        dlg->FileName = Path::GetFileNameWithoutExtension(selectedPath_) + ".png";
        if (dlg->ShowDialog() != System::Windows::Forms::DialogResult::OK) return;

        auto ext = Path::GetExtension(tf)->ToLower();
        Bitmap^ bmp = nullptr;
        if (ext == ".blp") bmp = WhiteoutDexTextureBrowser::DecodeBLP(tf);
        else if (ext == ".dds") bmp = WhiteoutDexTextureBrowser::DecodeDDS(tf);

        if (bmp) {
            bmp->Save(dlg->FileName, ImageFormat::Png);
            bmp->~Bitmap();
            lblInfo_->Text = "Saved: " + dlg->FileName;
        } else {
            lblInfo_->Text = "Decode failed.";
        }
    }

    void TextureBrowserForm::OnClearCacheClick(Object^ s, EventArgs^ e) {
        try {
            auto files = Directory::GetFiles(cacheDir_, "*.png");
            int count = files->Length;
            for each (String^ f in files) {
                try { File::Delete(f); } catch (...) {}
            }
            long long sizeFreed = count * 8;  // rough estimate ~8KB each
            lblInfo_->Text = "Cache cleared: " + count + " files (~" + sizeFreed + " KB)";
        }
        catch (...) {
            lblInfo_->Text = "Failed to clear cache.";
        }
    }

} // namespace WhiteoutDex
