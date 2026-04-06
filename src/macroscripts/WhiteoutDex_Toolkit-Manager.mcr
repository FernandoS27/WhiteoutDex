macroScript WhiteoutDex_Manager
buttonText:"WhiteoutDex Manager"
category:"WhiteoutDex Toolkit"
internalCategory:"WhiteoutDex Toolkit"
(
	-- Includes
-- include "WhiteoutDexModules\\Wc3Model.ms"
-- include "WhiteoutDexModules\\UtilityFunctions.ms"
	-- end Includes
	
-- include "SequenceManager.ms"
-- include "NodeManager.ms"
-- include "AnimTools.ms"
-- include "GridAndDummyCreator.ms"
-- include "FBXAnimationSaver.ms"
-- include "SkinChanger.ms"
-- include "VisibilityKeyer.ms"
-- include "TeamColorManager.ms"

	
	fn getIniSettingWithClass filename sectionString keyString valueClass defaultValue =
	(
		-- get the ini setting as usual
		local settingString = getINISetting filename sectionString keyString
		-- if no value is found for whatever reason the defaultvalue is returned
		if settingString == "" then return defaultValue
		-- if the class is a string then just return the string if it is not empty
		if valueClass == String then return settingString
		-- try to get the executed value of the ini string and if execution fails return defaultValue
		try (
			local val = if (maxVersion())[1] >= 24000 then
				safeExecute settingString ignoreSSSEState:true
			else
				execute settingString
		)
		catch return defaultValue
		-- only if the class of the value matches the given valueClass then return the value from the ini file
		if classOf val == valueClass then return val else return defaultValue
	)
	
	-- Ini Settings File
	global whiteoutDexManagerINIFilename = getDir #plugcfg + "\\" + "WhiteoutDexManager.ini"
	---------------------------------------------------------------------------------------
	-- Macro
	---------------------------------------------------------------------------------------
	global whiteoutdexManagerFloater
	
	-- Define a rollout for saving position, size and rolled up states
	-- since roloutfloater need a rollout for this
	rollout whiteoutdexManagerFloaterRollout "WhiteoutDex Manager Floater Rollout"
	(
		local floater
		
		on whiteoutdexManagerFloaterRollout open do
		(
			floater = whiteoutdexManagerFloaterRollout.rolloutFloater
			
			print "WhiteoutDex Manager Opened."
		)
		
		on whiteoutdexManagerFloaterRollout oktoclose do
		(
			-- safe the position, size and rolled up states of the rollouts to ini file
			setINISetting whiteoutDexManagerINIFilename "Rollout Floater" "Pos" (floater.pos as string)
			setINISetting whiteoutDexManagerINIFilename "Rollout Floater" "Size" (floater.size as string)
			
			for rl in floater.rollouts do
			(
				setINISetting whiteoutDexManagerINIFilename rl.title "RolledUp" ((not rl.open) as string)
			)
			
			true -- return true to let the rollout be closed!!!
		)
		
		on whiteoutdexManagerFloaterRollout close do
		(
			print "WhiteoutDex Manager Closed."
		)
	)
	
	on execute do
	(
		local whiteoutdexManagerRollouts = #(
			::SequenceManager.mainRollout,
			::VisibilityKeyer.mainRollout,
			::AnimTools.generalRoll,
			::AnimTools.controllersRoll,
			::SkinChanger.mainRollout,
			::GridAndDummyCreator.mainRollout,
			::ObjectManipulationTools.mainRollout,
			::AnimTools.skinRoll,
			::AnimTools.scalerRoll,
			::NodeManager.mainRollout,
			::TeamColorManager.mainRollout,
			::ObjectSettings.mainRollout
		)
	
		if whiteoutdexManagerFloater == undefined or not whiteoutdexManagerFloater.open then
		(
			-- Initial Values
			local pos = getIniSettingWithClass whiteoutDexManagerINIFilename "Rollout Floater" "Pos" Point2 [0, 0]
			local size = getIniSettingWithClass whiteoutDexManagerINIFilename "Rollout Floater" "Size" Point2 [456, 800]
			
			-- Localized floater title
			local floaterTitle = "WhiteoutDex Manager"
			if ::L != undefined then floaterTitle = ::L.t "mgr_whiteoutdex_manager_macbtn"
			
			whiteoutdexManagerFloater = newrolloutfloater floaterTitle 456 size.y pos.x pos.y
			
			-- Suppress redraws while adding rollouts
			try (whiteoutdexManagerFloater.lock = true) catch()
			
			-- add rollouts from array
			for rl in whiteoutdexManagerRollouts do
			(
				-- only add the rollout if it is not already displayed in another dialog or floater
				if rl.isDisplayed then continue
				
				-- get the rolled up state from ini files
				local rolledUpState = getIniSettingWithClass whiteoutDexManagerINIFilename rl.title "RolledUp" BooleanClass false
				addrollout rl whiteoutdexManagerFloater rolledUp:rolledUpState
			)
			
			-- add the hidden rollout for saving ini settings
			addrollout whiteoutdexManagerFloaterRollout whiteoutdexManagerFloater rolledUp:true border:false
			
			-- Resume redraws
			try (whiteoutdexManagerFloater.lock = false) catch()
			
		)
		else
		(
			if classof whiteoutdexManagerFloater == RolloutFloater \
				and whiteoutdexManagerFloater.open then
			(
				if (windows.getWindowPlacement whiteoutdexManagerFloater)[1] == #showMinimized then
				(
					windows.showWindow whiteoutdexManagerFloater #Restore
				)
			)
		)
	)
)
