
Global attachmentList = #("Head", "Overhead", "Origin", "Foot", "Chest", "Hand", "Weapon", "Sprite")
Global attachmentAdd1List = #("None", "Right", "Left", "Mount Left", "Mount Right")
Global attachmentAdd2List = #("None", "RallyPoint", "EatTree", "Gold", "Mount", "Mount Rear", "Rear", "Smart", "Alternate", "First", "Second", "Third", "Fourth", "Fifth", "Sixth")

plugin simpleManipulator Wc3_AttachPoint
name:"AttachPoint" 
classID:#(0x1136ac20, 0x6f9cfeb7)
category:"Warcraft 3 Systems" 
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
				btn_adsorption.text  = "Parent: " + Temp_adsorption.text
			)catch()
		)
		on General open do
		(
			if ::L != undefined then
				General.title = ::L.t "attach_general_functions_title"
		)
	)
	rollout modelMapParams "Model Map Parameters"
	(
		label mlbl1 "- Model Path Prefix -" pos:[25,7] align:#center
		edittext mdxPrefix pos:[8,25] height:20 width:142
		label mlbl2 "- Model File -" pos:[40,50] align:#center
		edittext mdxFile pos:[8,68] height:20 width:142
		button btnImportMDX "Import MDX Model" pos:[12,96] height:24 width:138
		button btnBrowseMPQ "Browse MPQ / CASC" pos:[12,124] height:24 width:138 tooltip:"Browse models from MPQ/CASC archives"

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
			local f = getOpenFileName caption:"Select MDX Model" types:"MDX Files (*.mdx)|*.mdx|MDL Files (*.mdl)|*.mdl|All Files (*.*)|*.*|" historyCategory:"AttachPointModel"
			if f != undefined then
			(
				mdxFile.text = filenameFromPath f
				if mdxPrefix.text == "" do mdxPrefix.text = getFilenamePath f
				updateModelPath()
			)
			mdxFile.enabled = true
		)

		on btnBrowseMPQ pressed do
		(
			try
			(
				-- Use NeoDex archive browser if available
				local result = undefined
				try
				(
					::NeoDexTexBrowserTarget = #(this, "attachment_model")
					::NeoDexTexBrowserInitCategory = 0
					::NeoDexTexBrowser.show()
				) catch ( messageBox "Archive Browser not available." )
			) catch ()
		)

		on modelMapParams open do
		(
			splitModelPath()

			-- Enable/disable browse button
			local browserAvailable = false
			try
			(
				if ::NeoDexMPQ != undefined and ::NeoDexMPQ.getDirectory() != "" and ::NeoDexMPQ.validateDirectory() then
					browserAvailable = true
				if not browserAvailable then
				(
					local cascIni = getDir #plugcfg + "\\NeoDex_Settings.ini"
					local cascDir = getINISetting cascIni "CASC" "W3Path"
					if cascDir != "" and doesFileExist (cascDir + "\\.build.info") then
						browserAvailable = true
				)
			) catch()
			btnBrowseMPQ.enabled = browserAvailable
		)
	)
	rollout params "Attachment Parameters"
	(
		group "Basic Parameters"
		(
			label        first_Str  "Type:"        align:#left     width:138   pos:[12,26]
			dropDownList first                     align:#center   width:138   pos:[12,44]     items:attachmentList
			label        add1_Str   "Direction:"   align:#left     width:138   pos:[12,71]
			dropDownList add1                      align:#center   width:138   pos:[12,89]     items:attachmentAdd1List
			label        add2_Str   "Additional:"  align:#left     width:138   pos:[12,117]
			dropDownList add2                      align:#center   width:138   pos:[12,134]    items:attachmentAdd2List
			label        hhf1                      height:0        offset:[999,-1]
		)
		group "Extended Parameters 1"
		(
			editText    edt2                                           pos:[9999,0]     height:0   width:0
			label       B1          "Use External Model ............"  align:#left      pos:[12,192]                 width:124
			checkbox    chk1                                                           pos:[137,192]   height:16   width:16
			label       B2          "Visibility .........."                           pos:[12,216]                 width:85
			spinner     Ath_Visibility                                 align:#left     pos:[94,214]    height:16   fieldwidth:45   range:[0.0,1.0,0.00]    type:#float
			button      btnInfo     "Use Tips"                                        pos:[12,238]     height:24    width:138
			label       hhf2        ""                                                 height:0
		)
		group "Extended Parameters 2"
		(
			label       B3          "ID ................."           align:#left     pos:[12,300]
			spinner     spn2        fieldwidth:45   height:16       align:#left     pos:[94,300]    range:[0,100,0] type:#integer
			label       B4          "Use Attachment ID ..............."  align:#left      pos:[12,323]
			checkbox    chk2                                                        pos:[137,323]   height:16   width:16
			label       hhf3                        height:0                       offset:[999,-2]
		)
		on btnInfo pressed do
		(
			messageBox ("Please note that the model path is prefixed with the Warcraft root directory, which means that for example, if my model is placed in the textures folder under the Warcraft root directory, then my path would be Textures\\Model.mdx    ") beep: false
		)
		on params open do
		(
			if ::L != undefined then
			(
				params.title = ::L.t "attach_attachment_parameters_title"
				first_Str.text = ::L.t "attach_type_lbl"
				add1_Str.text = ::L.t "attach_direction_lbl"
				add2_Str.text = ::L.t "attach_additional_lbl"
				B1.text = ::L.t "attach_use_external_model_lbl"
				B2.text = ::L.t "attach_visibility_lbl"
				btnInfo.text = ::L.t "attach_use_tips_btn"
				B3.text = ::L.t "attach_id_lbl"
				B4.text = ::L.t "attach_use_attachment_id_lbl"
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