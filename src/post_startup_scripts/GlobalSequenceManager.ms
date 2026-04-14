global GlobalSequenceManager

struct WdxGlobalSequenceManager
(
	globalEntries = #(),
	isRecording = false,
	isEditMode = false,
	recordDuration = 0f,
	recordCtrlType = 1,
	controllerSnapshot = #(),
	savedAnimRange = undefined,
	recordedObjects = #(),
	savedTransforms = #(),
	savedEditKeys = #(),
	
	-- Speichert alle Key-Daten eines Controllers (Zeit + Wert, inkl. Sub-Controller + ORT)
	fn backupControllerKeys ctrl =
	(
		local data = #()
		format "DEBUG backup ctrl=% class=%\n" ctrl (classOf ctrl)
		-- Sub-Controller (Euler_XYZ, Position_XYZ etc.)
		for i = 1 to 3 do
		(
			try
			(
				local sc = ctrl[i].controller
				if sc != undefined and (isProperty sc #keys) and sc.keys != undefined then
				(
					local subKeys = #()
					for k = 1 to sc.keys.count do
						append subKeys #(sc.keys[k].time, sc.keys[k].value)
					append data #("sub", i, subKeys, try(getAfterORT sc)catch(#constant))
					format "DEBUG   sub[%]: % keys, ort=%\n" i subKeys.count (try(getAfterORT sc)catch(#constant))
				)
			)
			catch()
		)
		-- Haupt-Controller Keys (compound Controller wie Euler_XYZ haben keine eigenen)
		try
		(
			if (isProperty ctrl #keys) and ctrl.keys != undefined and ctrl.keys.count > 0 then
			(
				local mainKeys = #()
				for k = 1 to ctrl.keys.count do
					append mainKeys #(ctrl.keys[k].time, ctrl.keys[k].value)
				append data #("main", 0, mainKeys, try(getAfterORT ctrl)catch(#constant))
				format "DEBUG   main: % keys, ort=%\n" mainKeys.count (try(getAfterORT ctrl)catch(#constant))
			)
		)
		catch()
		format "DEBUG   backup total entries=%\n" data.count
		data
	),
	
	-- Stellt Key-Daten auf einem Controller wieder her
	fn restoreControllerKeys ctrl data =
	(
		format "DEBUG restore ctrl=% data entries=%\n" (classOf ctrl) data.count
		-- Erst alle Keys löschen
		this.forceDeleteAllKeys ctrl
		format "DEBUG   after forceDelete: keys=%\n" (this.countControllerKeys ctrl)
		-- Keys wiederherstellen
		for entry in data do
		(
			local entryType = entry[1]
			local idx = entry[2]
			local keyData = entry[3]
			local ort = entry[4]
			format "DEBUG   restoring type=% idx=% keyCount=% ort=%\n" entryType idx keyData.count ort
			
			if entryType == "sub" then
			(
				try
				(
					local sc = ctrl[idx].controller
					format "DEBUG     sub ctrl class=%\n" (classOf sc)
					for kd in keyData do
						with animate on at time kd[1] (sc.value = kd[2])
					try (setAfterORT sc ort) catch()
					format "DEBUG     sub keys after restore=%\n" (try(sc.keys.count)catch("?"))
				)
				catch(format "DEBUG     sub RESTORE ERROR: %\n" (getCurrentException()))
			)
			else
			(
				for kd in keyData do
					with animate on at time kd[1] (ctrl.value = kd[2])
				try (setAfterORT ctrl ort) catch()
				format "DEBUG     main keys after restore=%\n" (this.countControllerKeys ctrl)
			)
		)
		format "DEBUG   FINAL keys=%\n" (this.countControllerKeys ctrl)
	),
	
	fn classifyUVController ctrl =
	(
		if classOf ctrl == Linear_float then
			return "Flow"
		
		if classOf ctrl == Bezier_float then
		(
			if ctrl.keys.count == 0 then return "Flow"
			local allStep = true
			for k = 1 to ctrl.keys.count where allStep do
			(
				try
				(
					if ctrl.keys[k].inTangentType != #step or ctrl.keys[k].outTangentType != #step then
						allStep = false
				)
				catch (allStep = false)
			)
			if allStep then return "SEQ" else return "Flow"
		)
		"Flow"
	),
	
	fn isTypeEditable type =
	(
		type == "Bone" or type == "Flow"
	),
	
	fn collectController ctrl description ctrlType &entriesMap =
	(
		try
		(
			if ctrl == undefined then return()
			if not (isProperty ctrl #keys) then return()
			if ctrl.keys == undefined or ctrl.keys.count == 0 then return()
			if getAfterORT ctrl != #cycle then return()
			
			local lastKeyTime = ctrl.keys[ctrl.keys.count].time
			if lastKeyTime <= 0f then return()
			
			local editable = this.isTypeEditable ctrlType
			local sourceStr = description + "  [" + ctrlType + "]"
			
			local found = false
			for entry in entriesMap where entry[1] == lastKeyTime do
			(
				append entry[2] #(ctrl, description, ctrlType, editable)
				appendIfUnique entry[4] sourceStr
				if editable then entry[5] = true
				found = true
				exit
			)
			if not found then
			(
				append entriesMap #(lastKeyTime, #(#(ctrl, description, ctrlType, editable)), false, #(sourceStr), editable, ctrlType)
			)
		)
		catch()
	),
	
	fn _collectIFLName name arr =
	(
		append arr name
	),
	
	fn collectIFL tex matName &entriesMap =
	(
		try
		(
			if tex == undefined or classOf tex != BitmapTexture then return()
			
			local texPath = tex.fileName
			if texPath == undefined or texPath.count < 5 then return()
			if stricmp (getFilenameType texPath) ".ifl" != 0 then return()
			
			local files = #()
			enumerateFiles tex this._collectIFLName files
			local iflIdx = findItem files texPath
			if iflIdx != 0 then deleteItem files iflIdx
			
			local fileCount = files.count
			if fileCount < 2 then return()
			
			local playback = tex.playbackrate
			if playback == undefined or playback == 0 then playback = 1.0
			local n = (::WdxUtils.round (1.0 / playback)) as integer
			if n == 0 then n = 1
			local duration = (n * (fileCount - 1)) as time
			if duration <= 0f then return()
			
			local sourceStr = matName + "  [IFL: " + (getFilenameFile texPath) + "]"
			
			local found = false
			for entry in entriesMap where entry[1] == duration do
			(
				entry[3] = true
				appendIfUnique entry[4] sourceStr
				found = true
				exit
			)
			if not found then
			(
				append entriesMap #(duration, #(), true, #(sourceStr), false, "IFL")
			)
		)
		catch()
	),
	
	fn scanScene =
	(
		local entriesMap = #()
		local scannedMaterials = #()
		
		for obj in objects do
		(
			local objName = obj.name
			
			try (this.collectController obj.pos.controller       (objName + ".Position")   "Bone" &entriesMap) catch()
			try (this.collectController obj.rotation.controller   (objName + ".Rotation")   "Bone" &entriesMap) catch()
			try (this.collectController obj.scale.controller      (objName + ".Scale")      "Bone" &entriesMap) catch()
			try (this.collectController obj.visibility.controller  (objName + ".Visibility") "Vis"  &entriesMap) catch()
			
			local mat = undefined
			try (mat = obj.material) catch()
			
			if mat != undefined and (findItem scannedMaterials mat) == 0 then
			(
				append scannedMaterials mat
				local matName = if mat.name != undefined then mat.name else "Material"
				
				try (this.collectController mat.alpha.controller (matName + ".Alpha") "Alpha" &entriesMap) catch()
				
				try
				(
					if mat.texture != undefined and classOf mat.texture == BitmapTexture then
					(
						local coords = mat.texture.coords
						local cU = coords.U_offset.controller
						local cV = coords.V_offset.controller
						local cW = coords.W_angle.controller
						local cSU = coords.U_tiling.controller
						local cSV = coords.V_tiling.controller
						
						local uvType = "Flow"
						for c in #(cU, cV, cW, cSU, cSV) where c != undefined do
						(
							uvType = this.classifyUVController c
							exit
						)
						
						this.collectController cU  (matName + ".UV.U_Offset")  uvType &entriesMap
						this.collectController cV  (matName + ".UV.V_Offset")  uvType &entriesMap
						this.collectController cW  (matName + ".UV.Rotation")  uvType &entriesMap
						this.collectController cSU (matName + ".UV.U_Scale")   uvType &entriesMap
						this.collectController cSV (matName + ".UV.V_Scale")   uvType &entriesMap
					)
				)
				catch()
				
				try (this.collectIFL mat.texture matName &entriesMap) catch()
			)
		)
		
		for entry in entriesMap do
		(
			if entry[3] and entry[2].count == 0 then
			(
				entry[6] = "IFL"
			)
			else
			(
				local types = #()
				for cp in entry[2] do appendIfUnique types cp[3]
				if types.count == 1 then entry[6] = types[1]
				else if types.count == 0 then entry[6] = "IFL"
				else entry[6] = "Mixed"
			)
		)
		
		fn sortByDuration a b =
		(
			local da = a[1].frame as integer
			local db = b[1].frame as integer
			case of
			(
				(da < db): -1
				(da > db): 1
				default: 0
			)
		)
		qsort entriesMap sortByDuration
		
		this.globalEntries = entriesMap
	),
	
	-- Skaliert Keys auf einem Controller (inkl. Sub-Controller) proportional
	fn scaleControllerKeys ctrl oldLastFrame newLastFrame =
	(
		if oldLastFrame <= 0 or newLastFrame <= 0 then return()
		
		-- Helper: Keys auf einem einzelnen Key-Controller skalieren
		fn scaleKeys kCtrl oldLast newLast =
		(
			if kCtrl == undefined then return()
			if not (isProperty kCtrl #keys) then return()
			if kCtrl.keys == undefined or kCtrl.keys.count < 2 then return()
			
			local kc = kCtrl.keys.count
			format "DEBUG     scaleKeys class=% keys=% old=% new=%\n" (classOf kCtrl) kc oldLast newLast
			-- Von hinten nach vorne (verhindert Sortierprobleme)
			for k = kc to 1 by -1 do
			(
				local oldFrame = kCtrl.keys[k].time.frame as integer
				if k == kc then
				(
					-- Letzter Key: EXAKT auf Ziel setzen
					kCtrl.keys[k].time = newLast as time
					format "DEBUG       key[%] %f -> %f (LAST)\n" k oldFrame newLast
				)
				else if oldFrame > 0 then
				(
					-- Mittlere Keys: Proportional skalieren mit korrektem Runden
					local newFrame = ((oldFrame as float / oldLast as float) * newLast as float + 0.5) as integer
					if newFrame < 1 then newFrame = 1
					if newFrame >= newLast then newFrame = newLast - 1
					format "DEBUG       key[%] %f -> %f\n" k oldFrame newFrame
					kCtrl.keys[k].time = newFrame as time
				)
				else
					format "DEBUG       key[%] %f -> 0f (KEEP)\n" k oldFrame
			)
		)
		
		-- Sub-Controller (Euler_XYZ, Position_XYZ etc.)
		for i = 1 to 3 do
		(
			try
			(
				local sc = ctrl[i].controller
				if sc != undefined then scaleKeys sc oldLastFrame newLastFrame
			)
			catch()
		)
		-- Haupt-Controller (Linear_Position, Bezier_Scale etc.)
		try (scaleKeys ctrl oldLastFrame newLastFrame) catch()
	),
	
	fn changeDuration entryIndex newDuration =
	(
		if entryIndex < 1 or entryIndex > this.globalEntries.count then return false
		
		local entry = this.globalEntries[entryIndex]
		if not entry[5] then return false
		if newDuration <= 0f then return false
		
		local newFrame = newDuration.frame as integer
		
		-- Echte alte Duration = tatsächlicher letzter Key-Frame (nicht entry[1] das driften kann)
		local oldFrame = 0
		for ctrlPair in entry[2] where ctrlPair[4] do
		(
			local ctrl = ctrlPair[1]
			-- Haupt-Controller
			try (if ctrl.keys != undefined and ctrl.keys.count > 0 then
				(local lf = ctrl.keys[ctrl.keys.count].time.frame as integer; if lf > oldFrame then oldFrame = lf)
			) catch()
			-- Sub-Controller
			for i = 1 to 3 do
			(
				try
				(
					local sc = ctrl[i].controller
					if sc != undefined and (isProperty sc #keys) and sc.keys != undefined and sc.keys.count > 0 then
						(local lf = sc.keys[sc.keys.count].time.frame as integer; if lf > oldFrame then oldFrame = lf)
				)
				catch()
			)
		)
		
		if oldFrame <= 0 then return false
		if newFrame == oldFrame then return true
		
		format "\n===== DEBUG changeDuration =====\n"
		format "DEBUG entryIndex=% oldFrame=% newFrame=%\n" entryIndex oldFrame newFrame
		
		for ctrlPair in entry[2] where ctrlPair[4] do
		(
			local ctrl = ctrlPair[1]
			format "DEBUG   scaling '%' class=%\n" ctrlPair[2] (classOf ctrl)
			this.scaleControllerKeys ctrl oldFrame newFrame
		)
		
		-- Duration als exakten Frame speichern (kein Float-Drift)
		entry[1] = newFrame as time
		format "DEBUG   stored duration=%\n" entry[1]
		format "===== DEBUG changeDuration END =====\n\n"
		true
	),
	
	fn removeGlobalSequence entryIndex =
	(
		if entryIndex < 1 or entryIndex > this.globalEntries.count then return false
		
		local entry = this.globalEntries[entryIndex]
		
		-- Slider auf Frame 0 setzen damit Initial-Pose erhalten bleibt
		sliderTime = 0
		
		for ctrlPair in entry[2] do
		(
			local ctrl = ctrlPair[1]
			local desc = ctrlPair[2]
			try
			(
				-- Für Bone-Einträge: Controller frisch vom Objekt holen
				-- (Spin kann den Controller-Typ geändert haben → Referenz veraltet)
				local dotIdx = findString desc "."
				if dotIdx != undefined then
				(
					local oName = substring desc 1 (dotIdx - 1)
					local channel = substring desc (dotIdx + 1) -1
					local obj = getNodeByName oName
					if obj != undefined then
					(
						local freshCtrl = case channel of
						(
							"Position": obj.pos.controller
							"Rotation": obj.rotation.controller
							"Scale":    obj.scale.controller
							default:    undefined
						)
						if freshCtrl != undefined then ctrl = freshCtrl
					)
				)
				
				format "DEBUG remove: desc=% ctrl=% keys=%\n" desc (classOf ctrl) (this.countControllerKeys ctrl)
				this.deleteKeysAfterZero ctrl
				format "DEBUG remove: after delete keys=%\n" (this.countControllerKeys ctrl)
			)
			catch(format "DEBUG remove ERROR: %\n" (getCurrentException()))
		)
		true
	),
	
	-- Zählt Keys inkl. Sub-Controller (für Euler_XYZ, Position_XYZ)
	fn countControllerKeys ctrl =
	(
		try
		(
			if ctrl.keys != undefined and ctrl.keys.count > 0 then return ctrl.keys.count
			local total = 0
			for i = 1 to 3 do
			(
				try (total += ctrl[i].controller.keys.count) catch()
			)
			total
		)
		catch (0)
	),
	
	-- Setzt ORT auf Controller inkl. Sub-Controller
	fn setControllerORT ctrl ortType =
	(
		try (setAfterORT ctrl ortType) catch()
		for i = 1 to 3 do
		(
			try (setAfterORT ctrl[i].controller ortType) catch()
		)
	),
	
	fn takeSnapshot doPos doRot doScale =
	(
		local snapshot = #()
		local objs = selection as array
		if objs.count == 0 then objs = objects as array
		
		for obj in objs do
		(
			if doPos then
			(
				try
				(
					local pc = obj.pos.controller
					if pc != undefined then
					(
						local kc = this.countControllerKeys pc
						append snapshot #(obj, "pos", kc, obj.name + ".Position")
					)
				)
				catch()
			)
			if doRot then
			(
				try
				(
					local rc = obj.rotation.controller
					if rc != undefined then
					(
						local kc = this.countControllerKeys rc
						append snapshot #(obj, "rot", kc, obj.name + ".Rotation")
					)
				)
				catch()
			)
			if doScale then
			(
				try
				(
					local sc = obj.scale.controller
					if sc != undefined then
					(
						local kc = this.countControllerKeys sc
						append snapshot #(obj, "scl", kc, obj.name + ".Scale")
					)
				)
				catch()
			)
		)
		snapshot
	),
	
	fn assignControllers ctrlType doPos doRot doScale =
	(
		if ctrlType == 1 then
		(
			return()
		)
		
		for obj in selection do
		(
			try
			(
				local savedTM = at time 0 obj.transform
				
				if doPos then
				(
					case ctrlType of
					(
						2: obj.pos.controller = Position_XYZ()
						3: obj.pos.controller = Linear_Position()
						4: obj.pos.controller = Position_XYZ()
						5: obj.pos.controller = TCB_Position()
					)
					try (deleteKeys obj.pos.controller #allKeys) catch()
				)
				if doRot then
				(
					case ctrlType of
					(
						2: obj.rotation.controller = Euler_XYZ()
						3: obj.rotation.controller = Linear_Rotation()
						4: obj.rotation.controller = Euler_XYZ()
						5: obj.rotation.controller = TCB_Rotation()
					)
					try (deleteKeys obj.rotation.controller #allKeys) catch()
				)
				if doScale then
				(
					case ctrlType of
					(
						2: obj.scale.controller = Bezier_Scale()
						3: obj.scale.controller = Linear_Scale()
						4: obj.scale.controller = Bezier_Scale()
						5: obj.scale.controller = TCB_Scale()
					)
					try (deleteKeys obj.scale.controller #allKeys) catch()
				)
				
				obj.transform = savedTM
			)
			catch
			(
			)
		)
	),
	
	fn applyStepTangents =
	(
		for snap in this.controllerSnapshot do
		(
			local obj = snap[1]
			local channel = snap[2]
			local oldCount = snap[3]
			try
			(
				local ctrl = case channel of
				(
					"pos": obj.pos.controller
					"rot": obj.rotation.controller
					"scl": obj.scale.controller
				)
				if ctrl.keys != undefined and ctrl.keys.count > oldCount then
				(
					for k = 1 to ctrl.keys.count do
					(
						try
						(
							ctrl.keys[k].inTangentType = #step
							ctrl.keys[k].outTangentType = #step
						)
						catch()
					)
				)
			)
			catch()
		)
	),
	
	fn beginRecording duration ctrlType doPos doRot doScale editMode:false =
	(
		this.savedAnimRange = animationRange
		this.recordDuration = duration as time
		this.recordCtrlType = ctrlType
		this.recordedObjects = selection as array
		this.isEditMode = editMode
		
		-- Key-Daten IMMER sichern (für Cancel-Restore, sowohl New als auch Edit)
		this.savedEditKeys = #()
		for obj in this.recordedObjects do
		(
			try (append this.savedEditKeys #(obj, "pos", this.backupControllerKeys obj.pos.controller)) catch()
			try (append this.savedEditKeys #(obj, "rot", this.backupControllerKeys obj.rotation.controller)) catch()
			try (append this.savedEditKeys #(obj, "scl", this.backupControllerKeys obj.scale.controller)) catch()
		)
		
		-- Original-Transforms und Controller-Klassen speichern BEVOR Controller geändert werden
		this.savedTransforms = #()
		for obj in this.recordedObjects do
		(
			local tm = at time 0 obj.transform
			local posClass = classOf obj.pos.controller
			local rotClass = classOf obj.rotation.controller
			local sclClass = classOf obj.scale.controller
			append this.savedTransforms #(obj, tm, posClass, rotClass, sclClass)
		)
		
		if not editMode then
		(
			this.assignControllers ctrlType doPos doRot doScale
			
			-- IMMER alle Keys löschen (unabhängig von Checkboxen/ctrlType)
			for obj in selection do
			(
				try
				(
					local pc = obj.pos.controller
					deleteKeys pc #allKeys
					for i = 1 to 3 do (try (deleteKeys pc[i].controller #allKeys) catch())
				)
				catch()
				try
				(
					local rc = obj.rotation.controller
					deleteKeys rc #allKeys
					for i = 1 to 3 do (try (deleteKeys rc[i].controller #allKeys) catch())
				)
				catch()
				try
				(
					local sc = obj.scale.controller
					deleteKeys sc #allKeys
					for i = 1 to 3 do (try (deleteKeys sc[i].controller #allKeys) catch())
				)
				catch()
			)
			
			with animate on at time 0
			(
				for obj in selection do
				(
					-- Position: immer Frame 0 Key
					try
					(
						local pc = obj.pos.controller
						if (classOf pc == Position_XYZ) then
						(
							for i = 1 to 3 do (try (addNewKey pc[i].controller 0f) catch())
						)
						else
						(
							try (addNewKey pc 0f) catch()
						)
					)
					catch()
					
					-- Rotation: immer Frame 0 Key
					try
					(
						local rc = obj.rotation.controller
						if (classOf rc == Euler_XYZ) then
						(
							for i = 1 to 3 do (try (addNewKey rc[i].controller 0f) catch())
						)
						else
						(
							try (addNewKey rc 0f) catch()
						)
					)
					catch()
					
					-- Scale: immer Frame 0 Key
					try
					(
						local sc = obj.scale.controller
						try (addNewKey sc 0f) catch()
					)
					catch()
				)
			)
		)
		
		this.controllerSnapshot = this.takeSnapshot true true true
		
		format "\n===== DEBUG beginRecording =====\n"
		format "DEBUG editMode=% snapshot entries=%\n" editMode this.controllerSnapshot.count
		for s in this.controllerSnapshot do
		(
			local ctrl = case s[2] of ("pos": s[1].pos.controller; "rot": s[1].rotation.controller; "scl": s[1].scale.controller)
			format "DEBUG   %: oldKeys=% ctrl=%\n" s[4] s[3] (classOf ctrl)
		)
		
		this.isRecording = true
		animationRange = interval 0 this.recordDuration
		sliderTime = 0
	),
	
	fn finishRecording =
	(
		format "\n===== DEBUG finishRecording START =====\n"
		format "DEBUG isEditMode=%\n" this.isEditMode
		if not this.isRecording then
		(
			return #(#(), #())
		)
		
		local newGlobals = #()
		local warnings = #()
		
		-- Snapshot: #(obj, channel, oldKeyCount, name)
		for snap in this.controllerSnapshot do
		(
			local obj = snap[1]
			local channel = snap[2]
			local oldCount = snap[3]
			local ctrlName = snap[4]
			
			try
			(
				-- AKTUELLEN Controller vom Objekt holen (nicht die alte Referenz!)
				local ctrl = case channel of
				(
					"pos": obj.pos.controller
					"rot": obj.rotation.controller
					"scl": obj.scale.controller
				)
				
				local newCount = this.countControllerKeys ctrl
				-- Im Edit-Modus: oldCount auf 0 setzen (alle Keys sind "neu")
				local effectiveOld = if this.isEditMode then 0 else oldCount
				format "DEBUG   %: ctrl=% oldKeys=% newKeys=% effectiveOld=%\n" ctrlName (classOf ctrl) oldCount newCount effectiveOld
				if newCount > effectiveOld then
				(
					if effectiveOld > 1 then
						append warnings ctrlName
					this.setControllerORT ctrl #cycle
					append newGlobals ctrlName
					format "DEBUG   -> SET ORT #cycle\n"
				)
				else
				(
					format "DEBUG   -> SKIPPED\n"
				)
			)
			catch
			(
				format "DEBUG   ERROR: %\n" (getCurrentException())
			)
		)
		
		animationRange = this.savedAnimRange
		this.isRecording = false
		this.isEditMode = false
		this.recordedObjects = #()
		this.savedTransforms = #()
		this.savedEditKeys = #()
		
		if this.recordCtrlType == 2 then this.applyStepTangents()
		
		format "DEBUG newGlobals=% warnings=%\n" newGlobals.count warnings.count
		format "===== DEBUG finishRecording END =====\n\n"
		#(newGlobals, warnings)
	),
	
	-- Löscht alle Keys nach Frame 0 auf einem Controller (inkl. Sub-Controller)
	fn deleteKeysAfterZero ctrl =
	(
		try
		(
			-- Sub-Controller (Euler_XYZ, Position_XYZ etc.)
			for i = 1 to 3 do
			(
				try
				(
					local sc = ctrl[i].controller
					if sc != undefined and (isProperty sc #keys) and sc.keys != undefined then
					(
						for k = sc.keys.count to 1 by -1 do
						(
							if sc.keys[k].time > 0f then deleteKey sc k
						)
					)
				)
				catch()
			)
			-- Haupt-Controller Keys
			if (isProperty ctrl #keys) and ctrl.keys != undefined then
			(
				for k = ctrl.keys.count to 1 by -1 do
				(
					if ctrl.keys[k].time > 0f then deleteKey ctrl k
				)
			)
			-- ORT zurücksetzen
			try (setAfterORT ctrl #constant) catch()
			for i = 1 to 3 do (try (setAfterORT ctrl[i].controller #constant) catch())
		)
		catch()
	),
	
	-- Löscht ALLE Keys (inkl. Frame 0) auf einem Controller (inkl. Sub-Controller)
	fn deleteAllKeys ctrl =
	(
		try
		(
			-- Sub-Controller einzeln rückwärts löschen (zuverlässiger als deleteKeys #allKeys)
			for i = 1 to 3 do
			(
				try
				(
					local sc = ctrl[i].controller
					if sc != undefined and (isProperty sc #keys) and sc.keys != undefined then
					(
						for k = sc.keys.count to 1 by -1 do (deleteKey sc k)
					)
				)
				catch()
			)
			-- Haupt-Controller Keys
			if (isProperty ctrl #keys) and ctrl.keys != undefined then
			(
				for k = ctrl.keys.count to 1 by -1 do (deleteKey ctrl k)
			)
			-- ORT zurücksetzen
			try (setAfterORT ctrl #constant) catch()
			for i = 1 to 3 do (try (setAfterORT ctrl[i].controller #constant) catch())
		)
		catch()
	),
	
	-- Erstellt eine frische Controller-Instanz basierend auf Klasse
	fn createControllerFromClass cls =
	(
		case cls of
		(
			Euler_XYZ: Euler_XYZ()
			Linear_Rotation: Linear_Rotation()
			TCB_Rotation: TCB_Rotation()
			Position_XYZ: Position_XYZ()
			Linear_Position: Linear_Position()
			TCB_Position: TCB_Position()
			Bezier_Position: Bezier_Position()
			Bezier_Scale: Bezier_Scale()
			Linear_Scale: Linear_Scale()
			TCB_Scale: TCB_Scale()
			default: undefined
		)
	),
	
	-- Löscht alle Keys rückwärts auf Controller + Sub-Controller
	fn forceDeleteAllKeys ctrl =
	(
		try
		(
			for i = 1 to 3 do
			(
				try
				(
					local sc = ctrl[i].controller
					if sc != undefined and (isProperty sc #keys) and sc.keys != undefined then
						for k = sc.keys.count to 1 by -1 do (deleteKey sc k)
				)
				catch()
			)
			if (isProperty ctrl #keys) and ctrl.keys != undefined then
				for k = ctrl.keys.count to 1 by -1 do (deleteKey ctrl k)
			try (setAfterORT ctrl #constant) catch()
			for i = 1 to 3 do (try (setAfterORT ctrl[i].controller #constant) catch())
		)
		catch()
	),
	
	fn cancelRecording =
	(
		if not this.isRecording then return()
		
		format "\n===== DEBUG cancelRecording mode=% =====\n" (if this.isEditMode then "EDIT" else "NEW")
		format "DEBUG savedEditKeys count=%\n" this.savedEditKeys.count
		sliderTime = 0
		
		-- IMMER Original-Controller wiederherstellen (Spin/assignControllers können sie geändert haben)
		for entry in this.savedTransforms do
		(
			local obj = entry[1]
			local origPosClass = entry[3]
			local origRotClass = entry[4]
			local origSclClass = entry[5]
			try
			(
				-- Nur wiederherstellen wenn sich der Controller-Typ geändert hat
				if classOf obj.pos.controller != origPosClass then
				(
					local newPos = this.createControllerFromClass origPosClass
					if newPos != undefined then obj.pos.controller = newPos
				)
				if classOf obj.rotation.controller != origRotClass then
				(
					local newRot = this.createControllerFromClass origRotClass
					if newRot != undefined then obj.rotation.controller = newRot
				)
				if classOf obj.scale.controller != origSclClass then
				(
					local newScl = this.createControllerFromClass origSclClass
					if newScl != undefined then obj.scale.controller = newScl
				)
				format "DEBUG   restored controllers for %: pos=% rot=% scl=%\n" obj.name (classOf obj.pos.controller) (classOf obj.rotation.controller) (classOf obj.scale.controller)
			)
			catch(format "DEBUG   ctrl restore ERROR: %\n" (getCurrentException()))
		)
		
		-- Keys aus Backup wiederherstellen (beide Modi)
		for entry in this.savedEditKeys do
		(
			local obj = entry[1]
			local channel = entry[2]
			local keyData = entry[3]
			format "DEBUG   restoring %.% keyData entries=%\n" obj.name channel keyData.count
			try
			(
				local ctrl = case channel of
				(
					"pos": obj.pos.controller
					"rot": obj.rotation.controller
					"scl": obj.scale.controller
				)
				format "DEBUG   current ctrl=% keys=%\n" (classOf ctrl) (this.countControllerKeys ctrl)
				this.restoreControllerKeys ctrl keyData
				format "DEBUG   after restore keys=%\n" (this.countControllerKeys ctrl)
			)
			catch(format "DEBUG   RESTORE ERROR: %\n" (getCurrentException()))
		)
		
		animationRange = this.savedAnimRange
		this.isRecording = false
		this.isEditMode = false
		this.recordedObjects = #()
		this.savedTransforms = #()
		this.savedEditKeys = #()
		format "===== DEBUG cancelRecording END =====\n\n"
	),
	
	-- 360-Grad Spin Preset
	fn createSpinSequence duration axisIdx direction ctrlType =
	(
		
		if selection.count == 0 then
		(
			messageBox (::WdxL.t "gseq_please_select_bones_in_the_vie_msg") title:(::WdxL.t "gseq_global_sequence_manager_ptitle")
			return false
		)
		
		if ctrlType == 2 then
		(
			messageBox (::WdxL.t "gseq_step_controller_cannot_do_smoo_msg") title:(::WdxL.t "gseq_global_sequence_manager_ptitle_2")
			return false
		)
		
		local axis = case axisIdx of
		(
			1: [1,0,0]
			2: [0,1,0]
			3: [0,0,1]
			default: [0,0,1]
		)
		
		local dur = duration as integer
		
		local successCount = 0
		
		for obj in selection do
		(
			try
			(
				local savedTM = at time 0 obj.transform
				format "DEBUG savedTM pos=% rot=%\n" savedTM.pos savedTM.rotation
				
				if ctrlType == 3 then
				(
					-- === LINEAR PATH: Linear_Rotation mit 4 Segmenten à 90° ===
					format "\n===== DEBUG Spin LINEAR =====\n"
					format "DEBUG obj=% axis=% dir=% dur=%\n" obj.name axis direction dur
					format "DEBUG current rot ctrl=% keys=%\n" (classOf obj.rotation.controller) (try(obj.rotation.controller.keys.count)catch("?"))
					
					-- Pos/Scale Keys sichern (werden durch Controller-Wechsel/animate on gestört)
					local posBak = this.backupControllerKeys obj.pos.controller
					local sclBak = this.backupControllerKeys obj.scale.controller
					
					obj.rotation.controller = Linear_Rotation()
					local rc = obj.rotation.controller
					try (deleteKeys rc #allKeys) catch()
					
					-- Base-Rotation als Quaternion
					local baseQ = savedTM.rotation
					format "DEBUG baseQ=%\n" baseQ
					
					-- 5 Keys: 0°, 90°, 180°, 270°, 359.5° (4 Segmente, jedes < 180°)
					-- 359.5° statt 360° weil q und -q in Quaternion-Space identisch sind
					-- → MaxScript merged sonst den ersten und letzten Key
					local segments = 4
					local stepAngles = #(0.0, 90.0, 180.0, 270.0, 359.5)
					for step = 0 to segments do
					(
						local t = ((dur as float * step / segments) + 0.5) as integer
						if step == 0 then t = 0
						if step == segments then t = dur
						local angle = stepAngles[step + 1] * direction
						local rotQ = quat angle axis
						local finalQ = baseQ * rotQ
						-- addNewKey erzwingt Key-Erstellung (animate on merged manchmal)
						local k = addNewKey rc t
						k.value = finalQ
						format "DEBUG   step % t=%f angle=% q=%\n" step t angle finalQ
					)
					
					format "DEBUG keys after addNewKey=%\n" rc.keys.count
					
					-- Cycle ORT
					this.setControllerORT rc #cycle
					
					-- Pos/Scale Keys wiederherstellen
					this.restoreControllerKeys obj.pos.controller posBak
					this.restoreControllerKeys obj.scale.controller sclBak
					
					format "DEBUG keys on rc=%\n" (try(rc.keys.count)catch("?"))
					format "===== DEBUG Spin LINEAR END =====\n\n"
				)
				else
				(
					-- === EULER PATH: Bezier, Clear, TCB ===
					local hadEuler = (classOf obj.rotation.controller == Euler_XYZ)
					local savedEulerX = 0.0
					local savedEulerY = 0.0
					local savedEulerZ = 0.0
					if hadEuler then
					(
						local oldRc = obj.rotation.controller
						savedEulerX = at time 0 oldRc[1].controller.value
						savedEulerY = at time 0 oldRc[2].controller.value
						savedEulerZ = at time 0 oldRc[3].controller.value
					)
					
					-- Pos/Scale Keys sichern (werden durch Controller-Wechsel/animate on gestört)
					local posBak = this.backupControllerKeys obj.pos.controller
					local sclBak = this.backupControllerKeys obj.scale.controller
					
					obj.rotation.controller = Euler_XYZ()
					local rc = obj.rotation.controller
					for i = 1 to 3 do rc[i].controller = Bezier_Float()
					
					try (deleteKeys rc #allKeys) catch()
					for i = 1 to 3 do (try (deleteKeys rc[i].controller #allKeys) catch())
					
					local eulerAxis = case axisIdx of (1: 1; 2: 2; 3: 3; default: 3)
					local subCtrl = rc[eulerAxis].controller
					local baseVal = 0.0
					
					if hadEuler then
					(
						with animate on at time 0
						(
							rc[1].controller.value = savedEulerX
							rc[2].controller.value = savedEulerY
							rc[3].controller.value = savedEulerZ
						)
						baseVal = case eulerAxis of (1: savedEulerX; 2: savedEulerY; 3: savedEulerZ)
					)
					else
					(
						obj.transform = savedTM
						baseVal = subCtrl.value
						with animate on at time 0
						(
							rc[1].controller.value = rc[1].controller.value
							rc[2].controller.value = rc[2].controller.value
							rc[3].controller.value = rc[3].controller.value
						)
					)
					
					local endVal = baseVal + (360.0 * direction)
					with animate on at time dur (subCtrl.value = endVal)
					
					setAfterORT subCtrl #cycle
					this.setControllerORT rc #cycle
					
					-- Pos/Scale Keys wiederherstellen
					this.restoreControllerKeys obj.pos.controller posBak
					this.restoreControllerKeys obj.scale.controller sclBak
				)
				
				successCount += 1
			)
			catch
			(
			)
		)
		
		successCount > 0
	),
	
	mainRollout = rollout mainRollout "Global Sequence Manager" width:460 height:342
	(
		local self
		local recording = false
		local spinApplied = false
		
		-- ===== TOP: ListView =====
		dotNetControl seqListView "System.Windows.Forms.ListView" pos:[6,6] width:446 height:150
		
		-- ===== MIDDLE: Edit Bar =====
		label endFrameLbl "End Frame:" pos:[8,164] width:58 height:16
		spinner endFrameSpn pos:[68,164] width:60 height:16 range:[1,999999,0] type:#integer scale:1
		button refreshBtn "Refresh" pos:[304,162] width:70 height:20
		button removeBtn "Remove" pos:[378,162] width:70 height:20
		
		-- ===== SEPARATOR =====
		dotNetControl sep1 "System.Windows.Forms.Label" pos:[6,186] width:446 height:1
		
		-- ===== BOTTOM: Create Section =====
		groupBox createGrp "Create / Edit" pos:[6,194] width:446 height:114
		
		-- Left: Settings
		label durLbl "Duration:" pos:[16,214] width:52 height:16
		spinner durSpn pos:[70,214] width:60 height:16 range:[1,999999,30] type:#integer scale:1
		label durUnit "f" pos:[133,214] width:10 height:16
		
		label ctrlLbl "Controller:" pos:[16,236] width:55 height:16
		dropdownlist ctrlDdl items:#("Clear", "None (Step)", "Linear", "Bezier", "Hermite (TCB)") pos:[70,234] width:120 height:20
		
		label chanLbl "Channels:" pos:[16,260] width:52 height:16
		checkbox posChk "P" pos:[70,260] width:30 checked:false
		checkbox rotChk "R" pos:[102,260] width:30 checked:true
		checkbox sclChk "S" pos:[134,260] width:30 checked:false
		
		-- Right: Actions
		button beginBtn "New" pos:[220,212] width:110 height:24
		button editBtn "Edit" pos:[334,212] width:110 height:24
		
		label axisLbl "Axis:" pos:[220,242] width:28 height:16
		dropdownlist axisDdl items:#("X (Roll)", "Y", "Z") pos:[248,240] width:90 height:20 selection:3
		button spinCWBtn "> CW" pos:[344,240] width:48 height:22
		button spinCCWBtn "CCW <" pos:[396,240] width:48 height:22
		
		button finishBtn "Finish" pos:[220,268] width:110 height:24 enabled:false
		button cancelBtn "Cancel" pos:[334,268] width:110 height:24 enabled:false
		
		-- ===== STATUS BAR =====
		dotNetControl statusBar "System.Windows.Forms.Label" pos:[6,316] width:446 height:18
		
		-- ========== TYPE COLORS ==========
		fn getTypeColor type =
		(
			local dc = dotNetClass "System.Drawing.Color"
			case type of
			(
				"Bone":  dc.FromArgb 124 196 124
				"Flow":  dc.FromArgb 124 180 232
				"SEQ":   dc.FromArgb 200 152 104
				"IFL":   dc.FromArgb 200 152 104
				"Alpha": dc.FromArgb 180 160 220
				"Vis":   dc.FromArgb 220 200 120
				"Mixed": dc.FromArgb 180 180 180
				default: dc.FromArgb 200 200 200
			)
		)
		
		fn getTypeLabel type =
		(
			case type of
			(
				"Bone":  "Bone"
				"Flow":  "Flow"
				"SEQ":   "SEQ"
				"IFL":   "IFL"
				"Alpha": "Alpha"
				"Vis":   "Vis"
				"Mixed": "Mixed"
				default: type
			)
		)
		
		-- ========== UPDATE LISTVIEW ==========
		fn updateList =
		(
			seqListView.Items.Clear()
			seqListView.BeginUpdate()
			
			for i = 1 to self.globalEntries.count do
			(
				local entry = self.globalEntries[i]
				local dur = entry[1].frame as integer
				local typeStr = getTypeLabel entry[6]
				local srcCount = entry[4].count
				local srcStr = ""
				local showCount = if srcCount > 3 then 3 else srcCount
				for s = 1 to showCount do
				(
					if s > 1 then srcStr += ", "
					srcStr += entry[4][s]
				)
				if srcCount > 3 then srcStr += "..."
				
				local item = dotNetObject "System.Windows.Forms.ListViewItem" (i as string)
				item.UseItemStyleForSubItems = false
				
				local sub1 = item.SubItems.Add ("0 - " + dur as string)
				local sub2 = item.SubItems.Add typeStr
				sub2.ForeColor = getTypeColor entry[6]
				local sub3 = item.SubItems.Add srcStr
				
				seqListView.Items.Add item
			)
			
			seqListView.EndUpdate()
			statusBar.Text = self.globalEntries.count as string + " Global Sequence(s)"
		)
		
		-- ========== UPDATE EDIT FIELDS ==========
		fn getSelectedIndex =
		(
			if seqListView.SelectedIndices.Count != 1 then return 0
			seqListView.SelectedIndices.Item[0] + 1
		)
		
		fn updateEditFields =
		(
			local selId = getSelectedIndex()
			
			if selId < 1 or selId > self.globalEntries.count then
			(
				endFrameSpn.value = 0
				endFrameSpn.enabled = false
				removeBtn.enabled = false
				editBtn.enabled = false
				statusBar.Text = self.globalEntries.count as string + " Global Sequence(s)"
				return()
			)
			
			local entry = self.globalEntries[selId]
			local entryType = entry[6]
			
			-- Bone: Edit + EndFrame + Remove
			-- Flow: EndFrame + Remove (kein Edit)
			-- Vis/Alpha/IFL/SEQ: nichts
			local canEditBtn = (entryType == "Bone")
			local canEndFrame = (entryType == "Bone" or entryType == "Flow")
			local canRemove = true
			
			endFrameSpn.value = entry[1].frame as integer
			endFrameSpn.enabled = canEndFrame
			removeBtn.enabled = canRemove
			editBtn.enabled = canEditBtn
			statusBar.Text = self.globalEntries.count as string + " Global Sequence(s) -- Selected: #" + selId as string + " " + (getTypeLabel entry[6]) + " [0-" + (entry[1].frame as integer) as string + "]"
		)
		
		-- ========== SET CREATE UI STATE ==========
		fn refreshAll =
		(
			self.scanScene()
			updateList()
			updateEditFields()
			try (::WdxSequenceManager.scanGlobalSequences()) catch()
			try (::WdxSequenceManager.mainRollout.setListItems()) catch()
		)
		
		fn notifySequenceManager =
		(
			try
			(
				::WdxSequenceManager.scanGlobalSequences()
				::WdxSequenceManager.mainRollout.setListItems()
			)
			catch()
		)
		-- ========== RECORDING UI STATE ==========
		fn setRecordingUI isRec =
		(
			-- Controls die nur während Recording sichtbar sind
			ctrlLbl.visible = isRec
			ctrlDdl.visible = isRec
			chanLbl.visible = isRec
			posChk.visible = isRec
			rotChk.visible = isRec
			sclChk.visible = isRec
			axisLbl.visible = isRec
			axisDdl.visible = isRec
			spinCWBtn.visible = isRec
			spinCCWBtn.visible = isRec
			finishBtn.visible = isRec
			cancelBtn.visible = isRec
			
			-- New/Edit-Buttons nur sichtbar wenn NICHT recording
			beginBtn.visible = not isRec
			editBtn.visible = not isRec
			
			-- Liste-Controls während Recording deaktivieren (verhindert Konflikte)
			if isRec then
			(
				endFrameSpn.enabled = false
				removeBtn.enabled = false
				refreshBtn.enabled = false
				finishBtn.enabled = true
				cancelBtn.enabled = true
			)
			else
			(
				refreshBtn.enabled = true
				-- endFrameSpn/removeBtn/editBtn werden von updateEditFields gesetzt
				updateEditFields()
			)
		)
		
		-- ========== EVENT HANDLERS ==========
		on seqListView ItemSelectionChanged arg do updateEditFields()
		
		on endFrameSpn changed val do
		(
			if recording then return()
			local selId = getSelectedIndex()
			if selId < 1 then return()
			
			undo "GlobalSeqMgr Change Duration" on
			(
				if self.changeDuration selId (val as time) then
				(
					updateList()
					try (seqListView.Items.Item[selId - 1].Selected = true) catch()
					try (::WdxSequenceManager.scanGlobalSequences()) catch()
					try (::WdxSequenceManager.mainRollout.setListItems()) catch()
				)
			)
		)
		
		on refreshBtn pressed do
		(
			refreshAll()
		)
		
		on removeBtn pressed do
		(
			if recording then return()
			local selId = getSelectedIndex()
			if selId < 1 then return()
			
			if not (queryBox ("Remove Global Sequence " + selId as string + "?") title:(::WdxL.t "gseq_confirm_ptitle")) then return()
			
			undo "GlobalSeqMgr Remove" on
			(
				self.removeGlobalSequence selId
				refreshAll()
				notifySequenceManager()
			)
		)
		
		-- ===== RECORDING =====
		on beginBtn pressed do
		(
			if selection.count == 0 then
			(
				messageBox (::WdxL.t "gseq_select_objects_in_the_viewport_msg") title:(::WdxL.t "gseq_global_sequence_manager_ptitle_3")
				return()
			)
			
			-- Slider auf Frame 0 setzen damit Pose korrekt ist
			sliderTime = 0
			
			self.beginRecording durSpn.value ctrlDdl.selection posChk.checked rotChk.checked sclChk.checked
			recording = true
			spinApplied = false
			setRecordingUI true
			statusBar.Text = "RECORDING -- Keyframe objects, then click Finish"
		)
		
		on editBtn pressed do
		(
			local selId = getSelectedIndex()
			if selId < 1 or selId > self.globalEntries.count then
			(
				messageBox (::WdxL.t "gseq_select_a_global_sequence_from_msg") title:(::WdxL.t "gseq_global_sequence_manager_ptitle_4")
				return()
			)
			
			local entry = self.globalEntries[selId]
			if entry[6] != "Bone" then
			(
				messageBox (::WdxL.t "gseq_only_bone_entries_can_be_edite_msg") title:(::WdxL.t "gseq_global_sequence_manager_ptitle_5")
				return()
			)
			
			-- Objekte und Channels aus Entry ermitteln
			local objNames = #()
			local hasPos = false
			local hasRot = false
			local hasScl = false
			
			for cInfo in entry[2] do
			(
				local desc = cInfo[2]
				local dotIdx = findString desc "."
				if dotIdx != undefined then
				(
					local oName = substring desc 1 (dotIdx - 1)
					appendIfUnique objNames oName
					
					local channel = substring desc (dotIdx + 1) -1
					if channel == "Position" then hasPos = true
					else if channel == "Rotation" then hasRot = true
					else if channel == "Scale" then hasScl = true
				)
			)
			
			-- Objekte in Szene finden und selektieren
			local objs = #()
			for n in objNames do
			(
				local o = getNodeByName n
				if o != undefined then append objs o
			)
			
			if objs.count == 0 then
			(
				messageBox (::WdxL.t "gseq_could_not_find_the_objects_in_msg") title:(::WdxL.t "gseq_global_sequence_manager_ptitle_6")
				return()
			)
			
			select objs
			
			-- UI mit Entry-Werten füllen
			durSpn.value = entry[1].frame as integer
			posChk.checked = hasPos
			rotChk.checked = hasRot
			sclChk.checked = hasScl
			
			-- Slider auf Frame 0 setzen
			sliderTime = 0
			
			self.beginRecording durSpn.value ctrlDdl.selection hasPos hasRot hasScl editMode:true
			recording = true
			spinApplied = false
			setRecordingUI true
			statusBar.Text = "EDITING #" + selId as string + " -- Keyframe objects, then click Finish"
		)
		
		on finishBtn pressed do
		(
			if not recording and not spinApplied then return()
			
			undo "GlobalSeqMgr Create" on
			(
				if recording then
				(
					local result = self.finishRecording()
					local newGlobals = result[1]
					local warnings = result[2]
					recording = false
					
					if newGlobals.count == 0 then
					(
						messageBox (::WdxL.t "gseq_no_new_keys_found_msg") title:(::WdxL.t "gseq_global_sequence_manager_ptitle_7")
						recording = false
						spinApplied = false
						setRecordingUI false
						statusBar.Text = "No keys recorded."
						return()
					)
					
					if warnings.count > 0 then
					(
						local warnStr = "Warning - existing keys on: "
						for w in warnings do warnStr += w + ", "
						messageBox warnStr title:(::WdxL.t "gseq_warning_ptitle")
					)
					
					messageBox (newGlobals.count as string + " controller(s) set as Global Sequence.") title:(::WdxL.t "gseq_success_ptitle")
				)
				
				refreshAll()
				notifySequenceManager()
			)
			
			spinApplied = false
			recording = false
			setRecordingUI false
			statusBar.Text = self.globalEntries.count as string + " Global Sequence(s) -- Done"
		)
		
		on cancelBtn pressed do
		(
			if recording then
				self.cancelRecording()
			recording = false
			spinApplied = false
			setRecordingUI false
			refreshAll()
			statusBar.Text = self.globalEntries.count as string + " Global Sequence(s) -- Cancelled"
		)
		
		-- ===== SPIN PRESETS =====
		on spinCWBtn pressed do
		(
			if selection.count == 0 then
			(
				messageBox (::WdxL.t "gseq_select_objects_in_the_viewport_msg_2") title:(::WdxL.t "gseq_global_sequence_manager_ptitle_8")
				return()
			)
			undo "GlobalSeqMgr Spin CW" on
			(
				local spinEnd = (sliderTime.frame as integer)
				format "\nDEBUG spinCW: sliderTime=% spinEnd=% ctrl=% recording=% editMode=%\n" sliderTime spinEnd ctrlDdl.selection recording spinApplied
				if spinEnd < 1 then
				(
					format "GlobalSeqMgr: Move slider past frame 0.\n"
					statusBar.Text = "Spin: Move slider past frame 0."
					return()
				)
				if self.createSpinSequence spinEnd axisDdl.selection 1 ctrlDdl.selection then
				(
					spinApplied = true
					statusBar.Text = "Spin CW 0-" + spinEnd as string + "f applied. Finish or add more."
				)
				else
				(
					statusBar.Text = "Spin failed -- check Listener."
				)
			)
		)
		
		on spinCCWBtn pressed do
		(
			if selection.count == 0 then
			(
				messageBox (::WdxL.t "gseq_select_objects_in_the_viewport_msg_3") title:(::WdxL.t "gseq_global_sequence_manager_ptitle_9")
				return()
			)
			undo "GlobalSeqMgr Spin CCW" on
			(
				local spinEnd = (sliderTime.frame as integer)
				format "\nDEBUG spinCCW: sliderTime=% spinEnd=% ctrl=% recording=% editMode=%\n" sliderTime spinEnd ctrlDdl.selection recording spinApplied
				if spinEnd < 1 then
				(
					format "GlobalSeqMgr: Move slider past frame 0.\n"
					statusBar.Text = "Spin: Move slider past frame 0."
					return()
				)
				if self.createSpinSequence spinEnd axisDdl.selection -1 ctrlDdl.selection then
				(
					spinApplied = true
					statusBar.Text = "Spin CCW 0-" + spinEnd as string + "f applied. Finish or add more."
				)
				else
				(
					statusBar.Text = "Spin failed -- check Listener."
				)
			)
		)
		
		-- ===== INIT =====
		on mainRollout open do
		(
			self = WdxGlobalSequenceManager
			
			-- ListView Setup
			local dc = dotNetClass "System.Drawing.Color"
			seqListView.View = (dotNetClass "System.Windows.Forms.View").Details
			seqListView.FullRowSelect = true
			seqListView.GridLines = true
			seqListView.MultiSelect = false
			seqListView.HeaderStyle = (dotNetClass "System.Windows.Forms.ColumnHeaderStyle").Nonclickable
			seqListView.BackColor = dc.FromArgb 42 42 42
			seqListView.ForeColor = dc.FromArgb 200 200 200
			try (seqListView.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 8.0) catch()
			
			seqListView.Columns.Add "#" 28
			seqListView.Columns.Add "Frames" 65
			seqListView.Columns.Add "Type" 50
			seqListView.Columns.Add "Sources" 290
			
			-- Separator
			sep1.BackColor = dc.FromArgb 80 80 80
			
			-- Status Bar
			statusBar.BackColor = dc.FromArgb 50 50 50
			statusBar.ForeColor = dc.FromArgb 138 180 248
			try (statusBar.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 7.5) catch()
			statusBar.TextAlign = (dotNetClass "System.Drawing.ContentAlignment").MiddleLeft
			statusBar.Text = "Initializing..."
			
			-- Recording-Controls initial verstecken
			setRecordingUI false
			
			-- Undo/Redo Callback: Liste automatisch aktualisieren
			callbacks.addScript #sceneUndo "try(WdxGlobalSequenceManager.mainRollout.refreshAll())catch()" id:#GlobalSeqMgrUndo
			callbacks.addScript #sceneRedo "try(WdxGlobalSequenceManager.mainRollout.refreshAll())catch()" id:#GlobalSeqMgrRedo
			
			self.scanScene()
			updateList()
			updateEditFields()

			-- Localization
			if ::WdxL != undefined then
			(
				mainRollout.title = ::WdxL.t "gseq_global_sequence_manager_title"
				endFrameLbl.text = ::WdxL.t "gseq_end_frame_lbl"
				refreshBtn.text = ::WdxL.t "gseq_refresh_btn"
				removeBtn.text = ::WdxL.t "gseq_remove_btn"
				beginBtn.text = ::WdxL.t "gseq_new_btn"
				editBtn.text = ::WdxL.t "gseq_edit_btn"
				spinCWBtn.text = ::WdxL.t "gseq__cw_btn"
				spinCCWBtn.text = ::WdxL.t "gseq_ccw_btn"
				finishBtn.text = ::WdxL.t "gseq_finish_btn"
				cancelBtn.text = ::WdxL.t "gseq_cancel_btn"
				durLbl.text = ::WdxL.t "gseq_duration_lbl"
				ctrlLbl.text = ::WdxL.t "gseq_controller_lbl"
				chanLbl.text = ::WdxL.t "gseq_channels_lbl"
				axisLbl.text = ::WdxL.t "gseq_axis_lbl"
			)
		)
		
		on mainRollout close do
		(
			-- Undo/Redo Callbacks entfernen
			callbacks.removeScripts id:#GlobalSeqMgrUndo
			callbacks.removeScripts id:#GlobalSeqMgrRedo
			
			-- Fenster geschlossen ohne Finish = Animation löschen + Pose wiederherstellen
			if recording then
			(
				try (self.cancelRecording()) catch()
				recording = false
				spinApplied = false
			)
			notifySequenceManager()
		)
	),
	
	fn showUI =
	(
		CreateDialog this.mainRollout
	),
	
	fn hideUI =
	(
		DestroyDialog this.mainRollout
	),
	
	on create do
	(
		print "GlobalSequenceManager instanced."
	)
)

global WdxGlobalSequenceManager = WdxGlobalSequenceManager()
