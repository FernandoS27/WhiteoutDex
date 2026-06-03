macroscript WhiteoutDex_Settings
buttonText:"Settings..."
category:"WhiteoutDex Toolkit"
internalCategory:"WhiteoutDex Toolkit"
tooltip:"WhiteoutDex Settings - Language / 语言 / Sprache / Язык / 言語 / 언어"
(
	rollout settingsRollout "WhiteoutDex Settings" width:340 height:480
	(
		local sidebarIni = (::WhiteoutDexInstallRoot) + "\\WhiteoutDex_Settings.ini"
		
		groupBox langGrp "Language / 语言 / Sprache / Язык / 言語 / 언어" pos:[8,4] width:324 height:72
		
		label langLabel "Interface Language:" pos:[20,28] width:120 height:16
		dropdownList langDDL "" pos:[142,24] width:140 height:20
		
		label noteLabel "" pos:[20,52] width:260 height:16 style_sunkenedge:false
		
		groupBox mpqGrp "MPQ Archives (Classic v800)" pos:[8,82] width:324 height:94
		label mpqLabel "Game Data Directory:" pos:[20,102] width:130 height:16
		edittext mpqPathEdt "" pos:[20,120] width:260 height:20 readOnly:true
		button mpqBrowseBtn "..." pos:[284,120] width:38 height:20 tooltip:"Browse for Warcraft III game data folder containing MPQ files"
		label mpqStatusLbl "" pos:[20,146] width:200 height:14
		button mpqClearBtn "Clear" pos:[248,144] width:72 height:18 tooltip:"Clear the MPQ directory"
		
		groupBox cascGrp "CASC Archives (Reforged v1200)" pos:[8,178] width:324 height:94
		label cascLabel "Warcraft III Reforged:" pos:[20,198] width:140 height:16
		edittext cascPathEdt "" pos:[20,216] width:260 height:20 readOnly:true
		button cascBrowseBtn "..." pos:[284,216] width:38 height:20 tooltip:"Browse for Warcraft III Reforged installation folder"
		label cascStatusLbl "" pos:[20,242] width:200 height:14
		button cascClearBtn "Clear" pos:[248,240] width:72 height:18 tooltip:"Clear the CASC directory"
		
		groupBox sidebarGrp "Sidebar" pos:[8,278] width:324 height:78
		checkbox chk_sidebarEnabled "Show Sidebar" pos:[20,300] width:120 height:18 tooltip:"Show the WhiteoutDex tool sidebar on startup"
		label lblDockSide "Dock Side:" pos:[160,302] width:60 height:16
		dropdownList ddl_dockSide "" pos:[222,298] width:100 height:20 items:#("Left", "Right")
		
		groupBox updateGrp "Auto-Update" pos:[8,362] width:324 height:72
		checkbox chk_autoUpdate "Check for updates on startup" pos:[20,384] width:200 height:18 tooltip:"Automatically check GitHub for new versions when 3ds Max starts"
		button btn_checkNow "Check Now" pos:[228,382] width:92 height:22 tooltip:"Manually check for updates now"
		
		button closeBtn "OK" pos:[248,446] width:82 height:24
		
		fn populateLanguages =
		(
			local langs = ::WdxL.getAvailableLanguages()
			local names = #()
			local currentIdx = 1
			for i = 1 to langs.count do
			(
				local code = langs[i]
				case code of
				(
					"en": append names "English (en)"
					"zh": append names "Chinese / 中文 (zh)"
					"de": append names "Deutsch / German (de)"
					"ru": append names "Русский / Russian (ru)"
					"ja": append names "日本語 / Japanese (ja)"
					"ko": append names "한국어 / Korean (ko)"
					default: append names code
				)
				if code == ::WdxL.getLanguage() then currentIdx = i
			)
			langDDL.items = names
			langDDL.selection = currentIdx
		)
		
		on langDDL selected idx do
		(
			local langs = ::WdxL.getAvailableLanguages()
			if idx >= 1 and idx <= langs.count then
			(
				local newLang = langs[idx]
				if newLang != ::WdxL.getLanguage() then
				(
					::WdxL.setLanguage newLang
					noteLabel.text = "Please reopen dialogs to see changes."
				)
			)
		)
		
		on mpqBrowseBtn pressed do
		(
			local dir = getSavePath caption:"Select Warcraft III Game Data Folder"
			if dir != undefined then
			(
				mpqPathEdt.text = dir
				::WhiteoutDexMPQ.saveSettings dir
				if ::WhiteoutDexMPQ.validateDirectory() then
					mpqStatusLbl.text = ::WdxL.t "set_mpq_status_found"
				else
					mpqStatusLbl.text = ::WdxL.t "set_mpq_status_not_found"
			)
		)
		
		on mpqClearBtn pressed do
		(
			mpqPathEdt.text = ""
			::WhiteoutDexMPQ.saveSettings ""
			mpqStatusLbl.text = ""
		)
		
		on cascBrowseBtn pressed do
		(
			local dir = getSavePath caption:(::WdxL.t "set_casc_browse_caption")
			if dir != undefined then
			(
				cascPathEdt.text = dir
				local iniPath = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
				setINISetting iniPath "CASC" "W3Path" dir
				-- WhiteoutLib opens CASC by directory; .build.info is not a
				-- required marker. Show "found" whenever the directory itself
				-- exists; the native open log surfaces any real failure.
				if doesFileExist dir then
					cascStatusLbl.text = ::WdxL.t "set_casc_status_found"
				else
					cascStatusLbl.text = ::WdxL.t "set_casc_status_not_found"
			)
		)
		
		on cascClearBtn pressed do
		(
			cascPathEdt.text = ""
			local iniPath = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
			setINISetting iniPath "CASC" "W3Path" ""
			cascStatusLbl.text = ""
		)
		
		-- Sidebar: toggle show/hide immediately
		on chk_sidebarEnabled changed state do
		(
			if (maxVersion())[1] >= 20000 then
				setINISetting sidebarIni "Sidebar" "Enabled" (if state then "1" else "0") forceUTF16:false
			else
				setINISetting sidebarIni "Sidebar" "Enabled" (if state then "1" else "0")
			if state then
			(
				if ::WhiteoutDexSidebar != undefined then ::WhiteoutDexSidebar.show()
			)
			else
			(
				if ::WhiteoutDexSidebar != undefined then ::WhiteoutDexSidebar.hide()
			)
		)
		
		-- Sidebar: change dock side immediately
		on ddl_dockSide selected idx do
		(
			local side = if idx == 1 then "cui_dock_left" else "cui_dock_right"
			if (maxVersion())[1] >= 20000 then
				setINISetting sidebarIni "Sidebar" "DockState" side forceUTF16:false
			else
				setINISetting sidebarIni "Sidebar" "DockState" side
			-- Re-dock if sidebar is open
			if ::WhiteoutDexSidebar != undefined and ::WhiteoutDexSidebar.isOpen then
			(
				local dockFlag = if idx == 1 then #cui_dock_left else #cui_dock_right
				try (cui.DockDialogBar ::WhiteoutDexSidebarRollout dockFlag) catch()
			)
		)
		
		-- Auto-Update: toggle
		on chk_autoUpdate changed state do
		(
			if (maxVersion())[1] >= 20000 then
				setINISetting sidebarIni "Updater" "AutoCheck" (if state then "1" else "0") forceUTF16:false
			else
				setINISetting sidebarIni "Updater" "AutoCheck" (if state then "1" else "0")
		)
		
		-- Auto-Update: manual check
		on btn_checkNow pressed do
		(
			local updaterPath = (::WhiteoutDexInstallRoot) + "\\post-start-up scripts parts\\WhiteoutDexAutoUpdater.ms"
			if doesFileExist updaterPath then
			(
				-- Force check regardless of auto-check setting
				global _ndxForceUpdateCheck = true
				fileIn updaterPath
			)
			else
			(
				messageBox "Auto-Updater script not found." title:"WhiteoutDex" beep:false
			)
		)
		
		on closeBtn pressed do destroyDialog settingsRollout
		
		on settingsRollout open do
		(
			populateLanguages()
			noteLabel.text = ""
			if ::WhiteoutDexMPQ != undefined then
			(
				mpqPathEdt.text = ::WhiteoutDexMPQ.getDirectory()
				if ::WhiteoutDexMPQ.getDirectory() != "" then
				(
					if ::WhiteoutDexMPQ.validateDirectory() then
						mpqStatusLbl.text = ::WdxL.t "set_mpq_status_found"
					else
						mpqStatusLbl.text = ::WdxL.t "set_mpq_status_not_found"
				)
				else
					mpqStatusLbl.text = ""
			)
			if ::WdxL != undefined then
			(
				mpqLabel.text = ::WdxL.t "set_mpq_directory_lbl"
				mpqBrowseBtn.tooltip = ::WdxL.t "set_mpq_browse_tip"
				mpqClearBtn.text = ::WdxL.t "set_mpq_clear_btn"
			)
			-- CASC path. WhiteoutLib doesn't require .build.info — check
			-- only that the configured directory still exists.
			local cascIni = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
			local cascDir = getINISetting cascIni "CASC" "W3Path"
			if cascDir != "" then
			(
				cascPathEdt.text = cascDir
				if doesFileExist cascDir then
					cascStatusLbl.text = ::WdxL.t "set_casc_status_found"
				else
					cascStatusLbl.text = ::WdxL.t "set_casc_status_not_found"
			)
			if ::WdxL != undefined then
			(
				cascLabel.text = ::WdxL.t "set_casc_directory_lbl"
				cascBrowseBtn.tooltip = ::WdxL.t "set_casc_browse_tip"
				cascClearBtn.text = ::WdxL.t "set_casc_clear_btn"
			)
			-- Sidebar localization
			if ::WdxL != undefined then
			(
				sidebarGrp.text = ::WdxL.t "set_sidebar_grp"
				chk_sidebarEnabled.text = ::WdxL.t "set_sidebar_show_chk"
				lblDockSide.text = ::WdxL.t "set_sidebar_dock_lbl"
				ddl_dockSide.items = #(::WdxL.t "set_sidebar_dock_left", ::WdxL.t "set_sidebar_dock_right")
			)
			-- Sidebar: load saved state
			local sidebarOn = getINISetting sidebarIni "Sidebar" "Enabled"
			chk_sidebarEnabled.checked = (sidebarOn == "1")
			local dockSide = getINISetting sidebarIni "Sidebar" "DockState"
			ddl_dockSide.selection = if dockSide == "cui_dock_right" then 2 else 1
			
			-- Auto-Update: load saved state (default: enabled)
			local autoUpdateOn = getINISetting sidebarIni "Updater" "AutoCheck"
			chk_autoUpdate.checked = (autoUpdateOn != "0")
			-- Auto-Update: localization
			if ::WdxL != undefined then
			(
				updateGrp.text = ::WdxL.t "set_update_grp"
				chk_autoUpdate.text = ::WdxL.t "set_update_auto_chk"
				btn_checkNow.text = ::WdxL.t "set_update_check_btn"
			)
		)
	)
	
	on execute do
	(
		try (destroyDialog settingsRollout) catch ()
		if (maxVersion())[1] >= 19000 then
			createDialog settingsRollout modal:true
		else
			createDialog settingsRollout
	)
)
