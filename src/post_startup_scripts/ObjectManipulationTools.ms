/*
    MaxScript Tool: Object Manipulation Tools
    -----------------------------------------
    Extended with Poly Position Copy/Paste/Mirror.
    Same struct/rollout pattern for WhiteoutDex integration.
*/

-- Poly Position globals & logic struct (must be outside rollout)
global g_wdxPpt_vertPositions = #()
global g_wdxPpt_vertIndices = #()
global g_wdxPpt_objName = undefined
global g_wdxPpt_faceCount = 0
global g_wdxPpt_timer = undefined

struct WdxPPT_Ops
(
	fn dbg msg = (format "[PPT] %\n" msg),
	
	fn getValidObject =
	(
		if selection.count != 1 do (return undefined)
		local obj = selection[1]
		local c = classOf obj
		if c != Editable_Poly and c != Editable_Mesh do (return undefined)
		obj
	),
	
	fn getSL =
	(
		local rawSL = subObjectLevel
		if rawSL == undefined then 0 else rawSL
	),
	
	fn getSelectedVertsArray obj =
	(
		local c = classOf obj
		local sl = WdxPPT_Ops.getSL()
		local verts = #{}
		local tmpBA = #{}
		local nv = 0
		if c == Editable_Poly do
		(
			if sl == 0 do (nv = polyOp.getNumVerts obj; verts = #{1..nv})
			if sl == 1 do (verts = polyOp.getVertSelection obj)
			if sl == 2 or sl == 3 do (tmpBA = polyOp.getEdgeSelection obj; verts = polyOp.getVertsUsingEdge obj tmpBA)
			if sl == 4 or sl == 5 do (tmpBA = polyOp.getFaceSelection obj; verts = polyOp.getVertsUsingFace obj tmpBA)
		)
		if c == Editable_Mesh do
		(
			if sl == 0 do (nv = meshOp.getNumVerts obj; verts = #{1..nv})
			if sl == 1 do (verts = getVertSelection obj)
			if sl == 2 do (tmpBA = getEdgeSelection obj; verts = meshOp.getVertsUsingEdge obj tmpBA)
			if sl >= 3 do (tmpBA = getFaceSelection obj; verts = meshOp.getVertsUsingFace obj tmpBA)
		)
		verts as array
	),
	
	fn getVertPos obj vi =
	(
		local c = classOf obj
		if c == Editable_Poly then (polyOp.getVert obj vi) else (meshOp.getVert obj vi)
	),
	
	fn setVertPos obj vi pos =
	(
		local c = classOf obj
		if c == Editable_Poly do (polyOp.setVert obj #{vi} pos)
		if c == Editable_Mesh do (meshOp.setVert obj #{vi} pos)
	),
	
	fn countSelectedFaces obj =
	(
		local sl = WdxPPT_Ops.getSL()
		local c = classOf obj
		local n = 0
		if sl >= 3 do
		(
			if c == Editable_Poly do (n = (polyOp.getFaceSelection obj).numberSet)
			if c == Editable_Mesh do (n = (getFaceSelection obj).numberSet)
		)
		n
	),
	
	fn doCopy obj =
	(
		local vertArr = WdxPPT_Ops.getSelectedVertsArray obj
		if vertArr.count == 0 do (return false)
		g_wdxPpt_vertPositions = for vi in vertArr collect (WdxPPT_Ops.getVertPos obj vi)
		g_wdxPpt_vertIndices = vertArr
		g_wdxPpt_objName = obj.name
		g_wdxPpt_faceCount = WdxPPT_Ops.countSelectedFaces obj
		WdxPPT_Ops.dbg ("COPY: " + (vertArr.count as string) + " verts from \"" + obj.name + "\"")
		true
	),
	
	fn doPaste obj mirX mirY mirZ mirrorMode byIndex =
	(
		local doMirror = (mirX or mirY or mirZ)
		local targetVerts = #()
		local srcPositions = g_wdxPpt_vertPositions
		local total = srcPositions.count
		local applied = 0
		local skipped = 0
		local mirrorCenter = [0,0,0]
		local c = classOf obj
		local maxVerts = 0
		local count = 0
		local vi = 0
		local pos = [0,0,0]
		local mirroredSrc = #()
		local matchOrder = #()
		local tgtPos = [0,0,0]
		local bestDist = 0.0
		local bestIdx = 0
		local d = 0.0
		local usedFlags = #()
		local srcIdx = 0
		
		if doMirror do
		(
			if mirrorMode == 1 do (mirrorCenter = [0,0,0] * (inverse obj.transform))
			if mirrorMode == 2 do (mirrorCenter = [0,0,0])
			if mirrorMode == 3 and total > 0 do
			(
				local sum = [0,0,0]
				for p in srcPositions do (sum += p)
				mirrorCenter = sum / total
			)
		)
		
		if byIndex then (targetVerts = g_wdxPpt_vertIndices)
		else (targetVerts = WdxPPT_Ops.getSelectedVertsArray obj)
		if targetVerts.count == 0 do (return "No target selection")
		
		if c == Editable_Poly do (maxVerts = polyOp.getNumVerts obj)
		if c == Editable_Mesh do (maxVerts = meshOp.getNumVerts obj)
		
		count = total
		if targetVerts.count < count do (count = targetVerts.count)
		
		mirroredSrc = for i = 1 to total collect
		(
			pos = copy srcPositions[i]
			if mirX do (pos.x = 2.0 * mirrorCenter.x - pos.x)
			if mirY do (pos.y = 2.0 * mirrorCenter.y - pos.y)
			if mirZ do (pos.z = 2.0 * mirrorCenter.z - pos.z)
			pos
		)
		
		if byIndex or (not doMirror) then
			(matchOrder = for i = 1 to count collect i)
		else
		(
			matchOrder = #()
			usedFlags = for i = 1 to total collect false
			for t = 1 to count do
			(
				vi = targetVerts[t]
				if vi <= maxVerts then
				(
					tgtPos = WdxPPT_Ops.getVertPos obj vi
					bestDist = 1e30
					bestIdx = 1
					for s = 1 to total do
					(
						if not usedFlags[s] do
						(
							d = length (mirroredSrc[s] - tgtPos)
							if d < bestDist do (bestDist = d; bestIdx = s)
						)
					)
					usedFlags[bestIdx] = true
					append matchOrder bestIdx
				)
				else (append matchOrder 1)
			)
		)
		
		undo "PPT Paste" on
		(
			for i = 1 to count do
			(
				vi = targetVerts[i]
				if vi > maxVerts then (skipped += 1)
				else
				(
					srcIdx = matchOrder[i]
					WdxPPT_Ops.setVertPos obj vi (copy mirroredSrc[srcIdx])
					applied += 1
				)
			)
			if c == Editable_Poly do (try(obj.EditablePoly.setEPolyNotifyFlag()) catch())
			update obj
		)
		
		local msg = "Pasted " + (applied as string) + " verts"
		if skipped > 0 do (msg += " (" + (skipped as string) + " skipped)")
		WdxPPT_Ops.dbg msg
		msg
	)
)

global WdxPPT = WdxPPT_Ops()

-- ================================================================
-- Original struct with poly tools added
-- ================================================================
struct WdxObjectManipulationTools
(
	isPoppedOut = false,
	savedIndex = undefined,
	g_storedPos       = [0,0,0],
	g_hierarchyData   = #(),
	g_copyStructData  = #(),

	fn getMirroredName objName =
	(
		local name = objName
		local len = name.count
		if len == 0 then return name
		
		fn isUpper c = (c >= "A" and c <= "Z")
		fn isLower c = (c >= "a" and c <= "z")
		fn isDelim c = (c == "_" or c == " " or c == "-")
		
		local fullPairs = #(
			#("Left", "Right"), #("left", "right"), #("LEFT", "RIGHT"),
			#("Right", "Left"), #("right", "left"), #("RIGHT", "LEFT")
		)
		for pair in fullPairs do
		(
			local search = pair[1]
			local repl = pair[2]
			local sLen = search.count
			local pos = findString name search
			if pos != undefined then
			(
				local afterPos = pos + sLen
				local beforeOk = (pos == 1)
				if not beforeOk then
				(
					local prev = substring name (pos - 1) 1
					local curr = substring name pos 1
					beforeOk = isDelim prev or (isUpper curr and isLower prev)
				)
				local afterOk = (afterPos > len)
				if not afterOk then
				(
					local next = substring name afterPos 1
					afterOk = isDelim next or isUpper next
				)
				if beforeOk and afterOk then
				(
					local prefix = if pos > 1 then substring name 1 (pos - 1) else ""
					local suffix = if afterPos <= len then substring name afterPos -1 else ""
					return prefix + repl + suffix
				)
			)
		)
		
		local charPairs = #(
			#("L", "R"), #("l", "r"),
			#("R", "L"), #("r", "l")
		)
		for pair in charPairs do
		(
			local search = pair[1]
			local repl = pair[2]
			if len >= 2 and substring name len 1 == search and isDelim (substring name (len - 1) 1) then
				return substring name 1 (len - 1) + repl
			if len >= 2 and substring name 1 1 == search and isDelim (substring name 2 1) then
				return repl + substring name 2 -1
			for i = 2 to (len - 1) do
			(
				if substring name i 1 == search and isDelim (substring name (i - 1) 1) and isDelim (substring name (i + 1) 1) then
					return substring name 1 (i - 1) + repl + substring name (i + 1) -1
			)
		)
		
		for pair in charPairs do
		(
			local search = pair[1]
			local repl = pair[2]
			if not isUpper search then continue
			if len >= 2 and substring name 1 1 == search and isUpper (substring name 2 1) then
				return repl + substring name 2 -1
			for i = 2 to (len - 1) do
			(
				if substring name i 1 == search and isDelim (substring name (i - 1) 1) and isUpper (substring name (i + 1) 1) then
					return substring name 1 (i - 1) + repl + substring name (i + 1) -1
			)
		)
		
		for pair in charPairs do
		(
			local search = pair[1]
			local repl = pair[2]
			if not isUpper search then continue
			if len >= 2 and substring name 1 1 == search and isLower (substring name 2 1) then
				return repl + substring name 2 -1
		)
		
		return name
	),

	fn collectDescendants obj result =
	(
		for child in obj.children do
		(
			appendIfUnique result child
			collectDescendants child result
		)
		result
	),

	fn applyMirror pos mirrorX mirrorY mirrorZ =
	(
		local result = [ pos.x, pos.y, pos.z ]
		if mirrorX then result.x = -result.x
		if mirrorY then result.y = -result.y
		if mirrorZ then result.z = -result.z
		result
	),

	mainRollout = rollout mainRollout "Object Manipulation Tools" width:440 height:362
	(
		local self

		/* =========================================
		   OBJECT TOOLS (original - unchanged)
		   ========================================= */
		groupBox grpTools "Tools" pos:[8,32] width:296 height:125

		-- Row 1: WdxHierarchy
		label lblHierarchy "WdxHierarchy" pos:[20,54] width:90 height:15
		button btnCopyHierarchy  "Copy"  pos:[130,51] width:80 height:20 tooltip:"Store parent-child relationships and unlink"
		button btnPasteHierarchy "Paste" pos:[215,51] width:80 height:20 tooltip:"Restore parent-child relationships"

		-- Row 2: Position
		label lblPosition "Position" pos:[20,80] width:90 height:15
		button btnCopyPos  "Copy"  pos:[130,77] width:80 height:20 tooltip:"Copy position of first selected object"
		button btnPastePos "Paste" pos:[215,77] width:80 height:20 tooltip:"Paste stored position to selection"

		-- Row 3: Structure
		label lblStructure "Structure (L\x2194R)" pos:[20,106] width:100 height:15
		button btnCopyStruct  "Copy"  pos:[130,103] width:80 height:20 tooltip:"Copy positions of all selected objects with names"
		button btnPasteStruct "Paste" pos:[215,103] width:80 height:20 tooltip:"Paste to mirrored L/R named objects"

		/* =========================================
		   OBJECT PASTE OPTIONS (original - unchanged)
		   ========================================= */
		groupBox grpOptions "Paste Options" pos:[308,32] width:128 height:125

		checkBox chkHierarchy "WdxHierarchy" pos:[320,54] checked:false tooltip:"Temporarily unlink/relink during paste"
		checkBox chkMirror  "Mirror"     pos:[320,77] checked:false tooltip:"Enable position mirroring on paste"
		checkBox chkMirrorX "X"          pos:[336,97] enabled:false tooltip:"Mirror along X axis"
		checkBox chkMirrorY "Y"          pos:[336,115] enabled:false tooltip:"Mirror along Y axis"
		checkBox chkMirrorZ "Z"          pos:[336,133] enabled:false tooltip:"Mirror along Z axis"

		/* =========================================
		   POLY TOOLS (new)
		   ========================================= */
		groupBox grpPoly "Poly Tools" pos:[8,162] width:296 height:125

		label lblPolyInfo "" pos:[20,180] width:270 height:15
		label lblPolySel "" pos:[20,196] width:270 height:15

		button btnPolyCopy  "Copy"  pos:[130,215] width:80 height:20 tooltip:"Copy vertex positions from sub-object selection"
		button btnPolyPaste "Paste" pos:[215,215] width:80 height:20 tooltip:"Paste vertex positions" enabled:false
		label lblPolyBuf "Buffer: empty" pos:[20,239] width:270 height:15

		/* =========================================
		   POLY OPTIONS (new)
		   ========================================= */
		groupBox grpPolyOpts "Poly Options" pos:[308,162] width:128 height:125

		checkBox chkPolyMirX "Mirror X" pos:[320,182] tooltip:"Mirror vertex X on paste"
		checkBox chkPolyMirY "Mirror Y" pos:[320,200] tooltip:"Mirror vertex Y on paste"
		checkBox chkPolyMirZ "Mirror Z" pos:[320,218] tooltip:"Mirror vertex Z on paste"
		radioButtons rdoPolyCenter labels:#("World", "Pivot", "Center") pos:[320,240] columns:1

		/* =========================================
		   POLY PASTE MODE (new)
		   ========================================= */
		groupBox grpPasteMode "Poly Paste Mode" pos:[8,292] width:428 height:38
		radioButtons rdoPolyMode labels:#("By Index (same verts)", "To Selection (other polys)") pos:[20,310] default:2 columns:2

		label lblStatus "" pos:[8,336] width:428 height:16

		/* =========================================
		   OBJECT EVENTS (original - unchanged)
		   ========================================= */
		on chkMirror changed state do
		(
			chkMirrorX.enabled = state
			chkMirrorY.enabled = state
			chkMirrorZ.enabled = state
		)

		-- A) Copy / Paste HIERARCHY
		on btnCopyHierarchy pressed do
		(
			undo "Copy WdxHierarchy (Unlink)" on
			(
				if selection.count > 0 then
				(
					self.g_hierarchyData = #()
					for obj in selection do
					(
						local oldParent = obj.parent
						append self.g_hierarchyData #(obj, oldParent)
						obj.parent = undefined
					)
					lblStatus.text = "WdxHierarchy: copied " + (selection.count as string) + " objects"
					format "Copy WdxHierarchy: Processed % objects.\n" selection.count
				)
				else
				(
					messageBox (::WdxL.t "omt_please_select_at_least_one_obj_msg") title:(::WdxL.t "omt_error_ptitle")
				)
			)
		)

		on btnPasteHierarchy pressed do
		(
			undo "Paste WdxHierarchy (Relink)" on
			(
				if self.g_hierarchyData.count > 0 then
				(
					for entry in self.g_hierarchyData do
					(
						local myObj  = entry[1]
						local oldPar = entry[2]
						if (isvalidnode myObj) and (isvalidnode oldPar or oldPar == undefined) then
						(
							myObj.parent = oldPar
						)
					)
					lblStatus.text = "WdxHierarchy: restored " + (self.g_hierarchyData.count as string) + " entries"
					format "Paste WdxHierarchy: Processed % entries.\n" self.g_hierarchyData.count
				)
				else
				(
					messageBox (::WdxL.t "omt_no_hierarchy_data_available_msg") title:(::WdxL.t "omt_note_ptitle")
				)
			)
		)

		-- B) Copy / Paste POSITION
		on btnCopyPos pressed do
		(
			if selection.count > 0 then
			(
				self.g_storedPos = selection[1].pos
				lblStatus.text = "Position: copied " + (self.g_storedPos as string)
				format "Copied position: %\n" self.g_storedPos
			)
			else
			(
				messageBox (::WdxL.t "omt_please_select_at_least_one_obj_msg_2") title:(::WdxL.t "omt_error_ptitle_2")
			)
		)

		on btnPastePos pressed do
		(
			undo "Paste Position" on
			(
				if selection.count > 0 then
				(
					for obj in selection do
					(
						local oldParent = undefined
						if chkHierarchy.checked and (obj.parent != undefined) then
						(
							oldParent = obj.parent
							obj.parent = undefined
						)
						local newPos = [ self.g_storedPos.x, self.g_storedPos.y, self.g_storedPos.z ]
						if chkMirror.checked then
							newPos = self.applyMirror newPos chkMirrorX.checked chkMirrorY.checked chkMirrorZ.checked
						obj.pos = newPos
						if chkHierarchy.checked and (oldParent != undefined) then
						(
							obj.parent = oldParent
						)
					)
					lblStatus.text = "Position: pasted to " + (selection.count as string) + " objects"
				)
				else
				(
					messageBox (::WdxL.t "omt_please_select_at_least_one_obj_msg_3") title:(::WdxL.t "omt_error_ptitle_3")
				)
			)
		)

		-- C) Copy / Paste STRUCTURE
		on btnCopyStruct pressed do
		(
			if selection.count > 0 then
			(
				self.g_copyStructData = #()
				local allObjects = #()
				for obj in selection do
				(
					appendIfUnique allObjects obj
					self.collectDescendants obj allObjects
				)
				for obj in allObjects do
				(
					append self.g_copyStructData #(obj.name, obj.pos)
				)
				lblStatus.text = "Structure: copied " + (self.g_copyStructData.count as string) + " objects"
				format "Copy Structure: % objects recorded (% selected + children).\n" self.g_copyStructData.count selection.count
			)
			else
			(
				messageBox (::WdxL.t "omt_please_select_at_least_one_obj_msg_4") title:(::WdxL.t "omt_error_ptitle_4")
			)
		)

		on btnPasteStruct pressed do
		(
			undo "Paste Structure" on
			(
				if self.g_copyStructData.count > 0 then
				(
					local transferred = 0
					local notFound = 0
					for entry in self.g_copyStructData do
					(
						local oldName  = entry[1]
						local oldPos   = entry[2]
						local newName  = self.getMirroredName oldName
						local target   = getNodeByName newName
						if target != undefined then
						(
							local oldParent = undefined
							if chkHierarchy.checked and (target.parent != undefined) then
							(
								oldParent = target.parent
								target.parent = undefined
							)
							local finalPos = [ oldPos.x, oldPos.y, oldPos.z ]
							if chkMirror.checked then
								finalPos = self.applyMirror finalPos chkMirrorX.checked chkMirrorY.checked chkMirrorZ.checked
							target.pos = finalPos
							if chkHierarchy.checked and (oldParent != undefined) then
							(
								target.parent = oldParent
							)
							transferred += 1
						)
						else
						(
							format "Paste Structure: Could not find '%' (mirrored from '%').\n" newName oldName
							notFound += 1
						)
					)
					lblStatus.text = "Structure: " + (transferred as string) + " done, " + (notFound as string) + " not found"
					format "Paste Structure: % transferred, % not found.\n" transferred notFound
				)
				else
				(
					messageBox (::WdxL.t "omt_no_structure_data_available_msg") title:(::WdxL.t "omt_note_ptitle_2")
				)
			)
		)

		/* =========================================
		   POLY EVENTS (new)
		   ========================================= */
		fn updatePolyInfo =
		(
			local obj = WdxPPT.getValidObject()
			if obj != undefined then
			(
				local vn = (WdxPPT.getSelectedVertsArray obj).count
				local nf = WdxPPT.countSelectedFaces obj
				local sl = WdxPPT.getSL()
				local slNames = #("Obj","Vtx","Edge","Bdr","Poly","Elem")
				local slN = "?"
				if sl >= 0 and sl <= 5 do (slN = slNames[sl + 1])
				lblPolyInfo.text = obj.name + "  [" + slN + "]"
				lblPolySel.text = (vn as string) + " verts  |  " + (nf as string) + " faces selected"
			)
			else
			(
				lblPolyInfo.text = ""
				lblPolySel.text = ""
			)
		)

		on btnPolyCopy pressed do
		(
			local obj = WdxPPT.getValidObject()
			if obj == undefined do (messageBox (::WdxL.t "omt_select_an_editable_polymesh_msg") title:(::WdxL.t "omt_error_ptitle_5"); return())
			local ok = WdxPPT.doCopy obj
			if ok then
			(
				lblPolyBuf.text = "Buffer: " + (g_wdxPpt_vertPositions.count as string) + "v from " + obj.name
				btnPolyPaste.enabled = true
				lblStatus.text = "Poly: copied " + (g_wdxPpt_vertPositions.count as string) + " verts"
			)
			else (messageBox (::WdxL.t "omt_no_subobject_selection_msg") title:(::WdxL.t "omt_error_ptitle_6"))
		)

		on btnPolyPaste pressed do
		(
			if g_wdxPpt_vertPositions.count == 0 do (messageBox (::WdxL.t "omt_copy_first_msg") title:(::WdxL.t "omt_error_ptitle_7"); return())
			local obj = WdxPPT.getValidObject()
			if obj == undefined do (messageBox (::WdxL.t "omt_select_an_editable_polymesh_msg_2") title:(::WdxL.t "omt_error_ptitle_8"); return())

			local byIdx = (rdoPolyMode.state == 1)
			if not byIdx do
			(
				local tgt = WdxPPT.getSelectedVertsArray obj
				if tgt.count == 0 do (messageBox (::WdxL.t "omt_no_target_selection_msg") title:(::WdxL.t "omt_error_ptitle_9"); return())
				if tgt.count != g_wdxPpt_vertPositions.count do
				(
					local mn = tgt.count
					if g_wdxPpt_vertPositions.count < mn do (mn = g_wdxPpt_vertPositions.count)
					local ok = queryBox ("Mismatch: src=" + (g_wdxPpt_vertPositions.count as string) + " tgt=" + (tgt.count as string) + "\nUse " + (mn as string) + "?") title:(::WdxL.t "omt_mismatch_ptitle")
					if not ok do (return())
				)
			)

			local result = WdxPPT.doPaste obj chkPolyMirX.checked chkPolyMirY.checked chkPolyMirZ.checked rdoPolyCenter.state byIdx
			lblStatus.text = "Poly: " + result
		)

		/* =========================================
		   POP OUT / DOCK
		   ========================================= */
		on popOutBtn pressed do
		(
			if ::WdxObjectManipulationTools.isPoppedOut then
			(
				::WdxObjectManipulationTools.isPoppedOut = false
				try ( destroyDialog mainRollout ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxObjectManipulationTools.savedIndex
					if idx != undefined and idx <= rollouts.count then
					(
						local movedRollouts = #()
						for i = rollouts.count to idx by -1 do
						(
							if rollouts[i].title != "WhiteoutDex Manager Floater Rollout" then
							(
								append movedRollouts rollouts[i]
								removeRollout rollouts[i] ::whiteoutdexManagerFloater
							)
						)
						addRollout mainRollout ::whiteoutdexManagerFloater
						for i = movedRollouts.count to 1 by -1 do
							addRollout movedRollouts[i] ::whiteoutdexManagerFloater
					)
					else
						addRollout mainRollout ::whiteoutdexManagerFloater
				)
				return()
			)
			
			if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
			(
				local rollouts = ::whiteoutdexManagerFloater.rollouts
				for i = 1 to rollouts.count do
					if rollouts[i] == mainRollout then ( ::WdxObjectManipulationTools.savedIndex = i; exit )
				removeRollout mainRollout ::whiteoutdexManagerFloater
			)
			
			::WdxObjectManipulationTools.isPoppedOut = true
			createDialog mainRollout width:440 height:362 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
		)

		/* =========================================
		   OPEN / CLOSE
		   ========================================= */
		on mainRollout open do
		(
			-- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
			try (WdxSetDialogIcon mainRollout) catch()
			self = WdxObjectManipulationTools
			updatePolyInfo()
			if g_wdxPpt_vertPositions.count > 0 do
			(
				local name = "?"
				if g_wdxPpt_objName != undefined do (name = g_wdxPpt_objName)
				lblPolyBuf.text = "Buffer: " + (g_wdxPpt_vertPositions.count as string) + "v from " + name
				btnPolyPaste.enabled = true
			)
			-- Auto-refresh timer for poly selection info
			g_wdxPpt_timer = dotNetObject "System.Windows.Forms.Timer"
			g_wdxPpt_timer.Interval = 500
			dotNet.addEventHandler g_wdxPpt_timer "Tick" (fn pptTick s e = (try(mainRollout.updatePolyInfo()) catch()))
			g_wdxPpt_timer.Start()
			
			-- Localization
			if ::WdxL != undefined then
			(
				mainRollout.title = ::WdxL.t "omt_object_manipulation_tools_title"
				grpTools.text = ::WdxL.t "omt_tools_grp"
				grpOptions.text = ::WdxL.t "omt_paste_options_grp"
				grpPoly.text = ::WdxL.t "omt_poly_tools_grp"
				grpPolyOpts.text = ::WdxL.t "omt_poly_options_grp"
				grpPasteMode.text = ::WdxL.t "omt_poly_paste_mode_grp"
				-- Object Tools
				lblHierarchy.text = ::WdxL.t "omt_hierarchy_lbl"
				lblPosition.text = ::WdxL.t "omt_position_lbl"
				lblStructure.text = ::WdxL.t "omt_structure_lx2194r_lbl"
				btnCopyHierarchy.text = ::WdxL.t "omt_copy_btn"
				try (btnCopyHierarchy.tooltip = ::WdxL.t "omt_store_parentchild_relationshi_tip") catch ()
				btnPasteHierarchy.text = ::WdxL.t "omt_paste_btn"
				try (btnPasteHierarchy.tooltip = ::WdxL.t "omt_restore_parentchild_relations_tip") catch ()
				btnCopyPos.text = ::WdxL.t "omt_copy_btn_2"
				try (btnCopyPos.tooltip = ::WdxL.t "omt_copy_position_of_first_selecte_tip") catch ()
				btnPastePos.text = ::WdxL.t "omt_paste_btn_2"
				try (btnPastePos.tooltip = ::WdxL.t "omt_paste_stored_position_to_selec_tip") catch ()
				btnCopyStruct.text = ::WdxL.t "omt_copy_btn_3"
				try (btnCopyStruct.tooltip = ::WdxL.t "omt_copy_positions_of_all_selected_tip") catch ()
				btnPasteStruct.text = ::WdxL.t "omt_paste_btn_3"
				try (btnPasteStruct.tooltip = ::WdxL.t "omt_paste_to_mirrored_lr_named_ob_tip") catch ()
				-- Paste Options
				chkHierarchy.text = ::WdxL.t "omt_hierarchy_lbl"
				try (chkHierarchy.tooltip = ::WdxL.t "omt_temporarily_unlinkrelink_duri_tip") catch ()
				try (chkMirror.tooltip = ::WdxL.t "omt_enable_position_mirroring_on_p_tip") catch ()
				try (chkMirrorX.tooltip = ::WdxL.t "omt_mirror_along_x_axis_tip") catch ()
				try (chkMirrorY.tooltip = ::WdxL.t "omt_mirror_along_y_axis_tip") catch ()
				try (chkMirrorZ.tooltip = ::WdxL.t "omt_mirror_along_z_axis_tip") catch ()
				-- Poly Tools
				btnPolyCopy.text = ::WdxL.t "omt_copy_btn_4"
				try (btnPolyCopy.tooltip = ::WdxL.t "omt_copy_vertex_positions_from_sub_tip") catch ()
				btnPolyPaste.text = ::WdxL.t "omt_paste_btn_4"
				try (btnPolyPaste.tooltip = ::WdxL.t "omt_paste_vertex_positions_tip") catch ()
				lblPolyBuf.text = ::WdxL.t "omt_buffer_empty_lbl"
				-- Poly Options
				try (chkPolyMirX.tooltip = ::WdxL.t "omt_mirror_vertex_x_on_paste_tip") catch ()
				try (chkPolyMirY.tooltip = ::WdxL.t "omt_mirror_vertex_y_on_paste_tip") catch ()
				try (chkPolyMirZ.tooltip = ::WdxL.t "omt_mirror_vertex_z_on_paste_tip") catch ()
			)
		)

		on mainRollout close do
		(
			if ::WdxObjectManipulationTools.isPoppedOut then
			(
				::WdxObjectManipulationTools.isPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxObjectManipulationTools.mainRollout
							local idx = ::WdxObjectManipulationTools.savedIndex
							local rollouts = ::whiteoutdexManagerFloater.rollouts
							if idx != undefined and idx <= rollouts.count then
							(
								local moved = #()
								for i = rollouts.count to idx by -1 do (if rollouts[i].title != "WhiteoutDex Manager Floater Rollout" then (append moved rollouts[i]; removeRollout rollouts[i] ::whiteoutdexManagerFloater))
								addRollout rl ::whiteoutdexManagerFloater
								for i = moved.count to 1 by -1 do addRollout moved[i] ::whiteoutdexManagerFloater
							)
							else addRollout rl ::whiteoutdexManagerFloater
						)
					) catch()
				))
				tmr.Start()
			)
			if g_wdxPpt_timer != undefined do
			(
				g_wdxPpt_timer.Stop()
				g_wdxPpt_timer = undefined
			)
		)
	),

	on create do
	(
	)
)
global WdxObjectManipulationTools = WdxObjectManipulationTools()
