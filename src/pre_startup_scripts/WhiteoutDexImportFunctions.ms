/*
	WhiteoutDex Import Functions
	=======================
	Contains all import logic separated from the UI.
	This file is loaded as a pre-start-up script and provides:
	  - ImporterSettings struct
	  - WhiteoutDexExecuteImport function
	
	The UI remains in the macroscript (WhiteoutDex_Toolkit-WhiteoutDex_Importer.mcr)
*/

global WhiteoutDexImportSettings
global WhiteoutDexImportedModel
global WhiteoutDexOrigBindPoses

struct ImporterSettings
(
	importMaterials = true,
	importSkinning = true,
	importAnimations = true,
	importObjects = true,
	importBones = true,
	importHelpers = true,
	importLights = true,
	importAttachments = true,
	importPE1 = true,
	importPE2 = true,
	importRE = true,
	importEO = true,
	importCS = true,
	importCameras = true,
	importTransAnim = true,
	importRotAnim = true,
	importScaleAnim = true,
	importTXAnim = true,
	importUVAnim = true,
	importVisAnim = true,
	importColorAnim = true,
	importParamAnim = true,
	importHelpersAsMaxBones = true,
	importBonesAsMaxBones = true,
	importHelpersAsPointHelpers = true,
	optimizeGeometry = true,
	optimizeBonesHelpers = true,
	searchMPQ = false,
	importFormat = #WhiteoutDex,
	-- Reforged v1200
	importCornEmitters = true,
	importFaceFX = true,
	searchCASC = false
	
)

/*
	WhiteoutDexReadMDX
	Reads an MDX file and stores the result in ::WhiteoutDexImportedModel
	
	Parameters:
		infile_name  - Full path to the .mdx file
		debugMode    - true = standard mode (with try/catch), false = debug mode (no error handling)
	
	Returns:
		true if successful, false if error occurred
*/
fn WhiteoutDexReadMDX infile_name debugMode =
(
    local noError = true
    local ver = peekMDXVersion infile_name
    format "WhiteoutDex: MDX version %\n" ver
    
    local readFunc
    if ver >= 1100 then
        readFunc = ::WhiteoutDexMDXReaderV1200.InputMDX
    else
        readFunc = ::WhiteoutDexMDXReader.InputMDX
    
    if debugMode then
    (
        try ::WhiteoutDexImportedModel = readFunc infile_name
        catch
        (
            messageBox "Import Failed: Error while reading the mdx file\n Please open the listener and report the bug"
            format "*** % ***\n" (getCurrentException()) to:listener
            noError = false
        )
    )
    else
    (
        ::WhiteoutDexImportedModel = readFunc infile_name
    )
    
    noError
)

/*
	WhiteoutDexExecuteImport
	Executes the import process: relinks, optimizes and rebuilds the scene.
	
	Parameters:
		model         - The parsed WC3 model (::WhiteoutDexImportedModel)
		modelpath     - The directory path of the source file
		mode          - true = standard mode (with try/catch), false = debug mode
		progressLabel - UI label control for progress display
		progressBar   - UI progressbar control for progress display
	
	Returns:
		true if successful, false if error occurred
*/
fn WhiteoutDexExecuteImport model modelpath mode progressLabel progressBar =
(
	local timeStart = timestamp()
	local noError = true

	::Progress.initiate progressLabel progressBar
	
	if mode then
	(
		try
		(
			::Progress.downLevel 50 1
			with redraw off (::ModelRelinker.relinkModel model)
			::Progress.updateItem "Cleaning up..."
			gc ()
			::Progress.upLevel ()
			::Progress.updateItem "Optimizing Model..."
			::Optimizer.optimizeImportModel model ::WhiteoutDexImportSettings.optimizeGeometry ::WhiteoutDexImportSettings.optimizeBonesHelpers
			::Progress.downLevel 50 1
			::SceneRebuilder.rebuildSceneModel model modelpath
			::Progress.upLevel ()
			::Progress.finalize ()
		)
		catch
		(
			messageBox "Export Failed: Error while rebuilding scene\n Please open the listener and report the bug"
			format "*** % ***\n" (getCurrentException()) to:listener
			noError = false
		)
	)
	else
	(
		MXSDebugger.showFirstFrameOnly = true
		::Progress.downLevel 50 1
		with redraw off
		(
			::ModelRelinker.relinkModel model
		)
		gc()
		::Progress.upLevel()
		::Progress.updateItem "Optimizing Model..."
		::Optimizer.optimizeImportModel model ::WhiteoutDexImportSettings.optimizeGeometry ::WhiteoutDexImportSettings.optimizeBonesHelpers
		::Progress.downLevel 50 1
		::SceneRebuilder.rebuildSceneModel model modelpath
		::Progress.upLevel()
		::Progress.finalize()
	)
	
	local message = "Total Time: " + (((timestamp () - timeStart) / 1000.0) as string) + "s."
	
	#(noError, message)
)

/*
	WhiteoutDexPostImportActions
	Runs post-import actions like updating sequence manager and viewport settings.
*/
fn WhiteoutDexPostImportActions =
(
	-- Update Sequences in SequenceManager
	try ::SequenceManager.sceneChangeCallback() catch()
	
	-- Viewport settings
	actionMan.executeAction 0 "554"  -- Views: Viewport Visual Style Consistent Colors
	actionMan.executeAction -844228238 "12"  -- Viewport Lighting and Shadows: High Quality
	actionMan.executeAction -844228238 "17"  -- Viewport Lighting and Shadows: Progressive Skylight
	actionMan.executeAction -844228238 "5"  -- Viewport Lighting and Shadows: Viewport Shadows Toggle
	actionMan.executeAction -844228238 "0"  -- Viewport Lighting and Shadows: Viewport Shadows Ambient Occlusion Toggle
	actionMan.executeAction 0 "63545"  -- Views: Viewport Materials Display as Shaded with Maps
)
