plugin Helper Wdx_Wc3CollisionSphere
name:"Wc3 CollisionSphere" 
classID:#(0x7A1B2C02, 0x3D4E5F02)
category:"Warcraft III Plugins" 
extends:dummy 
(
	local lastRadius, meshObj
	parameters pblock rollout:params1
	(
		radius type:#float animatable:false ui:amount default:1.0
	)
	parameters pblock rollout:General
    (
        adsorption  type:#string ui:Temp_adsorption 
    )
    rollout General "General Features"
    (
        PickButton btn_adsorption "Select Object (Adsorb)" pos:[12,7] width:138 height:24
        edittext Temp_adsorption "" pos:[9999,0] width:0 height:0
        on btn_adsorption picked obj do
        (
            try
            (
                undo "Adsorb to Object" on
                (
                    selection[1].pos = obj.pos
                    selection[1].parent = obj
                    Temp_adsorption.text = obj.name
                    btn_adsorption.text = (::WdxL.t "common_parent_prefix") + Temp_adsorption.text
                )
            )catch()
        )
        on General open do 
        (
            if Temp_adsorption.text != "" then
                btn_adsorption.text = (::WdxL.t "common_parent_prefix") + Temp_adsorption.text
            else
                btn_adsorption.text = ::WdxL.t "common_adsorb_btn"
            if ::WdxL != undefined then
                General.title = ::WdxL.t "col_general_features_title"
        )
    )
	rollout params1 "Collision Sphere Parameters"
	(
		label amount_Str "Radius ............"	width:85 pos:[12,8]	 align:#left
		Spinner amount								pos:[94,8]	align:#left		fieldwidth:45	range:[0, 1e9, 30]
		on params1 open do
		(
			if ::WdxL != undefined then
			(
				params1.title = ::WdxL.t "col_collision_sphere_parameters_title"
				amount_Str.text = ::WdxL.t "col_radius_lbl"
			)
		)
	)
	-- Only the finished mesh is kept. Keeping the Sphere made by
	-- createInstance alive (as before) made the Set Keys button flicker for
	-- as long as the shape was drawn in a viewport; the temporary object is
	-- let go right after its mesh is copied.
	on getDisplayMesh do
	(
		if meshObj == undefined or radius != lastRadius do
		(
			local s = createInstance sphere radius:radius
			meshObj = copy s.mesh
			s = undefined
			lastRadius = radius
		)
		meshObj
	)
	tool create 
	( 
		on mousePoint click do 
		(
			case click of
			(
				1: coordsys grid (nodeTM.translation = gridPoint)
			)
		)
		on mouseMove click do
		(
			case click of
			(
				2: radius = abs(gridDist.y)
				3: 
				(
					for i in objects do (if (classof i == Wdx_Wc3CollisionSphere) do (i.wirecolor = color 40 126 255))
					#stop
				)

			)
		)
	)
)

plugin Helper Wdx_CollisionBox
name:"Wc3 CollisionBox" 
classID:#(0x7A1B2C03, 0x3D4E5F03)
category:"Warcraft III Plugins" 
extends:dummy 
(
	local lastLength, lastHeight, lastWidth, meshObj
	parameters pblock rollout:params1
	(
		width	type:#float animatable:false ui:amount1 default:1.0
		length	type:#float animatable:false ui:amount2 default:1.0
		height	type:#float animatable:false ui:amount3 default:1.0
	)
	parameters pblock rollout:General
    (
        adsorption  type:#string ui:Temp_adsorption
    )
    rollout General "General Features"
    (
        PickButton btn_adsorption "Select Object (Adsorb)" pos:[12,7] width:138 height:24
        edittext Temp_adsorption "" pos:[9999,0] width:0 height:0
        on btn_adsorption picked obj do
        (
			try
			(
                undo "Adsorb to Object" on
                (
                    selection[1].pos = obj.pos
                    selection[1].parent = obj
                    Temp_adsorption.text = obj.name
                    btn_adsorption.text = (::WdxL.t "common_parent_prefix") + Temp_adsorption.text
                )
			)catch()
        )
        on General open do 
        (
            if Temp_adsorption.text != "" then
                btn_adsorption.text = (::WdxL.t "common_parent_prefix") + Temp_adsorption.text
            else
                btn_adsorption.text = ::WdxL.t "common_adsorb_btn"
            if ::WdxL != undefined then
                General.title = ::WdxL.t "col_general_features_title_2"
        )
    )
	rollout params1 "Collision Box Parameters"
	(
		label 	amount1_Str "Length ..........."	width:85 pos:[12,8]		 align:#left
		Spinner amount1 								pos:[94,8]		align:#left		fieldwidth:45	range:[0, 1e9, 30]
		label 	amount2_Str "Width ............"	width:85 pos:[12,29]	 align:#left
		Spinner amount2 								pos:[94,29]		align:#left		fieldwidth:45	range:[0, 1e9, 30]
		label 	amount3_Str "Height ..........."	width:85 pos:[12,50]	 align:#left
		Spinner amount3 								pos:[94,50]		align:#left		fieldwidth:45	range:[0, 1e9, 30]
		on params1 open do
		(
			if ::WdxL != undefined then
			(
				params1.title = ::WdxL.t "col_collision_box_parameters_title"
				amount1_Str.text = ::WdxL.t "col_length_lbl"
				amount2_Str.text = ::WdxL.t "col_width_lbl"
				amount3_Str.text = ::WdxL.t "col_height_lbl"
			)
		)
	)
	-- Only the finished mesh is kept (see Wdx_Wc3CollisionSphere).
	on getDisplayMesh do
	(
		if meshObj == undefined or length != lastLength or width != lastWidth or height != lastHeight do
		(
			local b = createInstance box length:length width:width height:height
			meshObj = copy b.mesh
			b = undefined
			lastLength	= length
			lastWidth	= width
			lastHeight	= height
		)
		meshObj
	)
	tool create 
	( 
		on mousePoint click do 
		(
			case click of
			(
				1: 
				(
					coordsys grid (nodeTM.translation = gridPoint)
				)
			)
		)
		on mouseMove click do
		(
			case click of
			(
				2: 
				(
					length = abs(gridDist.y)
					width = abs(gridDist.x)
				)
				3: (height = abs(gridDist.z))
				4: 
				(
					for i in objects do (if (classof i == Wdx_CollisionBox) do (i.wirecolor = color 40 126 255))
					#stop
				)
			)
		)
	)
)