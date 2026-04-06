/*
	WhiteoutDex Export Functions
	=======================
	Contains all export logic separated from the UI.
	This file is loaded as a pre-start-up script and provides:
	  - WhiteoutDexSceneMonitorStruct (scene problem detection & fixing)
	  - WhiteoutDexExportSettings struct
	  - exportScene function
	  - getIncrementedFilename function
	
	The UI remains in the macroscript (WhiteoutDex_Toolkit-WhiteoutDex_Exporter.mcr)
*/

/* ============================================================================
   FORWARD DECLARATIONS
   ============================================================================ */
global WhiteoutDexSceneMonitorStruct
global whiteoutDexSceneMonitorInstance
global theDuplicates = #()
global thePolyProblems = #()
global theBoneProblems = #()
global theMaterialProblems = #()
global allProblemsArray = #()

/* ============================================================================
   WHITEOUTDEX SCENE MONITOR STRUCT DEFINITION
   ============================================================================ */
struct WhiteoutDexSceneMonitorStruct
(
	-- ========================================
	-- PRIVATE PROPERTIES
	-- ========================================
	private lastSceneNodeCount = 0,
	private lastSceneChangeTime = 0,
	private cachedProblems = undefined,
	private scanInterval = 3000,
	private forceRefresh = false,
	
	-- ========================================
	-- PUBLIC PROPERTIES (Problem Arrays)
	-- ========================================
	public duplicates = #(),
	public polyProblems = #(),
	public boneProblems = #(),
	public materialProblems = #(),
	
	-- ========================================
	-- HELPER FUNCTIONS
	-- ========================================
	
	-- Check if scene has changed
	fn hasSceneChanged =
	(
		if forceRefresh then
		(
			forceRefresh = false
			return true
		)
		
		local currentNodeCount = objects.count
		local currentTime = timestamp()
		
		if currentNodeCount != lastSceneNodeCount then
		(
			lastSceneNodeCount = currentNodeCount
			lastSceneChangeTime = currentTime
			return true
		)
		
		-- Auto-check every 2 seconds for controller changes
		if (currentTime - lastSceneChangeTime) > 2000 then
		(
			lastSceneChangeTime = currentTime
			return true
		)
		
		return false
	),
	
	-- Force a refresh on next check
	fn invalidateCache =
	(
		cachedProblems = undefined
		forceRefresh = true
	),
	
	-- ========================================
	-- DETECTION FUNCTIONS
	-- ========================================
	
	-- Find duplicate object names
	fn findDuplicates =
	(
		local nameDict = dotNetObject "System.Collections.Generic.Dictionary`2[System.String,System.Int32]"
		local result = #()
		
		for obj in objects do
		(
			local objName = obj.name
			try
			(
				if nameDict.ContainsKey objName then
				(
					local count = nameDict.Item[objName]
					if count == 1 then
						appendIfUnique result objName
					nameDict.Item[objName] = count + 1
				)
				else
					nameDict.Add objName 1
			)
			catch
			(
				-- Fallback for .NET issues
				if (findItem result objName) == 0 then
				(
					for o in objects where o != obj and o.name == objName do
					(
						appendIfUnique result objName
						exit
					)
				)
			)
		)
		return result
	),
	
	-- Find mesh problems (Editable Poly, Empty meshes)
	fn findMeshProblems =
	(
		local result = #()
		local geoArray = geometry as array
		
		for obj in geoArray do
		(
			local baseObjClass = classof obj.baseobject
			
			case baseObjClass of
			(
				Editable_Poly:
				(
					-- Skip wenn Turn_to_Mesh oder Edit_Mesh im Stack ist
					-- Dann ist die Poly bereits effektiv ein Mesh
					local hasConverter = false
					for i = 1 to obj.modifiers.count do
					(
						local modClass = classOf obj.modifiers[i]
						if modClass == Turn_to_Mesh or modClass == Edit_Mesh then
						( hasConverter = true; exit )
					)
					if not hasConverter then
						append result ("[POLY] " + obj.name)
				)
				Editable_Mesh: 
				(
					try
					(
						if obj.mesh.numfaces == 0 then
							append result ("[EMPTY] " + obj.name)
					)
					catch
					(
						local tempMesh = snapshotAsMesh obj
						local isEmpty = tempMesh.numfaces == 0
						delete tempMesh
						if isEmpty then
							append result ("[EMPTY] " + obj.name)
					)
				)
			)
			
			-- Detect Edit_Mesh modifier above Skin modifier
			local skinIdx = 0
			local editMeshAboveSkin = false
			for i = 1 to obj.modifiers.count do
			(
				if classOf obj.modifiers[i] == Skin then
					skinIdx = i
			)
			if skinIdx > 0 then
			(
				for i = 1 to (skinIdx - 1) do
				(
					if classOf obj.modifiers[i] == Edit_Mesh then
					(
						editMeshAboveSkin = true
						exit
					)
				)
			)
			if editMeshAboveSkin then
				append result ("[EDIT_MESH] " + obj.name)
		)
		return result
	),
	
	-- Check if object is Biped or CAT bone
	fn isBipedOrCATBone obj =
	(
		local objClass = classof obj
		local bipedCATClasses = #(
			Biped_Object,
			CATParent,
			HubObject,
			CATBone
		)
		if (findItem bipedCATClasses objClass) != 0 then return true
		
		-- Prüft ob ein Controller-Name auf Biped/CAT Patterns passt
		fn isRigController conClassName =
		(
			matchPattern conClassName pattern:"*Bip*" or \
			matchPattern conClassName pattern:"*Xtra*" or \
			matchPattern conClassName pattern:"*Vertical_Horizontal_Turn*" or \
			matchPattern conClassName pattern:"*CAT*" or \
			matchPattern conClassName pattern:"*LayerMatrix*" or \
			matchPattern conClassName pattern:"*IKTarget*"
		)
		
		try
		(
			-- Top-Level Controller prüfen
			local conClassName = (classof obj.controller) as string
			if isRigController conClassName then return true
			
			-- Sub-Controller prüfen (2 Ebenen tief)
			for i = 1 to obj.controller.numSubs do
			(
				local sub = getSubAnim obj.controller i
				if sub != undefined and sub.controller != undefined then
				(
					local subName = (classof sub.controller) as string
					if isRigController subName then return true
					
					-- Eine Ebene tiefer
					for j = 1 to sub.controller.numSubs do
					(
						local sub2 = getSubAnim sub.controller j
						if sub2 != undefined and sub2.controller != undefined then
						(
							local sub2Name = (classof sub2.controller) as string
							if isRigController sub2Name then return true
						)
					)
				)
			)
		)
		catch ()
		
		return false
	),
	
	-- Check if object is bone or helper
	fn isBoneOrHelper obj =
	(
		local boneClassNames = #("BoneGeometry")
		local objClassName = (classof obj) as string
		local isBoneClass = (findItem boneClassNames objClassName) != 0
		local isDummy = (classof obj == Dummy)
		local isHelper = (superclassof obj == Helper)
		local isBipedCAT = isBipedOrCATBone obj
		
		return (isBoneClass or isDummy or isHelper or isBipedCAT)
	),
	
	-- Check if object is IK related
	fn isIKRelated obj =
	(
		local objClass = classof obj
		
		if objClass == IK_Chain_Object then return true
		
		local ikHelperClasses = #(
			IK_Chain_Object,
			IKChainObject,
			IKChainControl,
			HI_IKGoal,
			HD_IKGoal,
			IKSlave
		)
		
		if (findItem ikHelperClasses objClass) != 0 then return true
		
		-- Check transform controller
		if obj.transform != undefined then
		(
			local transformClass = classof obj.transform
			if transformClass == IKChainControl then return true
		)
		
		-- Check position/rotation controllers
		if obj.controller != undefined then
		(
			local controllerClass = classof obj.controller
			
			local ikControllerClasses = #(
				IKChainControl,
				IKControl,
				HI_IKControl,
				HD_IKControl,
				IKLimbControl,
				IK_Solver,
				IKSys
			)
			
			if (findItem ikControllerClasses controllerClass) != 0 then return true
			
			-- Check sub-controllers
			if (isProperty obj.controller #position) then
			(
				if obj.controller.position != undefined then
				(
					if obj.controller.position.controller != undefined then
					(
						local posCtrlClass = classof obj.controller.position.controller
						if (findItem ikControllerClasses posCtrlClass) != 0 then return true
					)
				)
			)
			
			if (isProperty obj.controller #rotation) then
			(
				if obj.controller.rotation != undefined then
				(
					if obj.controller.rotation.controller != undefined then
					(
						local rotCtrlClass = classof obj.controller.rotation.controller
						if (findItem ikControllerClasses rotCtrlClass) != 0 then return true
					)
				)
			)
		)
		
		-- Check IK properties
		if (isProperty obj #ikEnabled) then
		(
			if obj.ikEnabled == true then return true
		)
		
		if (isProperty obj #ikChainEnabled) then
		(
			if obj.ikChainEnabled == true then return true
		)
		
		return false
	),
	
	-- Check if object is Warcraft plugin
	fn isWarcraftPlugin obj =
	(
		local objClass = classof obj
		local warcraftClasses = #(
			BlizzParticle1,
			BlizzParticle2,
			BlizRibbon,
			Wc3_AttachPoint,
			Wc3CollisionSphere,
			Wc3CollisionBox,
			Wc3_Light,
			Wc3_Event
		)
		return (findItem warcraftClasses objClass) != 0
	),
	
	-- Check if object has valid PRS controller
	fn hasValidPRS obj =
	(
		if isBipedOrCATBone obj then return true
		if isIKRelated obj then return true
		if isWarcraftPlugin obj then return true
		
		-- Link Constraint wird beim Export automatisch gebacken
		if classof obj.controller == Link_Constraint then return true
		
		if obj.controller == undefined do return false
		if (classof obj.controller != PRS) then return false
		
		local posC = obj.controller.position.controller
		local rotC = obj.controller.rotation.controller
		local sclC = obj.controller.scale.controller
		
		if (posC == undefined or rotC == undefined or sclC == undefined) then return false
		
		local allowedPos = #(Linear_Position, TCB_Position, Bezier_Position, Position_XYZ)
		local allowedRot = #(Linear_Rotation, TCB_Rotation, Bezier_Rotation, Euler_XYZ)
		local allowedScl = #(Linear_Scale, TCB_Scale, Bezier_Scale, ScaleXYZ)
		
		local posOK = (findItem allowedPos (classof posC)) != 0
		local rotOK = (findItem allowedRot (classof rotC)) != 0
		local sclOK = (findItem allowedScl (classof sclC)) != 0
		
		return (posOK and rotOK and sclOK)
	),
	
	-- Find bone controller problems
	fn findBoneControllerProblems =
	(
		local result = #()
		local objArray = objects as array
		local relevantObjs = for obj in objArray where isBoneOrHelper obj collect obj
		
		for obj in relevantObjs do
		(
			if not hasValidPRS obj do
				append result obj
		)
		return result
	),
	
	-- Find material problems
	fn findMaterialProblems =
	(
		local problemMats = #()
		local checkedMats = #{}
		
		for obj in geometry do
		(
			if obj.material != undefined then
			(
				local matIndex = findItem sceneMaterials obj.material
				if matIndex > 0 and not checkedMats[matIndex] then
				(
					checkedMats[matIndex] = true
					local m = obj.material
					
					-- Check for problematic material types
					if (classof m == Physical_Material) or \
					(classof m == StandardMaterial) or \
					(matchPattern (classof m as string) pattern:"*OpenPBR*") or \
					(matchPattern (classof m as string) pattern:"*PBR*") then
						append problemMats m
				)
			)
		)
		return problemMats
	),
	
	-- ========================================
	-- FIX FUNCTIONS
	-- ========================================
	
	-- Fix duplicate names
	fn fixDuplicateNames =
	(
		undo "Fix Duplicate Names" on
		(
			local nameDict = dotNetObject "System.Collections.Generic.Dictionary`2[System.String,System.Collections.Generic.List`1[System.Object]]"
			
			for obj in objects do
			(
				local objName = obj.name
				try
				(
					if nameDict.ContainsKey objName then
					(
						local nodeList = nameDict.Item[objName]
						nodeList.Add obj
					)
					else
					(
						local nodeList = dotNetObject "System.Collections.Generic.List`1[System.Object]"
						nodeList.Add obj
						nameDict.Add objName nodeList
					)
				)
				catch
				(
					-- Fallback ohne .NET
					local found = false
					for g in (objects as array) where g.name == objName and g != obj do
					(
						found = true
						exit
					)
					if found then
					(
						obj.name = uniqueName objName
					)
				)
			)
			
			-- Umbenennen: Nur Gruppen mit mehr als 1 Eintrag
			local enumerator = nameDict.GetEnumerator()
			while enumerator.MoveNext() do
			(
				local baseName = enumerator.Current.Key
				local nodeList = enumerator.Current.Value
				if nodeList.Count > 1 then
				(
					for i = 1 to nodeList.Count - 1 do
					(
						local node = nodeList.Item[i]
						if isValidNode node then
							node.name = baseName + "_" + ((i + 1) as string)
					)
				)
			)
		)
	),
	
	-- Fix mesh problems
	fn fixMeshProblems =
	(
		undo "Fix Mesh Problems" on
		(
			local geoArray = geometry as array
			local toDelete = #()
			
			for obj in geoArray where isValidNode obj do
			(
				-- Handle Edit_Mesh above Skin
				local skinIdx = 0
				local editMeshIdx = 0
				for i = 1 to obj.modifiers.count do
				(
					if classOf obj.modifiers[i] == Skin and skinIdx == 0 then
						skinIdx = i
				)
				if skinIdx > 0 then
				(
					for i = 1 to (skinIdx - 1) do
					(
						if classOf obj.modifiers[i] == Edit_Mesh then
						(
							editMeshIdx = i
							exit
						)
					)
				)
				
				if editMeshIdx > 0 and skinIdx > 0 then
				(
					-- Step 1: Save modifiers above Edit_Mesh
					local modsAbove = #()
					for i = 1 to (editMeshIdx - 1) do
						append modsAbove obj.modifiers[i]
					
					-- Step 2: Save Skin and modifiers below Skin
					local modsAtAndBelowSkin = #()
					for i = skinIdx to obj.modifiers.count do
						append modsAtAndBelowSkin obj.modifiers[i]
					
					-- Step 3: Save modifiers between Edit_Mesh and Skin
					local modsBetween = #()
					for i = (editMeshIdx + 1) to (skinIdx - 1) do
						append modsBetween obj.modifiers[i]
					
					-- Step 4: Delete modifiers above Edit_Mesh
					for i = 1 to modsAbove.count do
						deleteModifier obj 1
					
					-- Step 5: Delete modifiers between Edit_Mesh and base (Skin, between, below)
					local countBelow = modsBetween.count + modsAtAndBelowSkin.count
					for i = 1 to countBelow do
						deleteModifier obj 2
					
					-- Step 6: Collapse Edit_Mesh into base mesh
					maxOps.CollapseNodeTo obj 1 false
					
					-- Step 7: Re-add Skin and below modifiers (bottom to top)
					for i = modsAtAndBelowSkin.count to 1 by -1 do
						addModifier obj modsAtAndBelowSkin[i]
					
					-- Step 8: Re-add between modifiers (bottom to top)
					for i = modsBetween.count to 1 by -1 do
						addModifier obj modsBetween[i]
					
					-- Step 9: Re-add modifiers above Edit_Mesh (bottom to top)
					for i = modsAbove.count to 1 by -1 do
						addModifier obj modsAbove[i]
					
					continue
				)
				
				-- Handle Editable_Poly base
				local baseObjClass = classof obj.baseobject
				
				if baseObjClass == Editable_Poly then
				(
					-- Skip wenn Turn_to_Mesh oder Edit_Mesh bereits im Stack
					local hasConverter = false
					for i = 1 to obj.modifiers.count do
					(
						local modClass = classOf obj.modifiers[i]
						if modClass == Turn_to_Mesh or modClass == Edit_Mesh then
						(
							hasConverter = true
							exit
						)
					)
					
					if not hasConverter then
					(
						-- Modifiers sichern
						local theMods = #()
						for i = 1 to obj.modifiers.count do
							append theMods obj.modifiers[i]
						
						local c = obj.modifiers.count
						while c > 0 do
						(
							deleteModifier obj c
							c -= 1
						)
						
						convertToMesh obj
						
						for i = theMods.count to 1 by -1 do
							addModifier obj theMods[i]
					)
					
					-- Nach Konvertierung: Prüfe ob Mesh leer ist
					baseObjClass = classof obj.baseobject
				)
				
				if baseObjClass == Editable_Mesh then
				(
					local tempMesh = snapshotAsMesh obj
					local isEmpty = tempMesh.numfaces == 0
					delete tempMesh
					if isEmpty then
						append toDelete obj
				)
			)
			
			-- Leere Meshes löschen
			for obj in toDelete where isValidNode obj do
				delete obj
		)
	),
	
	-- Fix bone controllers
	fn fixBoneControllers =
	(
		local allowedPos = #(Linear_Position, TCB_Position, Bezier_Position, Position_XYZ)
		local allowedRot = #(Linear_Rotation, TCB_Rotation, Bezier_Rotation, Euler_XYZ)
		local allowedScl = #(Linear_Scale, TCB_Scale, Bezier_Scale, ScaleXYZ)
		
		undo "Fix Bone Controllers" on
		(
			for obj in boneProblems do
			(
				if not isBipedOrCATBone obj then
				(
					if not isIKRelated obj then
					(
						if not isWarcraftPlugin obj then
						(
							if (obj.controller != undefined) AND (classof obj.controller == PRS) then
							(
								local posC = obj.controller.position.controller
								local rotC = obj.controller.rotation.controller
								local sclC = obj.controller.scale.controller
								
								if posC != undefined and (findItem allowedPos (classof posC)) == 0 then
									obj.controller.position.controller = Linear_Position()
								
								if rotC != undefined and (findItem allowedRot (classof rotC)) == 0 then
									obj.controller.rotation.controller = Linear_Rotation()
								
								if sclC != undefined and (findItem allowedScl (classof sclC)) == 0 then
									obj.controller.scale.controller = Linear_Scale()
							)
						)
					)
				)
			)
		)
	),
	
	-- Fix materials to Warcraft3
	fn fixMaterials =
	(
		-- Load material fix settings
		local iniPath = (getDir #plugcfg) + "\\MDLXExporter.ini"
		local applyUnshaded = false
		local applyUnfogged = false
		local applyTwosided = false
		local applyFilterMode = 1
		local applyPrefixPath = ""
		local applyUTile = false
		local applyVTile = false
		
		-- Read settings from INI
		local unshaded = getINISetting iniPath "MaterialFix" "Unshaded"
		local unfogged = getINISetting iniPath "MaterialFix" "Unfogged"
		local twosided = getINISetting iniPath "MaterialFix" "Twosided"
		local filterMode = getINISetting iniPath "MaterialFix" "FilterMode"
		local prefixPath = getINISetting iniPath "MaterialFix" "PrefixPath"
		local uTile = getINISetting iniPath "MaterialFix" "UTile"
		local vTile = getINISetting iniPath "MaterialFix" "VTile"
		
		if unshaded != "" then applyUnshaded = (unshaded == "true")
		if unfogged != "" then applyUnfogged = (unfogged == "true")
		if twosided != "" then applyTwosided = (twosided == "true")
		if filterMode != "" then applyFilterMode = (filterMode as integer)
		if prefixPath != "" then applyPrefixPath = prefixPath
		if uTile != "" then applyUTile = (uTile == "true")
		if vTile != "" then applyVTile = (vTile == "true")
		
		undo "Fix Materials to Wc3" on
		(
			for m in materialProblems do
			(
				local newMat = Warcraft3()
				newMat.name = m.name + "_wc3"
				
				-- Apply saved settings
				newMat.unshaded = applyUnshaded
				newMat.unfogged = applyUnfogged
				newMat.twosides = applyTwosided
				newMat.filtermode = applyFilterMode
				if applyPrefixPath != "" then newMat.path = applyPrefixPath
				
				-- OpenPBR Material conversion
				if (matchPattern (classof m as string) pattern:"*OpenPBR*") or \
				(matchPattern (classof m as string) pattern:"*PBR*") then
				(
					-- Try to extract base color map
					local baseColorMap = undefined
					local baseColor = color 128 128 128
					
					-- Check for various property names that OpenPBR might use
					local possibleColorMapProps = #(#baseColorMap, #base_color_map, #baseColor, #base_color, #diffuseMap, #diffuse_map, #albedoMap, #albedo_map)
					local possibleColorProps = #(#baseColor, #base_color, #diffuseColor, #diffuse_color, #albedo, #color)
					
					-- Try to find texture map
					for prop in possibleColorMapProps do
					(
						if (isProperty m prop) then
						(
							try
							(
								local propValue = getProperty m prop
								if propValue != undefined and (classof propValue == Bitmaptexture or superclassof propValue == textureMap) then
								(
									baseColorMap = propValue
									exit
								)
							)
							catch()
						)
					)
					
					-- Try to find base color
					for prop in possibleColorProps do
					(
						if (isProperty m prop) and baseColorMap == undefined then
						(
							try
							(
								local propValue = getProperty m prop
								if classof propValue == color then
								(
									baseColor = propValue
									exit
								)
							)
							catch()
						)
					)
					
					-- Apply texture or color
					if baseColorMap != undefined then
					(
						try
						(
							if classof baseColorMap == Bitmaptexture then
							(
								local texPath = baseColorMap.filename
								if texPath != undefined and texPath != "" then
									newMat.texture = Bitmaptexture fileName:texPath
								else
									newMat.texture = baseColorMap
							)
							else
								newMat.texture = baseColorMap
						)
						catch
						(
							newMat.texture = baseColorMap
						)
					)
					else
					(
						newMat.delegate.diffuse = baseColor
					)
					
					-- Handle transparency/transmission
					local possibleTransProps = #(#transmission, #transmissionWeight, #transparency, #opacity)
					for prop in possibleTransProps do
					(
						if (isProperty m prop) then
						(
							try
							(
								local propValue = getProperty m prop
								if classof propValue == float or classof propValue == integer then
								(
									-- Transmission/transparency is inverted to opacity
									if prop == #transmission or prop == #transmissionWeight or prop == #transparency then
										newMat.Alpha = (1.0 - propValue) * 100
									else
										newMat.Alpha = propValue * 100
									exit
								)
							)
							catch()
						)
					)
				)
				
				-- Physical Material conversion
				else if (classof m == Physical_Material) then
				(
					-- Try to get base color map
					if m.base_color_map != undefined then
					(
						try
						(
							if classof m.base_color_map == Bitmaptexture then
							(
								local texPath = m.base_color_map.filename
								if texPath != undefined and texPath != "" then
									newMat.texture = Bitmaptexture fileName:texPath
							)
							else
								newMat.texture = m.base_color_map
						)
						catch
						(
							newMat.texture = m.base_color_map
						)
					)
					else
					(
						-- Use base color if no texture
						newMat.delegate.diffuse = m.base_color
					)
					
					-- Handle transparency
					if m.transparency > 0 then
						newMat.Alpha = (1.0 - m.transparency) * 100
					else
						newMat.Alpha = 100.0
				)
				
				-- Standard Material conversion
				else if (classof m == StandardMaterial) then
				(
					-- Transfer diffuse map if it exists
					if m.diffuseMap != undefined then
					(
						try
						(
							if classof m.diffuseMap == Bitmaptexture then
							(
								local texPath = m.diffuseMap.filename
								if texPath != undefined and texPath != "" then
								(
									-- Create new bitmap texture with same path
									newMat.texture = Bitmaptexture fileName:texPath
									
									-- Transfer UV tiling settings from original
									if (isProperty m.diffuseMap #coords) and m.diffuseMap.coords != undefined then
									(
										if (isProperty newMat.texture #coords) and newMat.texture.coords != undefined then
										(
											try
											(
												newMat.texture.coords.U_Tiling = m.diffuseMap.coords.U_Tiling
												newMat.texture.coords.V_Tiling = m.diffuseMap.coords.V_Tiling
												newMat.texture.coords.U_Offset = m.diffuseMap.coords.U_Offset
												newMat.texture.coords.V_Offset = m.diffuseMap.coords.V_Offset
											)
											catch()
										)
									)
								)
							)
							else
							(
								-- If it's another type of texture map, try to assign directly
								newMat.texture = m.diffuseMap
							)
						)
						catch
						(
							-- Fallback: try direct assignment
							try (newMat.texture = m.diffuseMap) catch()
						)
					)
					
					-- Transfer diffuse color if no texture
					if newMat.texture == undefined and m.diffuse != undefined then
					(
						newMat.delegate.diffuse = m.diffuse
					)
					
					-- Transfer opacity settings
					if m.opacity != undefined and m.opacity < 100 then
					(
						newMat.Alpha = m.opacity
						
						-- Check if opacity map exists
						if m.opacityMap != undefined then
						(
							-- You might want to handle opacity maps here if needed
							-- For now, just use the opacity value
						)
					)
					else
					(
						newMat.Alpha = 100.0
					)
					
					-- Transfer self-illumination if applicable
					if m.selfIllumAmount > 0 then
					(
						newMat.unshaded = true
					)
					
					-- Check for two-sided material
					if (isProperty m #twoSided) and m.twoSided then
					(
						newMat.twosides = true
					)
				)
				
				-- Apply U/V Tile settings if requested
				if newMat.texture != undefined then
				(
					try
					(
						newMat.Wrap_Width = applyUTile
						newMat.Wrap_Height = applyVTile
						
						if (isProperty newMat.texture #coords) then
						(
							local coords = newMat.texture.coords
							if coords != undefined then
							(
								try
								(
									coords.U_Tile = applyUTile
									coords.V_Tile = applyVTile
								)
								catch
								(
									if (hasProperty coords #U_Tile) then coords.U_Tile = applyUTile
									if (hasProperty coords #V_Tile) then coords.V_Tile = applyVTile
								)
							)
						)
					)
					catch()
				)
				
				-- Ensure alpha value
				if newMat.Alpha == 0 then
					newMat.Alpha = 100.0
				
				-- Apply new material to objects
				for obj in geometry do
				(
					if obj.material == m then
						obj.material = newMat
				)
				
				-- Update Material Editor
				for i = 1 to meditMaterials.count do
				(
					if meditMaterials[i] == m then
					(
						meditMaterials[i] = newMat
						setMTLMeditObjType newMat 3
					)
				)
			)
		)
	),
	
	-- ========================================
	-- MAIN FUNCTIONS
	-- ========================================
	
	-- Refresh all problem lists (with caching)
	fn refreshAllProblems =
	(
		if cachedProblems != undefined and not hasSceneChanged() then
			return cachedProblems
		
		duplicates = findDuplicates()
		polyProblems = findMeshProblems()
		boneProblems = findBoneControllerProblems()
		materialProblems = findMaterialProblems()
		
		local totalProblems = duplicates.count + polyProblems.count + 
							boneProblems.count + materialProblems.count
		
		cachedProblems = totalProblems
		return totalProblems
	),
	
	-- Fix all problems at once
	fn fixAllProblems =
	(
		if duplicates.count > 0 then fixDuplicateNames()
		if polyProblems.count > 0 then fixMeshProblems()
		if boneProblems.count > 0 then fixBoneControllers()
		if materialProblems.count > 0 then fixMaterials()
		
		-- Force refresh after fixes
		invalidateCache()
		refreshAllProblems()
	),
	
	-- Get problem count
	fn getProblemCount =
	(
		return duplicates.count + polyProblems.count + 
			   boneProblems.count + materialProblems.count
	),
	
	-- Get problem summary string
	fn getProblemSummary =
	(
		local summary = ""
		if duplicates.count > 0 then
			summary += (duplicates.count as string) + " duplicate names, "
		if polyProblems.count > 0 then
			summary += (polyProblems.count as string) + " mesh problems, "
		if boneProblems.count > 0 then
			summary += (boneProblems.count as string) + " controller problems, "
		if materialProblems.count > 0 then
			summary += (materialProblems.count as string) + " material problems, "
		
		if summary != "" then
			summary = substring summary 1 (summary.count - 2) -- Remove last comma
		else
			summary = "No problems found"
		
		return summary
	),
	
	-- Initialize
	fn initialize =
	(
		lastSceneNodeCount = objects.count
		lastSceneChangeTime = timestamp()
		cachedProblems = undefined
		forceRefresh = true
		refreshAllProblems()
	)
)

/* ============================================================================
   EXPORT SETTINGS STRUCT
   ============================================================================ */
struct WhiteoutDexExportSettings
(
	usesMdx,
	modelName,
	filename,
	optionsMergeSimilar,
	optionsFixNormals,
	optionsFixNormalsTreshold,
	optionsFixSharedNormals,
	optionsExportSmoothgroups,
	optionsKeepUnusedBonesHelpers,
	optionsOpenFolder,
	extentsType,
	extentsPrecision,
	exportMode,
	exportVersion = 800,
	progressBarControl,
	progressLabelControl,
	-- Texture Conversion
	texConvertToBLP = true,
	texOverwrite = false,
	texCompression = "paletted",
	texJpegQuality = 75,
	texDither = false,
	texMipmaps = true
)

/* ============================================================================
   UTILITY FUNCTIONS
   ============================================================================ */

/*
	getIncrementedFilename
	Returns a filename with an auto-incremented suffix (_1, _2, etc.)
	to avoid overwriting existing files.
	
	Parameters:
		filename - The base filename (full path with extension)
	
	Returns:
		A new filename with incremented suffix
*/
fn getIncrementedFilename filename =
(
	local dir = getFilenamePath filename
	local fileBase = getFilenameFile filename
	local ext = getFilenameType filename
	
	-- Entferne existierende Nummer am Ende falls vorhanden
	local cleanBase = fileBase
	local pattern = "_[0-9]+$"
	if (matchPattern fileBase pattern:"*_[0-9]") or (matchPattern fileBase pattern:"*_[0-9][0-9]") or (matchPattern fileBase pattern:"*_[0-9][0-9][0-9]") then
	(
		-- Finde das letzte Underscore vor Zahlen
		local lastUnderscore = 0
		for i = fileBase.count to 1 by -1 do
		(
			if fileBase[i] == "_" then
			(
				local remainingStr = substring fileBase (i+1) -1
				local isAllDigits = true
				for j = 1 to remainingStr.count do
				(
					local charCode = bit.charAsInt remainingStr[j]
					if charCode < 48 or charCode > 57 then
					(
						isAllDigits = false
						exit
					)
				)
				if isAllDigits and remainingStr.count > 0 then
				(
					cleanBase = substring fileBase 1 (i-1)
					exit
				)
			)
		)
	)
	
	-- Finde die nächste verfügbare Nummer
	local counter = 1
	local newFilename = dir + cleanBase + "_" + (counter as string) + ext
	
	while doesFileExist newFilename do
	(
		counter += 1
		newFilename = dir + cleanBase + "_" + (counter as string) + ext
	)
	
	return newFilename
)

/* ============================================================================
   BLP TEXTURE CONVERSION (post-export)
   ============================================================================ */

/*
	convertTexturesToBLP
	After export, converts all source textures (TGA/PNG/DDS/BMP/JPG) to BLP
	and saves them alongside the exported model file.
	
	Parameters:
		exportFilePath - Full path of the exported MDX/MDL file
	
	Returns:
		Number of textures successfully converted
*/
fn convertTexturesToBLP exportFilePath settings =
(
	local exePath = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\WhiteoutTexCLI.exe"
	if not (doesFileExist exePath) do
	(
		format "WhiteoutDex: WhiteoutTexCLI.exe not found, skipping BLP conversion\n"
		return 0
	)
	
	local exportDir = getFilenamePath exportFilePath
	local converted = 0
	local skipped = 0
	local alreadyDone = #()
	
	for obj in objects do
	(
		local mat = obj.material
		if mat == undefined then continue
		
		local wc3Mats = #()
		if classof mat == Warcraft3 then
			append wc3Mats mat
		else if classof mat == CompositeMaterial then
		(
			if mat.baseMaterial != undefined and classof mat.baseMaterial == Warcraft3 then
				append wc3Mats mat.baseMaterial
			for j = 1 to mat.materialList.count do
				if mat.materialList[j] != undefined and classof mat.materialList[j] == Warcraft3 then
					append wc3Mats mat.materialList[j]
		)
		
		for wc3 in wc3Mats do
		(
			-- Keine Textur vorhanden, ueberspringen
			if wc3.texture == undefined then continue
			if classof wc3.texture != Bitmaptexture then continue
			
			local srcFile = wc3.texture.filename
			if srcFile == undefined or srcFile == "" then continue
			
			-- Quell-Textur existiert nicht auf der Festplatte (nur Pfad-Referenz)
			-- Fallback: try scene-relative path (common when opening .max from another machine)
			if not (doesFileExist srcFile) then
			(
				local sceneDir = getFilenamePath maxFilePath
				if sceneDir != "" and sceneDir != undefined then
				(
					local scenePath = sceneDir + (filenameFromPath srcFile)
					if doesFileExist scenePath then
						srcFile = scenePath
					else
						continue
				)
				else
					continue
			)
			
			-- Bereits in diesem Export verarbeitet
			local srcLower = toLower srcFile
			if findItem alreadyDone srcLower > 0 then continue
			append alreadyDone srcLower
			
			local srcExt = toLower (getFilenameType srcFile)
			local baseName = getFilenameFile srcFile
			
			-- Prefix-Path als Unterordner (z.B. "war3mapImported\")
			local prefix = if wc3.path != undefined and wc3.path != "" then wc3.path else ""
			local outDir = exportDir + prefix
			makeDir outDir all:true
			local blpPath = outDir + baseName + ".blp"
			
			-- BLP existiert bereits am Ziel
			if (doesFileExist blpPath) and (not settings.texOverwrite) then
			(
				skipped += 1
				continue
			)
			
			if srcExt == ".blp" then
			(
				-- Schon BLP, nur kopieren
				if (toLower srcFile) != (toLower blpPath) then
				(
					try ( copyFile srcFile blpPath ) catch()
				)
				converted += 1
			)
			else
			(
				-- Konvertieren: TGA/PNG/DDS/BMP/JPG -> BLP
				local opts = "--blp_type=1"
				opts += " --blp_compression=" + settings.texCompression
				if settings.texCompression == "jpeg" then
					opts += " --jpeg_quality=" + (settings.texJpegQuality as string)
				if settings.texDither and settings.texCompression == "paletted" then
					opts += " --blp_dither"
				if settings.texMipmaps then
					opts += " --generate_mipmaps=maximum"
				
				-- Unicode-safe: copy source to temp dir with ASCII-only names
				-- (WhiteoutTexCLI.exe does not support Unicode paths)
				local tempDir = (getDir #temp) + "\\"
				local uniqueID = (timestamp() as string) + (random 1000 9999 as string)
				local tempSrc = tempDir + "ndx_src_" + uniqueID + srcExt
				local tempBlp = tempDir + "ndx_out_" + uniqueID + ".blp"
				
				try ( copyFile srcFile tempSrc ) catch
				(
					format "WhiteoutDex BLP convert failed (copy to temp): %\n" srcFile
					continue
				)
				
				local cmd = "cmd /c \"\"" + exePath + "\" \"" + tempSrc + "\" \"" + tempBlp + "\" " + opts + "\""
				local exitCode = 0
				HiddenDOSCommand cmd exitCode:&exitCode
				
				-- Clean up temp source
				try ( deleteFile tempSrc ) catch ()
				
				if exitCode == 0 and doesFileExist tempBlp then
				(
					-- Move result to actual destination
					try ( copyFile tempBlp blpPath ) catch
					(
						format "WhiteoutDex BLP convert failed (copy result): % -> %\n" tempBlp blpPath
					)
					try ( deleteFile tempBlp ) catch ()
					if doesFileExist blpPath then
						converted += 1
					else
						format "WhiteoutDex BLP convert failed: % -> %\n" srcFile blpPath
				)
				else
				(
					try ( deleteFile tempBlp ) catch ()
					format "WhiteoutDex BLP convert failed: % -> %\n" srcFile blpPath
				)
			)
		)
	)
	
	if converted > 0 then
		format "WhiteoutDex: % textures converted to BLP in %\n" converted exportDir
	if skipped > 0 then
		format "WhiteoutDex: % textures skipped (BLP already exists)\n" skipped
	converted
)

/* ============================================================================
   EXPORT SCENE FUNCTION
   ============================================================================ */

/*
	exportScene
	Main export function that parses the scene and writes MDX/MDL files.
	
	Parameters:
		settings - WhiteoutDexExportSettings instance with all export configuration
*/
	fn exportScene settings =
	(
		local time1 = timestamp ()
		local model
		::Progress.initiate settings.progressLabelControl settings.progressBarControl
		local sel = #()
		for s in selection do append sel s
		local len = animationRange
		local sliderTmp = sliderTime
		animationRange = interval 0f 100f
		sliderTime = 0f
		case settings.exportMode of
		(
			1:
			(
				local noError = true
				::ErrorReporter.InitiateErrorReporter ()
				DisableSceneRedraw ()
				::Progress.downLevel 70 1
				try
				(
					model = ::SceneParser.ParseScene \
							mergeGeosets:settings.optionsMergeSimilar \
							extentsCalculation:settings.extentsType \
							extentsPrecision:settings.extentsPrecision \
							ATFixNormals:settings.optionsFixNormals \
							ATThresshold:settings.optionsFixNormalsTreshold \
							faceGroups:settings.optionsExportSmoothgroups \
							keepUnusedBonesHelpers:settings.optionsKeepUnusedBonesHelpers \
							fixSharedNormals:settings.optionsFixSharedNormals \
							exportVersion:settings.exportVersion
				)
				catch
				(
					messageBox "Export Failed: Error while parsing the scene\n Please open the listener and report the bug"
					format "*** % ***\n" (getCurrentException()) to:listener
					if resumeEditing != undefined then resumeEditing ()
					noError = false
				)
				::Progress.upLevel ()
				if noError then
				(
					::Progress.downLevel 30 1
					model.name = settings.modelName
					try
					(
						::Progress.updateItem "Optimizing Model..."
						::Optimizer.optimizeExportModel model
						
						if settings.usesMdx then ::MDXWriter.OutputMDX model settings.filename
						else ::MDLWriter.OutputMDL model settings.filename
						
						-- Convert textures to BLP alongside the exported model (Classic v800 only)
						if settings.exportVersion == 800 and settings.texConvertToBLP then
						(
							settings.progressLabelControl.caption = "Converting Textures..."
							try ( convertTexturesToBLP settings.filename settings ) catch
							(
								format "WhiteoutDex: BLP conversion warning: %\n" (getCurrentException())
							)
						)
						
					)
					catch
					(
						messageBox "Export Failed: Error while writting the mdl file\n Please open the listener and report the bug"
						format "*** % ***\n" (getCurrentException()) to:listener
						noError = false
					)
					::Progress.upLevel ()
					::Progress.finalize ()
				)
				EnableSceneRedraw ()
				if noError then
				(
					local message = "Total Time: " + (((timestamp () -time1)/1000.0) as string) + "s."
					messageBox message title:"Export Successful"
				

					if (::ErrorReporter.errorReporterHasErrors()) then
						if (queryBox "Some warnnings were found during the exporting process.\n Would you like to see them?") then
						(
							try
							(
								::ErrorReporter.promptErrorReporter()
								messageBox \
									"The warnnings have been printed in the listener.\n You can find it in utilities->MaxScript or by typing F11."
							)
							catch
							(
								messageBox "Warnning Propmt Failed: Error while propmting warnnings"
								format "*** % ***\n" (getCurrentException()) to:listener
							)
						)
				)
				else
					messageBox "Export Failed, try the debug mode to generate the log."

				-- Open Export folder if enabled
				if settings.optionsOpenFolder then
				(
					local exportDir = getFilenamePath settings.filename
					try shellLaunch "explorer.exe" exportDir catch()
				)
			)
			default:
			(
				try
				(
					::ErrorReporter.InitiateErrorReporter ()
					::Progress.downLevel 70 1
					model = ::SceneParser.ParseScene \
							mergeGeosets:settings.optionsMergeSimilar \
							extentsCalculation:settings.extentsType \
							extentsPrecision:settings.extentsPrecision \
							ATFixNormals:settings.optionsFixNormals \
							ATThresshold:settings.optionsFixNormalsTreshold \
							faceGroups:settings.optionsExportSmoothgroups \
							fixSharedNormals:settings.optionsFixSharedNormals \
							exportVersion:settings.exportVersion

					::Progress.upLevel ()
					model.name = settings.modelName
					gc()
					::Progress.downLevel 30 1
					
					if settings.usesMdx then ::MDXWriter.OutputMDX model settings.filename
					else ::MDLWriter.OutputMDL model settings.filename
					
					-- Convert textures to BLP alongside the exported model (Classic v800 only)
					if settings.exportVersion == 800 and settings.texConvertToBLP then
					(
						settings.progressLabelControl.caption = "Converting Textures..."
						try ( convertTexturesToBLP settings.filename settings ) catch
						(
							format "WhiteoutDex: BLP conversion warning: %\n" (getCurrentException())
						)
					)
					
					::Progress.upLevel ()
					::ErrorReporter.promptErrorReporter()
				)
				catch
				(
					format "*** Debug Export Error: % ***\n" (getCurrentException()) to:listener
					messageBox "Debug Export Failed. Check the Listener for details."
				)
			)
		)
		animationRange = len
		sliderTime = sliderTmp
		deselect selection
		select sel
		settings.progressLabelControl.caption = "Idle"
		settings.progressBarControl.value = 0.0
	)

/* ============================================================================
   GLOBAL INSTANCE CREATION
   ============================================================================ */
::WhiteoutDexSceneMonitorStruct = WhiteoutDexSceneMonitorStruct
::whiteoutDexSceneMonitorInstance = WhiteoutDexSceneMonitorStruct()
