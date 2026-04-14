struct WdxTeamColorManager
(
    isPoppedOut = false,
    savedIndex = undefined,
    toolName = "Team Color Manager",
   
    mainRollout = rollout mainRollout "Team Color Manager"
    (
        local self
       
        -- Adding a GroupBox around the controls
        groupBox teamColorGrp "Team Color Settings" pos:[8,22] width:428 height:42
       
        -- Adjusted positions of controls to fit inside the GroupBox
        spinner teamColorSpn "Team Color:" pos:[16,38] width:48 range:[1,16,1] type:#integer scale:1 align:#left
        colorPicker colorCPck pos:[116, 36] width:96 height:22
        button changeColorBtn "Change Color" pos:[232, 36] width:196 height:22
       
        on teamColorSpn changed val do
        (
            if WhiteoutDexWc3MaterialTeamColors == undefined then return false
            colorCPck.color = WhiteoutDexWc3MaterialTeamColors[val] -- This global is declared in WdxMaterialData.ms
        )
        on changeColorBtn pressed do
        (
            if WhiteoutDexUpdateTeamColor == undefined then return false
            WhiteoutDexUpdateTeamColor (teamColorSpn.value as integer) -- This function is declared in WdxMaterialData.ms
        )
       
        -- Pop Out / Dock
        on popOutBtn pressed do
        (
            if ::WdxTeamColorManager.isPoppedOut then
            (
                ::WdxTeamColorManager.isPoppedOut = false
                try ( destroyDialog mainRollout ) catch()
                
                if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
                (
                    local rollouts = ::whiteoutdexManagerFloater.rollouts
                    local idx = ::WdxTeamColorManager.savedIndex
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
                    if rollouts[i] == mainRollout then ( ::WdxTeamColorManager.savedIndex = i; exit )
                removeRollout mainRollout ::whiteoutdexManagerFloater
            )
            
            ::WdxTeamColorManager.isPoppedOut = true
            createDialog mainRollout width:440 height:82 style:#(#style_titlebar, #style_sysmenu, #style_minimizebox, #style_resizing)
        )
        
        on mainRollout close do
        (
            if ::WdxTeamColorManager.isPoppedOut then
            (
                ::WdxTeamColorManager.isPoppedOut = false
                local tmr = dotNetObject "System.Windows.Forms.Timer"
                tmr.Interval = 1
                dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
                (
                    s.Stop(); s.Dispose()
                    try
                    (
                        if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
                        (
                            local rl = ::WdxTeamColorManager.mainRollout
                            local idx = ::WdxTeamColorManager.savedIndex
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
            self = WdxTeamColorManager
            -- Localization
            if ::WdxL != undefined then
            (
                mainRollout.title = ::WdxL.t "tc_team_color_manager_title"
                teamColorGrp.text = ::WdxL.t "tc_team_color_settings_grp"
                teamColorSpn.caption = ::WdxL.t "tc_team_color_spn"
                changeColorBtn.text = ::WdxL.t "tc_change_color_btn"
            )
        )
    ),
   
    on create do
    (
    )
)
global WdxTeamColorManager = WdxTeamColorManager()
