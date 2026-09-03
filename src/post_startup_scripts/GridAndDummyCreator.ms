struct WdxGridAndDummyCreator
(
    isPoppedOut = false,
    savedIndex = undefined,
    toolName,
    gridColor = color 255 255 0,
    gridSize = 10.0,
    pointsColor = color 255 255 0,
    pointsSize = 10.0,
    lastCreatedGrid,
    lastCreatedPoints,
   
    mainRollout = rollout mainRollout "Grid and Dummy Creator"
    (
        local self
        
        GroupBox gridGrp "Grid" pos:[8,30] width:210 height:94 tooltip:"Settings for grid creation"
        colorpicker gridCPck "Color:" pos:[16,46] width:194 height:22 align:#left tooltip:"Set the color of the created grid"
        spinner gridSizeSpn "Size:" pos:[16,74] width:68 range:[0.001,999999,10] type:#worldunits align:#left tooltip:"Set the size of the grid in world units"
        button createGridBtn "Create Grid" pos:[16,96] width:194 height:22 align:#left tooltip:"Create a new grid with current settings at world origin [0,0,0]"
        
        GroupBox pointsGrp "Point(s)" pos:[224, 30] width:210 height:94 tooltip:"Settings for point/dummy creation"
        colorpicker pointsCPck "Color:" pos:[232,46] width:194 height:22 align:#left tooltip:"Set the color of the created point objects"
        spinner pointsSizeSpn "Size:" pos:[232,74] width:68 range:[0.001,999999,10] type:#worldunits align:#left tooltip:"Set the size of point objects in world units"
        button createPointsBtn "Create Point(s)" pos:[232,96] width:194 height:22 align:#left tooltip:"Create point objects at selected objects' positions or at world origin if nothing is selected"
        
        on gridCPck changed clr do self.gridColor = clr
        on gridSizeSpn changed size do self.gridSize = size
           
        on pointsCPck changed clr do self.pointsColor = clr
        on pointsSizeSpn changed size do self.pointsSize = size
           
        on createGridBtn pressed do self.createGrid()
        on createPointsBtn pressed do self.createPointsFromSelectedObjects()
       
		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::WdxGridAndDummyCreator.isPoppedOut then
			(
				::WdxGridAndDummyCreator.isPoppedOut = false
				try ( destroyDialog mainRollout ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxGridAndDummyCreator.savedIndex
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
					if rollouts[i] == mainRollout then ( ::WdxGridAndDummyCreator.savedIndex = i; exit )
				removeRollout mainRollout ::whiteoutdexManagerFloater
			)
			
			::WdxGridAndDummyCreator.isPoppedOut = true
			createDialog mainRollout width:440 height:142 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
		)
		
		on mainRollout close do
		(
			if ::WdxGridAndDummyCreator.isPoppedOut then
			(
				::WdxGridAndDummyCreator.isPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxGridAndDummyCreator.mainRollout
							local idx = ::WdxGridAndDummyCreator.savedIndex
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
		)
       
        on mainRollout open do
        (
            -- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
            try (WdxSetDialogIcon mainRollout) catch()
            self = WdxGridAndDummyCreator
            gridCPck.color = self.gridColor
            gridSizeSpn.value = self.gridSize
            pointsCPck.color = self.pointsColor
            pointsSizeSpn.value = self.pointsSize

            -- Localization
            if ::WdxL != undefined then
            (
                mainRollout.title = ::WdxL.t "grid_grid_and_dummy_creator_title"
                gridGrp.text = ::WdxL.t "grid_grid_grp"
                try (gridGrp.tooltip = ::WdxL.t "grid_settings_for_grid_creation_tip") catch ()
                pointsGrp.text = ::WdxL.t "grid_points_grp"
                try (pointsGrp.tooltip = ::WdxL.t "grid_settings_for_pointdummy_creat_tip") catch ()
                gridCPck.caption = ::WdxL.t "grid_color_cpk"
                try (gridCPck.tooltip = ::WdxL.t "grid_set_the_color_of_the_created_g_tip") catch ()
                gridSizeSpn.caption = ::WdxL.t "grid_size_spn"
                try (gridSizeSpn.tooltip = ::WdxL.t "grid_set_the_size_of_the_grid_in_wo_tip") catch ()
                createGridBtn.text = ::WdxL.t "grid_create_grid_btn"
                try (createGridBtn.tooltip = ::WdxL.t "grid_create_a_new_grid_with_current_tip") catch ()
                pointsCPck.caption = ::WdxL.t "grid_color_cpk_2"
                try (pointsCPck.tooltip = ::WdxL.t "grid_set_the_color_of_the_created_p_tip") catch ()
                pointsSizeSpn.caption = ::WdxL.t "grid_size_spn_2"
                try (pointsSizeSpn.tooltip = ::WdxL.t "grid_set_the_size_of_point_objects_tip") catch ()
                createPointsBtn.text = ::WdxL.t "grid_create_points_btn"
                try (createPointsBtn.tooltip = ::WdxL.t "grid_create_point_objects_at_select_tip") catch ()
            )
        )
    ),
   
    fn createGrid =
    (
        theHold.Begin()
       
        local newGrid = grid width:this.gridSize length:this.gridSize grid_space:10 pos:[0,0,0]
        newGrid.wirecolor = this.gridColor
       
        newGrid.position.controller = linear_position()
        newGrid.rotation.controller = linear_rotation()
        newGrid.scale.controller = linear_scale()
       
        theHold.Accept("Create Grid")
        redrawViews()
       
        this.lastCreatedGrid = newGrid
       
        return newGrid
    ),
   
    fn createPoint xform: =
    (
        theHold.Begin()
        
        local newPoint = Point size:this.pointsSize pos:[0,0,0] centermarker:true box:true
        newPoint.wirecolor = this.pointsColor
    
        newPoint.position.controller = linear_position()
        newPoint.rotation.controller = linear_rotation()
        newPoint.scale.controller = linear_scale()
        
        if xform != unsupplied and xform != undefined then
            newPoint.transform = xform
    
        theHold.Accept("Create Point")
        
        return newPoint
    ),
    
    fn createPointsFromSelectedObjects =
    (
        local createdPoints = #()
        local selArray = selection as array
        
        local savedTime = sliderTime
        
        theHold.Begin()
        disableSceneRedraw()

        if selArray.count > 0 then
        (
            sliderTime = 0f
            
            for selectedObj in selArray where isValidNode selectedObj do
            (
                local objParent = selectedObj.parent
                local newPoint = this.createPoint()
                
                if objParent != undefined then
                (
                    newPoint.transform = objParent.transform
                    newPoint.pos = selectedObj.pos
                    newPoint.parent = objParent
                )
                else
                (
                    newPoint.pos = selectedObj.pos
                )
                
                selectedObj.parent = newPoint
                append createdPoints newPoint
            )
            
            sliderTime = savedTime
        )
        else
        (
            local newPoint = this.createPoint()
            append createdPoints newPoint
        )

        select createdPoints
        
        enableSceneRedraw()
        theHold.Accept("Create (Multiple) Points")
        redrawViews()

        this.lastCreatedPoints = createdPoints
        return createdPoints
    ),
   
    fn showUI =
    (
        if not this.mainRollout.isDisplayed then CreateDialog this.mainRollout width:456
    ),
   
    fn hideUI =
    (
        if this.mainRollout.inDialog then DestroyDialog this.mainRollout
    ),
   
    fn toggleUI =
    (
        if this.mainRollout.inDialog then this.hideUI() else this.showUI()
    ),
   
    on create do
    (
        this.toolName = "Grid and Dummy Creator"
    )
)
global WdxGridAndDummyCreator = WdxGridAndDummyCreator()
