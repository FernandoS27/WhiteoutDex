// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#using <System.dll>
#using <System.Drawing.dll>

#ifndef WHITEOUTDEX_NETCORE
#using <System.Windows.Forms.dll>
#endif

namespace WhiteoutDex {

    using namespace System;
    using namespace System::Drawing;
    using namespace System::Windows::Forms;
    using namespace System::ComponentModel;
    using namespace System::Collections::Generic;

    public ref class TextureEventArgs : public EventArgs {
    public:
        property String^ ArchivePath;
        property String^ ExtractedPath;
        property String^ Source;
        TextureEventArgs(String^ a, String^ e, String^ s) {
            ArchivePath = a; ExtractedPath = e; Source = s;
        }
    };

    public ref class TextureBrowserForm : public Form {
    public:
        event EventHandler<TextureEventArgs^>^ TextureApply;
        event EventHandler<TextureEventArgs^>^ TextureMtlSlot;
        event EventHandler<TextureEventArgs^>^ TextureSelected;

        property String^ SelectedPath   { String^ get() { return selectedPath_; } }
        property String^ SelectedSource { String^ get() { return selectedSource_; } }
        
        // Slot targeting: set before Show() to filter by texture type
        // Values: "" (no filter), "diffuse", "normal", "orm", "emissive", "reflection"
        property String^ SlotFilter {
            String^ get() { return slotFilter_; }
            void set(String^ value) { 
                slotFilter_ = value ? value : "";
                UpdateSlotUI();
            }
        }
        
        // Auto-copy archive directory path into material prefix path
        property bool AutoPrefixPath {
            bool get() { return chkCopyPath_ ? chkCopyPath_->Checked : false; }
        }
        
        // Initial category index to select when form opens
        // 0=All, 1=Units, 2=Buildings, 3=Doodads, 4=Abilities, 5=Environment, 6=Icons
        property int InitialCategory {
            int get() { return initialCategory_; }
            void set(int value) { initialCategory_ = value; }
        }

        TextureBrowserForm();
        ~TextureBrowserForm();

    private:
        Panel^              topPanel_;
        Panel^              slotPanel_;
        Label^              lblSlotInfo_;
        TextBox^            searchBox_;
        Button^             btnSearch_;
        ComboBox^           cmbSource_;
        ComboBox^           cmbFormat_;
        ComboBox^           cmbCategory_;
        ComboBox^           cmbShow_;
        Button^             btnLoadMore_;
        Button^             btnViewToggle_;
        Label^              lblCount_;
        ListView^           listView_;
        ImageList^          imageList_;
        ImageList^          smallImageList_;
        Panel^              bottomPanel_;
        Label^              lblInfo_;
        Button^             btnMtlSlot_;
        Button^             btnApplyToSel_;
        Button^             btnSavePNG_;
        Button^             btnClearCache_;
        CheckBox^           chkCopyPath_;
        BackgroundWorker^   bgWorker_;

        List<String^>^      allPaths_;
        List<String^>^      filteredPaths_;
        Dictionary<int,int>^ thumbMap_;
        int                 thumbsLoaded_;
        String^             selectedPath_;
        String^             selectedSource_;
        String^             tempDir_;
        String^             cacheDir_;
        int                 thumbSize_;
        int                 batchSize_;
        bool                isLoading_;
        int                 currentSourceIdx_;
        bool                isListView_;
        String^             slotFilter_;
        int                 initialCategory_;

        static array<int>^     showOptions_ = gcnew array<int>   { 50, 100, 200, 500, 1000, 2000, 99999 };
        static array<String^>^ showLabels_  = gcnew array<String^>{ "50", "100", "200", "500", "1000", "2000", "All" };
        static array<String^>^ categoryLabels_ = gcnew array<String^>{
            "All", "Units", "Buildings", "Doodads", "Abilities", "Environment", "Icons"
        };

        String^ GetSource(String^ taggedPath);
        String^ GetClean(String^ taggedPath);
        int     GetMaxTiles();
        String^ ExtractToTemp(String^ taggedPath);
        void    CreatePlaceholder();
        bool    MatchesCategory(String^ pathLower, int catIdx);
        bool    MatchesSlotFilter(String^ pathLower);
        String^ GetSlotLabel();
        void    UpdateSlotUI();

        String^  GetCachePath(String^ taggedPath);
        Bitmap^  LoadFromCache(String^ taggedPath);
        void     SaveToCache(String^ taggedPath, Bitmap^ thumb);

        void InitializeUI();
        void CollectPaths(int sourceIdx);
        void FilterAndDisplay();
        void LoadThumbnailBatch();

        void OnSearchClick(Object^ s, EventArgs^ e);
        void OnSearchKeyDown(Object^ s, KeyEventArgs^ e);
        void OnLoadMoreClick(Object^ s, EventArgs^ e);
        void OnViewToggleClick(Object^ s, EventArgs^ e);
        void OnFormShown(Object^ s, EventArgs^ e);
        void OnRetrieveVirtualItem(Object^ s, RetrieveVirtualItemEventArgs^ e);
        void OnItemSelectionChanged(Object^ s, ListViewItemSelectionChangedEventArgs^ e);
        void OnItemDoubleClick(Object^ s, EventArgs^ e);
        void OnMtlSlotClick(Object^ s, EventArgs^ e);
        void OnApplyToSelClick(Object^ s, EventArgs^ e);
        void OnSavePNGClick(Object^ s, EventArgs^ e);
        void OnClearCacheClick(Object^ s, EventArgs^ e);

        void OnBgDoWork(Object^ s, DoWorkEventArgs^ e);
        void OnBgProgressChanged(Object^ s, ProgressChangedEventArgs^ e);
        void OnBgRunWorkerCompleted(Object^ s, RunWorkerCompletedEventArgs^ e);
    };
}
