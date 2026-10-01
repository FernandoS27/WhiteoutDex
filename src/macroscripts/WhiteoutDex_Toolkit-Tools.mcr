/*
	WhiteoutDex_Toolkit-Tools.mcr
	==============================
	Macroscripts for all WhiteoutDex tools.
	Each tool opens as a standalone dialog window.
	
	Compatibility: 3ds Max 2016 - 2027
*/

macroScript WhiteoutDex_SequenceManager
	category:"WhiteoutDex Toolkit"
	toolTip:"Sequence Manager"
	buttonText:"Sequence Manager"
(
	on execute do
	(
		try (destroyDialog ::WdxSequenceManager.mainRollout) catch()
		try (createDialog ::WdxSequenceManager.mainRollout width:444 height:402 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_VisibilityKeyer
	category:"WhiteoutDex Toolkit"
	toolTip:"Visibility Keyer"
	buttonText:"Visibility Keyer"
(
	on execute do
	(
		try (destroyDialog ::WdxVisibilityKeyer.mainRollout) catch()
		try (createDialog ::WdxVisibilityKeyer.mainRollout width:440 height:242 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_AnimGeneral
	category:"WhiteoutDex Toolkit"
	toolTip:"Anim: General Tools"
	buttonText:"Anim: General Tools"
(
	on execute do
	(
		try (destroyDialog ::WdxAnimTools.generalRoll) catch()
		try (createDialog ::WdxAnimTools.generalRoll width:440 height:254 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_AnimControllers
	category:"WhiteoutDex Toolkit"
	toolTip:"Anim: Controllers"
	buttonText:"Anim: Controllers"
(
	on execute do
	(
		try (destroyDialog ::WdxAnimTools.controllersRoll) catch()
		try (createDialog ::WdxAnimTools.controllersRoll width:440 height:177 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_AnimSkin
	category:"WhiteoutDex Toolkit"
	toolTip:"Anim: Skinning"
	buttonText:"Anim: Skinning"
(
	on execute do
	(
		try (destroyDialog ::WdxAnimTools.skinRoll) catch()
		try (createDialog ::WdxAnimTools.skinRoll width:440 height:92 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_AnimScaler
	category:"WhiteoutDex Toolkit"
	toolTip:"Anim: Model Scaler"
	buttonText:"Anim: Model Scaler"
(
	on execute do
	(
		try (destroyDialog ::WdxAnimTools.scalerRoll) catch()
		try (createDialog ::WdxAnimTools.scalerRoll width:440 height:172 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_SkinChanger
	category:"WhiteoutDex Toolkit"
	toolTip:"Skin Changer"
	buttonText:"Skin Changer"
(
	on execute do
	(
		try (destroyDialog ::WdxSkinChanger.mainRollout) catch()
		try (createDialog ::WdxSkinChanger.mainRollout width:440 height:245 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_GridDummy
	category:"WhiteoutDex Toolkit"
	toolTip:"Grid & Dummy Creator"
	buttonText:"Grid & Dummy Creator"
(
	on execute do
	(
		try (destroyDialog ::WdxGridAndDummyCreator.mainRollout) catch()
		try (createDialog ::WdxGridAndDummyCreator.mainRollout width:440 height:142 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_ObjectTools
	category:"WhiteoutDex Toolkit"
	toolTip:"Object Manipulation Tools"
	buttonText:"Object Manipulation Tools"
(
	on execute do
	(
		try (destroyDialog ::WdxObjectManipulationTools.mainRollout) catch()
		try (createDialog ::WdxObjectManipulationTools.mainRollout width:440 height:362 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_NodeManager
	category:"WhiteoutDex Toolkit"
	toolTip:"Node Manager"
	buttonText:"Node Manager"
(
	on execute do
	(
		try (destroyDialog ::WdxNodeManager.mainRollout) catch()
		try (createDialog ::WdxNodeManager.mainRollout width:420 height:365 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_ObjectSettings
	category:"WhiteoutDex Toolkit"
	toolTip:"Object Settings"
	buttonText:"Object Settings"
(
	on execute do
	(
		try (destroyDialog ::WdxObjectSettings.mainRollout) catch()
		try (createDialog ::WdxObjectSettings.mainRollout width:440 height:232 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)) catch()
	)
)

macroScript WhiteoutDex_Renderer
	category:"WhiteoutDex Toolkit"
	toolTip:"Wc3 Renderer"
	buttonText:"Renderer"
(
	on execute do
	(
		try
		(
			WhiteoutFlakesStart()
			-- Hand the renderer the active sequence ranges so its popcorn
			-- animVisibilityGuide gate has a current-sequence name to
			-- match against. Lives in SequenceManager.ms because the
			-- previewer's own Resync button calls the same function.
			try
			(
				if ::WdxPushRendererSequences == undefined then
					format "WDX Renderer: WdxPushRendererSequences is undefined — no sequences pushed\n"
				else
					WdxPushRendererSequences()
			)
			catch
			(
				format "WDX Renderer: sequence push failed: %\n" (getCurrentException())
			)
		)
		catch (messageBox (::WdxL.t "tools_renderer_failed_msg"))
	)
)

macroScript WhiteoutDex_KeyframeOptimizer
	category:"WhiteoutDex Toolkit"
	toolTip:"Keyframe Optimizer"
	buttonText:"KF Optimizer"
(
	on execute do
	(
		try
		(
			-- Keyframe_Optimizer.ms is auto-loaded on Max startup (it lives in
			-- post_startup_scripts), so wdxOpenKfoDialog should already be defined.
			-- Re-fileIn defensively in case the user installed mid-session.
			if ::wdxOpenKfoDialog == undefined do
			(
				local scriptPath = (::WhiteoutDexInstallRoot) + "\\scripts\\post_startup_scripts\\Keyframe_Optimizer.ms"
				fileIn scriptPath
			)
			::wdxOpenKfoDialog()
		) catch (messageBox ((::WdxL.t "tools_kfo_error_msg") + "\n" + (getCurrentException())))
	)
)

macroScript WhiteoutDex_CellShadeCreator
	category:"WhiteoutDex Toolkit"
	toolTip:"Cell Shade Creator"
	buttonText:"Cell Shade"
(
	on execute do
	(
		try
		(
			if ::wdxOpenCellShadeDialog == undefined do
			(
				local scriptPath = (::WhiteoutDexInstallRoot) + "\\scripts\\post_startup_scripts\\Cell_Shade_Creator.ms"
				fileIn scriptPath
			)
			::wdxOpenCellShadeDialog()
		) catch (messageBox ((::WdxL.t "tools_cellshade_error_msg") + "\n" + (getCurrentException())))
	)
)

macroScript WhiteoutDex_MaterialFix
	category:"WhiteoutDex Toolkit"
	toolTip:"Material Fix Settings"
	buttonText:"Material Fix"
(
	on execute do
	(
		try
		(
			-- MaterialFix.ms is a post-startup script; load it defensively in
			-- case the toolkit was installed mid-session.
			if ::WdxMaterialFix == undefined do
				fileIn ((::WhiteoutDexInstallRoot) + "\\scripts\\post_startup_scripts\\MaterialFix.ms")
			::WdxMaterialFix.open()
		) catch (messageBox ((::WdxL.t "matfix_open_error_msg") + "\n" + (getCurrentException())))
	)
)

macroScript WhiteoutDex_TextureAnimCreator
	category:"WhiteoutDex Toolkit"
	toolTip:"Texture Animation Creator"
	buttonText:"Texture Anim"
(
	on execute do
	(
		try
		(
			if ::WdxTexAnimCreator == undefined do
				fileIn ((::WhiteoutDexInstallRoot) + "\\scripts\\post_startup_scripts\\TextureAnimCreator.ms")
			::WdxTexAnimCreator.open()
		) catch (messageBox ((::WdxL.t "tac_open_error_msg") + "\n" + (getCurrentException())))
	)
)
