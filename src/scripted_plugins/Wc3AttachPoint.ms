
Global attachmentList = #("Head", "Overhead", "Origin", "Foot", "Chest", "Hand", "Weapon", "Sprite")
Global attachmentAdd1List = #("None", "Right", "Left", "Mount Left", "Mount Right")
Global attachmentAdd2List = #("None", "RallyPoint", "EatTree", "Gold", "Mount", "Mount Rear", "Rear", "Smart", "Alternate", "First", "Second", "Third", "Fourth", "Fifth", "Sixth")
-- The four parts the panel builds the node name from - the exporter writes
-- the node name, e.g. "Chest Mount Left Rear Ref". Mount and side as columns
-- of their own (as NeoDexPlus has them), so every combination can be picked;
-- the extra list keeps what the old "Additional" list had besides them.
-- attachmentAdd1List / attachmentAdd2List stay for the hidden legacy
-- controls that the saved nameAdd1 / nameAdd2 values are bound to.
Global attachmentMountList = #("None", "Mount")
Global attachmentSideList = #("None", "Right", "Left")
Global attachmentExtraList = #("None", "Rear", "Alternate", "RallyPoint", "EatTree", "Gold", "Smart", "First", "Second", "Third", "Fourth", "Fifth", "Sixth")

plugin simpleManipulator Wdx_Wc3AttachPoint
name:"Wc3 AttachPoint" 
classID:#(0x7A1B2C01, 0x3D4E5F01)
category:"Warcraft III Plugins" 
(
    -------------------------------------------------------------------------------------------------------
    parameters pblock rollout:General
    (
        adsorption  type:#string ui:Temp_adsorption
    )
    parameters pblock rollout:params
    (
        nameFirst           type:#integer  animatable:false    ui:first                default:1
        nameAdd1            type:#integer  animatable:false    ui:add1                 default:1
        nameAdd2            type:#integer  animatable:false    ui:add2                 default:1
        usesExternalModel   type:#boolean  animatable:false    ui:chk1                 default:false
        externalModelPath   type:#string   animatable:false    ui:edt2                 default:""
        usesAttachmentId    type:#boolean  animatable:false    ui:chk2                 default:false
        attachmentId        type:#integer  animatable:false    ui:spn2                 default:0
        A_Visibility        type:#float    animatable:true     ui:Ath_Visibility       default:1.000
    )
	-----------------------------------------------------------------------------------------------------
    rollout General "General Functions"
	(
		PickButton btn_adsorption "Select Object (Adsorb)" pos:[12,7] width:138 height:24
		edittext Temp_adsorption "" pos:[9999,0] width:0 height:0
		on btn_adsorption picked obj do
		(
			try
			(
				selection[1].pos     = obj.pos
				selection[1].parent  = obj
				Temp_adsorption.text = obj.name
				btn_adsorption.text = (::WdxL.t "common_parent_prefix") + Temp_adsorption.text
			)catch()
		)
		on General open do
		(
			if ::WdxL != undefined then
			(
				General.title = ::WdxL.t "attach_general_functions_title"
				-- Only reset the caption while nothing is adsorbed; once a
				-- parent is picked the button shows that name instead.
				if Temp_adsorption.text == "" do
					btn_adsorption.text = ::WdxL.t "common_adsorb_btn"
			)
		)
	)
	rollout modelMapParams "Model Map Parameters"
	(
		label mlbl1 "- Model Path Prefix -" pos:[25,7] align:#center
		edittext mdxPrefix pos:[8,25] height:20 width:142
		label mlbl2 "- Model File -" pos:[40,50] align:#center
		edittext mdxFile pos:[8,68] height:20 width:142
		button btnImportMDX "Import MDX Model" pos:[12,96] height:24 width:138
		button btnBrowseMPQ "Browse Game Archives" pos:[12,124] height:24 width:138 tooltip:"Pick an .mdx / .mdl straight out of the CASC or MPQ archives"

		fn updateModelPath =
		(
			local prefix = mdxPrefix.text
			local file = mdxFile.text
			if file != "" then
				this.externalModelPath = prefix + file
			else
				this.externalModelPath = prefix
		)

		fn splitModelPath =
		(
			local fullPath = this.externalModelPath
			if fullPath == undefined or fullPath == "" then
			(
				mdxPrefix.text = ""
				mdxFile.text = ""
			)
			else
			(
				local lastSlash = 0
				for i = 1 to fullPath.count do
				(
					if fullPath[i] == "\\" or fullPath[i] == "/" do lastSlash = i
				)
				if lastSlash > 0 then
				(
					mdxPrefix.text = substring fullPath 1 lastSlash
					mdxFile.text = substring fullPath (lastSlash + 1) -1
				)
				else
				(
					mdxPrefix.text = ""
					mdxFile.text = fullPath
				)
			)
		)

		on mdxPrefix changed txt do updateModelPath()
		on mdxFile changed txt do updateModelPath()

		on btnImportMDX pressed do
		(
			mdxFile.enabled = false
			-- Only the filter group names are translated; the extension masks
			-- after each '|' are parsed by Max and stay verbatim.
			local f = getOpenFileName caption:(::WdxL.t "attach_select_mdx_cap") \
				types:((::WdxL.t "attach_mdx_filter") + " (*.mdx)|*.mdx|" + (::WdxL.t "attach_mdl_filter") + " (*.mdl)|*.mdl|" + (::WdxL.t "bmp_all_files_filter") + " (*.*)|*.*|") \
				historyCategory:"AttachPointModel"
			if f != undefined then
			(
				mdxFile.text = filenameFromPath f
				if mdxPrefix.text == "" do mdxPrefix.text = getFilenamePath f
				updateModelPath()
			)
			mdxFile.enabled = true
		)

		-- Browse the game's own archives. What comes back is the CASC-relative
		-- path ("units\nightelf\druid\druid.mdx") - the form MDX stores and the
		-- exporter round-trips - so it splits straight into prefix + file. The
		-- texture browser used to be wired here, which was the wrong tool: it
		-- lists .blp/.dds, not models.
		on btnBrowseMPQ pressed do
		(
			local picked = undefined
			try (
				picked = ::WhiteoutDexModelBrowser.pickModel initial:this.externalModelPath \
					caption:(::WdxL.t "attach_select_model_cap")
			) catch (
				messageBox (::WdxL.t "common_asset_browser_unavailable_msg") title:"WhiteoutDex" beep:false
			)
			if picked != undefined and picked != "" do
			(
				local lastSlash = 0
				for i = 1 to picked.count do
					if picked[i] == "\\" or picked[i] == "/" do lastSlash = i
				if lastSlash > 0 then
				(
					mdxPrefix.text = substring picked 1 lastSlash
					mdxFile.text = substring picked (lastSlash + 1) -1
				)
				else
				(
					mdxPrefix.text = ""
					mdxFile.text = picked
				)
				updateModelPath()
			)
		)

		on modelMapParams open do
		(
			splitModelPath()

			-- Enable/disable browse button. Only the renderer .dlx matters here:
			-- the browser dialog picks its own source and can be pointed at an
			-- install by hand, so gating on a configured MPQ/CASC path would
			-- disable the one control that tells the user to configure one.
			local browserAvailable = false
			try ( browserAvailable = ::WhiteoutDexModelBrowser.isAvailable() ) catch ( browserAvailable = false )
			btnBrowseMPQ.enabled = browserAvailable

			if ::WdxL != undefined then
			(
				modelMapParams.title	= ::WdxL.t "attach_model_map_params_title"
				mlbl1.text				= ::WdxL.t "attach_model_path_prefix_lbl"
				mlbl2.text				= ::WdxL.t "attach_model_file_lbl"
				btnImportMDX.text		= ::WdxL.t "attach_import_mdx_btn"
				btnBrowseMPQ.text		= ::WdxL.t "attach_browse_archives_btn"
				try (btnBrowseMPQ.tooltip = ::WdxL.t "attach_browse_archives_tip") catch ()
			)
		)
	)
	rollout params "Attachment Parameters"
	(
		group "Basic Parameters"
		(
			label        first_Str  "Type:"        align:#left     width:138   pos:[12,26]
			dropDownList first                     align:#center   width:138   pos:[12,44]     items:attachmentList
			label        mount_Str  "Mount:"       align:#left     width:138   pos:[12,71]
			dropDownList ddMount                   align:#center   width:138   pos:[12,89]     items:attachmentMountList
			label        side_Str   "Side:"        align:#left     width:138   pos:[12,117]
			dropDownList ddSide                    align:#center   width:138   pos:[12,134]    items:attachmentSideList
			label        extra_Str  "Extra:"       align:#left     width:138   pos:[12,162]
			dropDownList ddExtra                   align:#center   width:138   pos:[12,179]    items:attachmentExtraList
			label        hhf1                      height:0        offset:[999,-1]
		)
		group "Extended Parameters 1"
		(
			editText    edt2                                           pos:[9999,0]     height:0   width:0
			label       B1          "Use External Model ............"  align:#left      pos:[12,238]                 width:124
			checkbox    chk1                                                           pos:[137,238]   height:16   width:16
			label       B2          "Visibility .........."                           pos:[12,262]                 width:85
			spinner     Ath_Visibility                                 align:#left     pos:[94,260]    height:16   fieldwidth:45   range:[0.0,1.0,0.00]    type:#float
			button      btnInfo     "Use Tips"                                        pos:[12,284]     height:24    width:138
			label       hhf2        ""                                                 height:0
		)
		group "Extended Parameters 2"
		(
			label       B3          "ID ................."           align:#left     pos:[12,346]
			spinner     spn2        fieldwidth:45   height:16       align:#left     pos:[94,346]    range:[0,100,0] type:#integer
			label       B4          "Use Attachment ID ..............."  align:#left      pos:[12,369]
			checkbox    chk2                                                        pos:[137,369]   height:16   width:16
			label       hhf3                        height:0                       offset:[999,-2]
		)
		-- The old Direction / Additional controls, hidden and outside every
		-- group (a group box sizes itself around its controls): nameAdd1 and
		-- nameAdd2 stay ui:-bound to them, so the parameter definition - and
		-- with it every saved scene's values - stays as it was.
		dropDownList add1  width:10  pos:[9999,0]  items:attachmentAdd1List
		dropDownList add2  width:10  pos:[9999,0]  items:attachmentAdd2List

		-- The node name is the attachment's name in the MDX. The four lists
		-- are read from it when the panel opens and write it back when one of
		-- them changes: "<Type>[ Mount][ <Side>][ <Extra>][ <other words>] Ref".
		-- Words the lists do not know (e.g. "Large") are kept as they are.
		local otherWords = ""
		fn thisNode = (try (refs.dependentNodes this firstOnly:true) catch (undefined))
		fn listIndex list word =
		(
			for i = 2 to list.count where (stricmp list[i] word) == 0 do return i
			0
		)
		fn readName =
		(
			local node = thisNode()
			if node == undefined then return false
			local words = filterString node.name " "
			-- "Ref" (and NeoDex's "- Ref") end the name; they are added back
			while words.count > 0 and ((stricmp words[words.count] "Ref") == 0 or words[words.count] == "-") do
				deleteItem words words.count
			local t = 0, m = 1, s = 1, e = 1
			local rest = #()
			for w in words do
			(
				local i = 0
				if t == 0 do
				(
					for k = 1 to attachmentList.count where (stricmp attachmentList[k] w) == 0 do i = k
					if i > 0 do ( t = i; continue )
				)
				i = listIndex attachmentMountList w
				if i > 0 and m == 1 do ( m = i; continue )
				i = listIndex attachmentSideList w
				if i > 0 and s == 1 do ( s = i; continue )
				i = listIndex attachmentExtraList w
				if i > 0 and e == 1 do ( e = i; continue )
				append rest w
			)
			if t > 0 do
			(
				first.selection = t
				if nameFirst != t do nameFirst = t
			)
			ddMount.selection = m
			ddSide.selection = s
			ddExtra.selection = e
			otherWords = ""
			for w in rest do otherWords += " " + w
			true
		)
		fn writeName =
		(
			local node = thisNode()
			if node == undefined then return false
			local n = attachmentList[first.selection]
			if ddMount.selection > 1 do n += " " + attachmentMountList[ddMount.selection]
			if ddSide.selection > 1 do n += " " + attachmentSideList[ddSide.selection]
			if ddExtra.selection > 1 do n += " " + attachmentExtraList[ddExtra.selection]
			node.name = n + otherWords + " Ref"
			true
		)
		on first selected i do writeName()
		on ddMount selected i do writeName()
		on ddSide selected i do writeName()
		on ddExtra selected i do writeName()

		on btnInfo pressed do
		(
			messageBox ((::WdxL.t "attach_model_path_info_msg") + "    ") beep: false
		)
		on params open do
		(
			add1.visible = false
			add2.visible = false
			try (readName()) catch ()
			if ::WdxL != undefined then
			(
				params.title = ::WdxL.t "attach_attachment_parameters_title"
				first_Str.text = ::WdxL.t "attach_type_lbl"
				mount_Str.text = ::WdxL.t "attach_mount_lbl"
				side_Str.text = ::WdxL.t "attach_side_lbl"
				extra_Str.text = ::WdxL.t "attach_extra_lbl"
				B1.text = ::WdxL.t "attach_use_external_model_lbl"
				B2.text = ::WdxL.t "attach_visibility_lbl"
				btnInfo.text = ::WdxL.t "attach_use_tips_btn"
				B3.text = ::WdxL.t "attach_id_lbl"
				B4.text = ::WdxL.t "attach_use_attachment_id_lbl"
			)
		)
	)
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	tool create
    (
        on mousePoint click do
        (
            case click of
            (1:
				(
					nodeTM.translation = gridPoint
					#stop
                )
            )
        )
    )
    
    function BuildGizmo points Drawcolor=
    (
        local unselectedColor = Drawcolor
        local selectedColor = Drawcolor
        local giz = manip.makeGizmoShape()
        giz.startNewLine()
        for pnt in points do giz.addpoint pnt
        this.addGizmoShape giz 0 unselectedColor selectedColor
    )
    
    on updateGizmos do
    (
        this.clearGizmos()
		local Drawcolor = [255,222,38]
		Drawcolor = [Drawcolor.x/255,Drawcolor.y/255,Drawcolor.z/255]
        BuildGizmo #([0,-4,-10], [5,0,-10], [0,4,-10], [0,-4,-10], [0,0,0],[0,4,-10] ) Drawcolor
        BuildGizmo #([0,0,0],[5,0,-10]) Drawcolor
    )
	-----------------------------------------------------------------------------------------------------
)