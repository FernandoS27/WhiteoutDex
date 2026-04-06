struct TeamColorManager
(
    isPoppedOut = false,
    savedIndex = undefined,
    toolName = "Team Color Manager",
   
    mainRollout = rollout mainRollout "Team Color Manager"
    (
        local self
        button popOutBtn "Undock" pos:[379, 2] width:56 height:18 tooltip:"Undock / Dock"
       
        -- Adding a GroupBox around the controls
        groupBox teamColorGrp "Team Color Settings" pos:[8,22] width:428 height:42
       
        -- Adjusted positions of controls to fit inside the GroupBox
        spinner teamColorSpn "Team Color:" pos:[16,38] width:48 range:[1,16,1] type:#integer scale:1 align:#left
        colorPicker colorCPck pos:[116, 36] width:96 height:22
        button changeColorBtn "Change Color" pos:[232, 36] width:196 height:22
       
        on teamColorSpn changed val do
        (
            if WhiteoutDexWc3MaterialTeamColors == undefined then return false
            colorCPck.color = WhiteoutDexWc3MaterialTeamColors[val] -- This global is declared in Wc3Material.ms
        )
        on changeColorBtn pressed do
        (
            if WhiteoutDexUpdateTeamColor == undefined then return false
            WhiteoutDexUpdateTeamColor (teamColorSpn.value as integer) -- This function is declared in Wc3Material.ms
        )
       
        -- Pop Out / Dock
        on popOutBtn pressed do
        (
            if ::TeamColorManager.isPoppedOut then
            (
                ::TeamColorManager.isPoppedOut = false
                try ( destroyDialog mainRollout ) catch()
                
                if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
                (
                    local rollouts = ::whiteoutdexManagerFloater.rollouts
                    local idx = ::TeamColorManager.savedIndex
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
                    if rollouts[i] == mainRollout then ( ::TeamColorManager.savedIndex = i; exit )
                removeRollout mainRollout ::whiteoutdexManagerFloater
            )
            
            ::TeamColorManager.isPoppedOut = true
            popOutBtn.text = ::L.t "common_dock_btn"
            createDialog mainRollout width:440 height:82 style:#(#style_titlebar, #style_sysmenu, #style_minimizebox, #style_resizing)
        )
        
        on mainRollout close do
        (
            if ::TeamColorManager.isPoppedOut then
            (
                ::TeamColorManager.isPoppedOut = false
                local tmr = dotNetObject "System.Windows.Forms.Timer"
                tmr.Interval = 1
                dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
                (
                    s.Stop(); s.Dispose()
                    try
                    (
                        if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
                        (
                            local rl = ::TeamColorManager.mainRollout
                            local idx = ::TeamColorManager.savedIndex
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
            self = TeamColorManager
            
            if ::TeamColorManager.isPoppedOut then
                popOutBtn.text = ::L.t "common_dock_btn"
            else
                popOutBtn.text = ::L.t "common_undock_btn"
            popOutBtn.tooltip = ::L.t "common_undock_dock_tip"
            
            -- Localization
            if ::L != undefined then
            (
                mainRollout.title = ::L.t "tc_team_color_manager_title"
                teamColorGrp.text = ::L.t "tc_team_color_settings_grp"
                teamColorSpn.caption = ::L.t "tc_team_color_spn"
                changeColorBtn.text = ::L.t "tc_change_color_btn"
            )
        )
    ),
   
    on create do
    (
    )
)
global TeamColorManager = TeamColorManager()
