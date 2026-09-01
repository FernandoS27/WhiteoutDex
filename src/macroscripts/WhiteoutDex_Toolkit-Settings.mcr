macroscript WhiteoutDex_Settings
buttonText:"Settings..."
category:"WhiteoutDex Toolkit"
internalCategory:"WhiteoutDex Toolkit"
tooltip:"WhiteoutDex Settings - Language / 语言 / Sprache / Язык / 言語 / 언어"
(
	/* ====================================================================
	   MPQ LOAD ORDER

	   The list of archives to search, in the order to search them, shared
	   by everything in the toolkit that opens an MPQ: the renderer's
	   content providers, the importer's texture resolver, the asset
	   browser's tree and its extractor.

	     <plugcfg>\WhiteoutDex_Settings.ini
	     [MPQ]
	     List=War3Patch.mpq|War3x.mpq|War3.mpq|D:\Mods\Custom.mpq

	   The native half lives in src/native_sources/common/wdx_mpq_settings.h
	   and reads the same key the same way - an entry is a name relative to
	   the Game Data Directory, or a full path when the archive lives
	   somewhere else. Change the format in one and the other has to follow.

	   An ABSENT key means "never customised", which every reader treats as
	   "keep your own default order". That is why OK deletes the key rather
	   than writing an empty value when the list is emptied: an empty value
	   reads the same as absent anyway, and deleting says so honestly.
	   ==================================================================== */
	rollout mpqOrderRollout "MPQ Load Order" width:530 height:312
	(
		local entries = #()
		local baseDir = ""
		local settingsIni = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
		-- The retail Warcraft III archives, highest priority first. Mirrors
		-- wdx::mpq::DefaultNames() in wdx_mpq_settings.h.
		local defaultNames = #("War3Patch.mpq", "War3xLocal.mpq", "War3Local.mpq", "War3x.mpq", "War3.mpq", "Deprecated.mpq")

		label hintLbl "Archives are searched top to bottom - the first one holding a file wins." pos:[10,8] width:510 height:16
		listbox lstMpq "" pos:[10,28] width:370 height:13
		button btnUp "Move Up" pos:[392,44] width:126 height:24
		button btnDown "Move Down" pos:[392,72] width:126 height:24
		button btnRemove "Remove" pos:[392,104] width:126 height:24
		button btnAddFile "Add Archive..." pos:[392,140] width:126 height:24 tooltip:"Add one .mpq from anywhere on disk"
		button btnAddFolder "Add Folder..." pos:[392,168] width:126 height:24 tooltip:"Add every .mpq in a folder"
		button btnScan "Scan Game Folder" pos:[392,204] width:126 height:24 tooltip:"Replace the list with what is in the Game Data Directory"
		button btnDefaults "Defaults" pos:[392,232] width:126 height:24 tooltip:"Replace the list with the retail Warcraft III archives"
		label baseLbl "" pos:[10,212] width:370 height:16
		label emptyLbl "An empty list means the default order." pos:[10,232] width:370 height:16
		button btnOk "OK" pos:[306,274] width:100 height:26
		button btnCancel "Cancel" pos:[414,274] width:100 height:26

		-- Absolute (a drive letter or a UNC root) is the answer on its own;
		-- anything else hangs off the base directory. Same rule as
		-- wdx::mpq::Resolve, which gets it from std::filesystem.
		fn isAbsoluteEntry e =
		(
			if e.count < 2 then return false
			if e[2] == ":" then return true
			(e[1] == "\\" and e[2] == "\\")
		)

		fn resolveEntry e =
		(
			if e == "" then return ""
			if isAbsoluteEntry e then return e
			if baseDir == "" then return ""
			local b = baseDir
			if b[b.count] != "\\" then b += "\\"
			b + e
		)

		-- Redraw from `entries`, marking what is not on disk. That mark is
		-- the only place a stale entry gets reported: a texture lookup
		-- failing three layers down is not somewhere the user can act on it.
		fn refreshList keepSel:0 =
		(
			local disp = #()
			for e in entries do
			(
				local full = resolveEntry e
				local missing = (full == "" or not (doesFileExist full))
				append disp (if missing then (e + "      [missing]") else e)
			)
			lstMpq.items = disp
			if keepSel >= 1 and keepSel <= disp.count then lstMpq.selection = keepSel
		)

		-- Store a full path only when it has to be one. An archive sitting in
		-- the Game Data Directory is recorded by name, so the list still
		-- works after the user moves or reinstalls the game.
		fn addPath p =
		(
			if p == undefined or p == "" then return false
			local e = p
			local b = baseDir
			if b != "" then
			(
				if b[b.count] != "\\" then b += "\\"
				if p.count > b.count and (toLower (substring p 1 b.count)) == (toLower b) then
					e = substring p (b.count + 1) -1
			)
			-- Already listed under either spelling: a second copy would only
			-- be dead weight behind the first.
			local target = toLower (resolveEntry e)
			for x in entries do
				if (toLower (resolveEntry x)) == target then return false
			append entries e
			true
		)

		on btnUp pressed do
		(
			local i = lstMpq.selection
			if i > 1 then
			(
				local t = entries[i - 1]
				entries[i - 1] = entries[i]
				entries[i] = t
				refreshList keepSel:(i - 1)
			)
		)

		on btnDown pressed do
		(
			local i = lstMpq.selection
			if i >= 1 and i < entries.count then
			(
				local t = entries[i + 1]
				entries[i + 1] = entries[i]
				entries[i] = t
				refreshList keepSel:(i + 1)
			)
		)

		on btnRemove pressed do
		(
			local i = lstMpq.selection
			if i >= 1 and i <= entries.count then
			(
				deleteItem entries i
				refreshList keepSel:(amin i entries.count)
			)
		)

		on btnAddFile pressed do
		(
			local f = getOpenFileName caption:"Add MPQ Archive" types:"MPQ Archives (*.mpq)|*.mpq|All Files (*.*)|*.*"
			if f != undefined then
			(
				addPath f
				refreshList keepSel:entries.count
			)
		)

		on btnAddFolder pressed do
		(
			local d = getSavePath caption:"Add every .mpq in a folder"
			if d != undefined then
			(
				local added = 0
				local found = getFiles (d + "\\*.mpq")
				sort found
				for f in found do ( if addPath f then added += 1 )
				refreshList keepSel:entries.count
				if added == 0 then
					messageBox "No new .mpq archives in that folder." title:"WhiteoutDex" beep:false
			)
		)

		-- What is actually in the Game Data Directory, in the order a reader
		-- should search it: the retail names first, patch before base, then
		-- anything else the folder holds sorted descending so a mod's archive
		-- still overrides the retail set. Same shape as ScanMpqDirectory() in
		-- storage_browser_primitives.cpp, which is the fallback the native
		-- side uses when this list is not configured - so seeding an unset
		-- list with this changes nothing about what gets read.
		fn scanBaseDir =
		(
			local ordered = #()
			if baseDir == "" then return ordered
			local names = #()
			for f in (getFiles (baseDir + "\\*.mpq")) do append names (filenameFromPath f)
			for k in defaultNames do
			(
				for n in names do
					if (toLower n) == (toLower k) then append ordered n
			)
			local extra = #()
			for n in names do
			(
				local known = false
				for k in defaultNames do
					if (toLower n) == (toLower k) then known = true
				if not known then append extra n
			)
			sort extra
			for i = extra.count to 1 by -1 do append ordered extra[i]
			ordered
		)

		on btnScan pressed do
		(
			if baseDir == "" then
				messageBox "Set the Game Data Directory in Settings first." title:"WhiteoutDex" beep:false
			else
			(
				entries = scanBaseDir()
				refreshList keepSel:1
				if entries.count == 0 then
					messageBox "No .mpq archives in the Game Data Directory." title:"WhiteoutDex" beep:false
			)
		)

		on btnDefaults pressed do
		(
			entries = deepCopy defaultNames
			refreshList keepSel:1
		)

		on btnOk pressed do
		(
			if entries.count == 0 then
				delIniSetting settingsIni "MPQ" "List"
			else
			(
				local joined = ""
				for i = 1 to entries.count do
				(
					if i > 1 then joined += "|"
					joined += entries[i]
				)
				setINISetting settingsIni "MPQ" "List" joined
			)
			-- The MaxScript-side archive handles were opened in the old
			-- order; drop them so the next lookup reopens in the new one.
			if ::WhiteoutDexMPQ != undefined then try (::WhiteoutDexMPQ.closeMPQs()) catch()
			destroyDialog mpqOrderRollout
		)

		on btnCancel pressed do destroyDialog mpqOrderRollout

		on mpqOrderRollout open do
		(
			-- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
			try (WdxSetDialogIcon mpqOrderRollout) catch()
			-- Where a relative entry hangs off: the Settings dialog's own MPQ
			-- directory, else the Reforged/CASC root. Same fallback as
			-- wdx::mpq::ArchiveDirectory - a Classic install keeps its
			-- archives in the install root, so a user who filled in only one
			-- of the two paths still gets a working list.
			baseDir = ""
			if ::WhiteoutDexMPQ != undefined then baseDir = ::WhiteoutDexMPQ.getDirectory()
			if baseDir == "" then baseDir = getINISetting settingsIni "CASC" "W3Path"
			if baseDir == "" then
				baseLbl.text = "No Game Data Directory set - only full paths will resolve."
			else
				baseLbl.text = "Relative to: " + baseDir

			entries = #()
			local raw = getINISetting settingsIni "MPQ" "List"
			if raw != undefined and raw != "" then
			(
				for e in (filterString raw "|") do
				(
					local t = trimLeft (trimRight e)
					if t != "" then append entries t
				)
			)
			else
			(
				-- Nothing configured yet. Seed with the archives that are
				-- already being read rather than with an empty list, so
				-- "add my mod" APPENDS to the game instead of replacing it -
				-- on a Classic install the list is the whole storage, and a
				-- one-entry list would quietly drop War3.mpq and everything
				-- in it. Pressing OK on the seed pins today's set, which is
				-- what an editor's OK should mean; Cancel leaves the key
				-- absent and every reader on its own default.
				entries = scanBaseDir()
				if entries.count == 0 then entries = deepCopy defaultNames
			)
			refreshList keepSel:1
		)
	)

	rollout settingsRollout "WhiteoutDex Settings" width:340 height:512
	(
		local sidebarIni = (::WhiteoutDexInstallRoot) + "\\WhiteoutDex_Settings.ini"
		local settingsIni = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"

		groupBox langGrp "Language / 语言 / Sprache / Язык / 言語 / 언어" pos:[8,4] width:324 height:72

		label langLabel "Interface Language:" pos:[20,28] width:120 height:16
		dropdownList langDDL "" pos:[142,24] width:140 height:20

		label noteLabel "" pos:[20,52] width:260 height:16 style_sunkenedge:false

		groupBox mpqGrp "MPQ Archives (Classic v800)" pos:[8,82] width:324 height:122
		label mpqLabel "Game Data Directory:" pos:[20,102] width:130 height:16
		edittext mpqPathEdt "" pos:[20,120] width:260 height:20 readOnly:true
		button mpqBrowseBtn "..." pos:[284,120] width:38 height:20 tooltip:"Browse for Warcraft III game data folder containing MPQ files"
		label mpqStatusLbl "" pos:[20,146] width:140 height:14
		button mpqDetectBtn "Detect" pos:[166,144] width:76 height:18 tooltip:"Auto-detect the Warcraft III installation"
		button mpqClearBtn "Clear" pos:[248,144] width:72 height:18 tooltip:"Clear the MPQ directory"
		button mpqOrderBtn "Edit Load Order..." pos:[20,168] width:150 height:22 tooltip:"Choose which MPQ archives are searched and in what order, including your own"
		label mpqOrderLbl "" pos:[178,172] width:142 height:16

		groupBox cascGrp "CASC Archives (Reforged v1200)" pos:[8,210] width:324 height:94
		label cascLabel "Warcraft III Reforged:" pos:[20,230] width:140 height:16
		edittext cascPathEdt "" pos:[20,248] width:260 height:20 readOnly:true
		button cascBrowseBtn "..." pos:[284,248] width:38 height:20 tooltip:"Browse for Warcraft III Reforged installation folder"
		label cascStatusLbl "" pos:[20,274] width:140 height:14
		button cascDetectBtn "Detect" pos:[166,272] width:76 height:18 tooltip:"Auto-detect the Warcraft III installation"
		button cascClearBtn "Clear" pos:[248,272] width:72 height:18 tooltip:"Clear the CASC directory"

		groupBox sidebarGrp "Sidebar" pos:[8,310] width:324 height:78
		checkbox chk_sidebarEnabled "Show Sidebar" pos:[20,332] width:120 height:18 tooltip:"Show the WhiteoutDex tool sidebar on startup"
		label lblDockSide "Dock Side:" pos:[160,334] width:60 height:16
		dropdownList ddl_dockSide "" pos:[222,330] width:100 height:20 items:#("Left", "Right")

		groupBox updateGrp "Auto-Update" pos:[8,394] width:324 height:72
		checkbox chk_autoUpdate "Check for updates on startup" pos:[20,416] width:200 height:18 tooltip:"Automatically check GitHub for new versions when 3ds Max starts"
		button btn_checkNow "Check Now" pos:[228,414] width:92 height:22 tooltip:"Manually check for updates now"

		button closeBtn "OK" pos:[248,478] width:82 height:24

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

		-- "Default order" vs "Custom order (N)". Reads the key rather than a
		-- cached count, so it is right after the editor writes, after a
		-- hand-edit of the INI, and on first open alike.
		fn refreshMpqOrderLabel =
		(
			local raw = getINISetting settingsIni "MPQ" "List"
			local n = 0
			if raw != undefined and raw != "" then
			(
				for e in (filterString raw "|") do
					if (trimLeft (trimRight e)) != "" then n += 1
			)
			if n == 0 then
				mpqOrderLbl.text = "Default order"
			else
				mpqOrderLbl.text = "Custom order (" + n as string + ")"
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

		-- Re-run WhiteoutLib's game finder on demand. `force:true` overrides a
		-- path that is already set, so this doubles as a "re-scan after I moved
		-- or reinstalled the game" button.
		on mpqDetectBtn pressed do
		(
			local found = ::WhiteoutDexMPQ.autoDetect force:true
			mpqPathEdt.text = found
			if found == "" then
				mpqStatusLbl.text = ::WdxL.t "set_detect_none_found"
			else if ::WhiteoutDexMPQ.validateDirectory() then
				mpqStatusLbl.text = ::WdxL.t "set_mpq_status_found"
			else
				mpqStatusLbl.text = ::WdxL.t "set_mpq_status_not_found"
		)

		-- Modal, so the summary beside the button is repainted from a settled
		-- INI rather than from whatever the editor had on screen.
		on mpqOrderBtn pressed do
		(
			try (destroyDialog mpqOrderRollout) catch ()
			createDialog mpqOrderRollout modal:true
			refreshMpqOrderLabel()
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
			::WhiteoutDexCASC.w3path = ""
			cascStatusLbl.text = ""
		)

		-- Same as the MPQ side: force a fresh scan and persist whatever
		-- WhiteoutLib's game finder turns up.
		on cascDetectBtn pressed do
		(
			local found = ::WhiteoutDexCASC.autoDetect force:true
			cascPathEdt.text = found
			if found == "" then
				cascStatusLbl.text = ::WdxL.t "set_detect_none_found"
			else if doesFileExist found then
				cascStatusLbl.text = ::WdxL.t "set_casc_status_found"
			else
				cascStatusLbl.text = ::WdxL.t "set_casc_status_not_found"
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
			-- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
			try (WdxSetDialogIcon settingsRollout) catch()
			populateLanguages()
			noteLabel.text = ""
			if ::WhiteoutDexMPQ != undefined then
			(
				-- Empty settings get filled from WhiteoutLib's game finder.
				-- Startup already tried this; retrying here covers the case
				-- where the game was installed after 3ds Max was launched.
				::WhiteoutDexMPQ.autoDetect()
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
			refreshMpqOrderLabel()
			if ::WdxL != undefined then
			(
				mpqLabel.text = ::WdxL.t "set_mpq_directory_lbl"
				mpqBrowseBtn.tooltip = ::WdxL.t "set_mpq_browse_tip"
				mpqClearBtn.text = ::WdxL.t "set_mpq_clear_btn"
				mpqDetectBtn.text = ::WdxL.t "set_detect_btn"
				mpqDetectBtn.tooltip = ::WdxL.t "set_detect_tip"
				mpqOrderBtn.text = ::WdxL.t "set_mpq_order_btn"
				mpqOrderBtn.tooltip = ::WdxL.t "set_mpq_order_tip"
			)
			-- CASC path. WhiteoutLib doesn't require .build.info — check
			-- only that the configured directory still exists.
			if ::WhiteoutDexCASC != undefined then ::WhiteoutDexCASC.autoDetect()
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
				cascDetectBtn.text = ::WdxL.t "set_detect_btn"
				cascDetectBtn.tooltip = ::WdxL.t "set_detect_tip"
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
