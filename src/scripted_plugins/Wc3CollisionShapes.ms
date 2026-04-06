plugin Helper Wc3CollisionSphere
name:"CollisionSphere" 
classID:#(0xb945647, 0x61c1bb70)
category:"Warcraft 3 Systems" 
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
                    btn_adsorption.text = "Parent: " + Temp_adsorption.text
                )
            )catch()
        )
        on General open do 
        (
            if Temp_adsorption.text != "" then
                btn_adsorption.text = "Parent: " + Temp_adsorption.text
            else
                btn_adsorption.text = "Select Object (Adsorb)"
            if ::L != undefined then
                General.title = ::L.t "col_general_features_title"
        )
    )
	rollout params1 "Collision Sphere Parameters"
	(
		label amount_Str "Radius ............"	width:85 pos:[12,8]	 align:#left
		Spinner amount								pos:[94,8]	align:#left		fieldwidth:45	range:[0, 1e9, 30]
		on params1 open do
		(
			if ::L != undefined then
			(
				params1.title = ::L.t "col_collision_sphere_parameters_title"
				amount_Str.text = ::L.t "col_radius_lbl"
			)
		)
	)
	on getDisplayMesh do
	(
		if (meshObj == undefined) do 
		(
			meshObj = createInstance sphere radius:radius
			lastRadius = radius
		)
		if radius != lastRadius do 
		(
			meshObj.radius = radius
			lastRadius = radius
		)
		meshObj.mesh
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
					for i in objects do (if (classof i == Wc3_CollisionSphere) do (i.wirecolor = color 40 126 255))
					#stop
				)

			)
		)
	)
)

plugin Helper Wc3CollisionBox
name:"CollisionBox" 
classID:#(0x51e76dc0, 0x471f81a5)
category:"Warcraft 3 Systems" 
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
                    btn_adsorption.text = "Parent: " + Temp_adsorption.text
                )
			)catch()
        )
        on General open do 
        (
            if Temp_adsorption.text != "" then
                btn_adsorption.text = "Parent: " + Temp_adsorption.text
            else
                btn_adsorption.text = "Select Object (Adsorb)"
            if ::L != undefined then
                General.title = ::L.t "col_general_features_title_2"
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
			if ::L != undefined then
			(
				params1.title = ::L.t "col_collision_box_parameters_title"
				amount1_Str.text = ::L.t "col_length_lbl"
				amount2_Str.text = ::L.t "col_width_lbl"
				amount3_Str.text = ::L.t "col_height_lbl"
			)
		)
	)
	on getDisplayMesh do
	(
		if (meshObj == undefined) do 
		(
			meshObj = createInstance box length:length \
			width:width	height:height
			lastLength	= length
			lastWidth	= width
			lastHeight	= height
		)
		if length != lastLength do 
		(
			meshObj.length = length
			lastLength = length
		)
		if width != lastWidth do 
		(
			meshObj.width = width
			lastWidth = width
		)
		if height != lastHeight do 
		(
			meshObj.height = height
			lastHeight = height
		)
		meshObj.mesh
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
					for i in objects do (if (classof i == Wc3_CollisionBox) do (i.wirecolor = color 40 126 255))
					#stop
				)
			)
		)
	)
)