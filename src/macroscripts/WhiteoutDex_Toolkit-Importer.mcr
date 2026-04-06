macroScript WhiteoutDex_Importer
buttonText:"WhiteoutDex Importer"
category:"WhiteoutDex Toolkit"
internalCategory:"WhiteoutDex Toolkit" 
(
	/*
		WhiteoutDex Importer - UI Only
		=========================
		All import logic is in WhiteoutDexImportFunctions.ms
		This file only contains the UI rollouts and event handlers.
	*/

	local whiteoutDexIPVersion = "1.0"
	local mode
	local modelpath
	local onControlSet = true
	local mdxVersion = 800

    rollout importerRoll "WhiteoutDex Importer" width:410 height:598
    (
        button importBtn "Import" pos:[149,566] width:58 height:22 tooltip:"Start the import process with current settings"
        GroupBox geometryGrp "Geometry" pos:[14,9] width:104 height:51
        checkbox skinnedChk "Skinned" pos:[30,29] width:70 height:16 enabled:false checked:true tooltip:"Import skinning data (vertex weights and bone assignments)"
        GroupBox objectsGrp "Objects" pos:[11,134] width:236 height:179
        GroupBox materialsGrp "Materials" pos:[13,68] width:114 height:63
        checkbox importTexturesChk "Import Textures" pos:[19,109] width:94 height:17 enabled:false checked:true tooltip:"Import and assign texture maps to materials"
        checkbox importObjectsChk "Import Objects" pos:[22,150] width:125 height:18 enabled:false checked:true tooltip:"Import all scene objects (master switch)"
        checkbox bonesChk "Bones" pos:[29,185] width:94 height:17 enabled:false checked:true tooltip:"Import skeletal bones for animation and skinning"
        checkbox helpersChk "Helpers" pos:[29,206] width:77 height:18 enabled:false checked:true tooltip:"Import helper objects for positioning and animation"
        checkbox lightsChk "Lights" pos:[29,228] width:60 height:17 enabled:false checked:true tooltip:"Import light sources from the model"
        checkbox attachmentsChk "Attachments" pos:[29,251] width:82 height:17 enabled:false checked:true tooltip:"Import attachment points (for weapons, effects, etc.)"
        checkbox particleEmittersOneChk "Particle Emitters 1" pos:[127,185] width:107 height:15 enabled:false checked:true tooltip:"Import MDL-style particle emitters"
        checkbox particleEmittersTwoChk "Particle Emitters 2" pos:[127,206] width:105 height:17 enabled:false checked:true tooltip:"Import advanced particle emitter systems"
        checkbox eventObjectsChk "Event Objects" pos:[29,273] width:87 height:16 enabled:false checked:true tooltip:"Import event objects (for sounds, effects triggers)"
        checkbox ribbonEmittersChk "Ribbon Emitters" pos:[127,228] width:105 height:15 enabled:false checked:true tooltip:"Import ribbon trail emitter systems"
        checkbox collisionShapesChk "Collision Shapes" pos:[127,251] width:99 height:16 enabled:false checked:true tooltip:"Import collision geometry (boxes and spheres)"
        GroupBox animationsGrp "Animations" pos:[136,10] width:257 height:124
        checkbox importAnimationsChk "Import Animations" pos:[144,26] width:108 height:19 enabled:false checked:true tooltip:"Import all animation data (master switch)"
        checkbox rotationsChk "Rotations" pos:[159,54] width:74 height:15 enabled:false checked:true tooltip:"Import rotation animation tracks"
        checkbox translationChk "Translation" pos:[159,73] width:80 height:16 enabled:false checked:true tooltip:"Import position/movement animation tracks"
        checkbox scaleChk "Scale" pos:[159,93] width:79 height:15 enabled:false checked:true tooltip:"Import scaling animation tracks"
        checkbox parameterChk "Parameter" pos:[249,54] width:89 height:17 enabled:false checked:true tooltip:"Import parameter animations (intensity, attenuation, etc.)"
        checkbox unwrapAnimationsChk "Unwrap Animations" pos:[249,73] width:114 height:17 enabled:false checked:true tooltip:"Import UV coordinate animations"
        checkbox textureAnimationsCk "Texture Animations" pos:[249,93] width:113 height:14 enabled:false checked:true tooltip:"Import texture switching animations"
        checkbox visibilityChk "Visibility" pos:[159,111] width:62 height:15 enabled:false checked:true tooltip:"Import visibility/transparency animations"
        checkbox importMaterialsChk "Import Materials" pos:[19,87] width:93 height:17 enabled:false checked:true tooltip:"Import material definitions and properties"
        GroupBox settingsGrp "Settings" pos:[251,134] width:145 height:180
        radiobuttons fastSettingsRdo "Fast Settings" pos:[262,149] width:128 height:110 enabled:true labels:#("Custom", "Static No Materials", "Static Materials", "Animated No Skinning", "Animated No objects", "All") default:6 columns:1 tooltip:"Quick presets for common import configurations"
        radiobuttons modeRdo "Mode:" pos:[264,264] width:79 height:46 enabled:false labels:#("New Scene", "Merge") columns:1 tooltip:"Choose whether to replace current scene or merge with it"
        checkbox colorChk "Color" pos:[248,111] width:127 height:15 enabled:false checked:true tooltip:"Import color/tinting animations"
        checkbox camerasChk "Cameras" pos:[127,273] width:96 height:13 enabled:false checked:true tooltip:"Import camera objects and their animations"
        
        -- Helper Import Options GroupBox
        GroupBox helperOptionsGrp "Helper Options" pos:[10,320] width:381 height:45
        checkbox importHelpersAsPointHelpersChk "Import Helpers as Point Helpers" pos:[25,340] width:250 height:16 checked:true tooltip:"Import helpers as Point objects. If disabled, helpers will be imported as bones."
        
        groupBox optimizerGrp "Optimizer" pos:[10,372] width:381 height:68
        checkbox optimizeGeometryChk "Optimize Geometry" pos:[25,392] width:274 height:16 checked:true tooltip:"Combine nearby vertices and simplify the mesh structure"
        checkbox optimizeBonesAndHelpersChk "Optimize Bones and Helpers" pos:[26,414] width:256 height:14 checked:true tooltip:"Remove unnecessary bones and helpers without animations"
        
        groupBox mpqGrp "MPQ Textures" pos:[10,445] width:381 height:40
        checkbox searchMPQChk "Search MPQ Archives for Textures" pos:[25,462] width:280 height:16 checked:false tooltip:"Search Warcraft III MPQ archives for textures not found locally. Set the MPQ directory in WhiteoutDex Settings first."
        
        GroupBox progressGrp "Progress" pos:[10,497] width:382 height:61
        progressBar progressBarPbar "ProgressBar" pos:[32,538] width:355 height:11 color:(color 30 10 190) tooltip:"Shows import progress"
        label idleLbl "Idle..." pos:[35,514] width:158 height:17 tooltip:"Displays current import status"
        
        -- INI Datei Pfad
        local iniFile = (getDir #plugcfg + "\\WhiteoutDexImporter.ini")
        
        -- Funktion zum Speichern der Einstellungen
        fn saveSettings =
        (
            setINISetting iniFile "Geometry" "Skinned" (skinnedChk.checked as string)
            
            setINISetting iniFile "Materials" "ImportMaterials" (importMaterialsChk.checked as string)
            setINISetting iniFile "Materials" "ImportTextures" (importTexturesChk.checked as string)
            
            setINISetting iniFile "Objects" "ImportObjects" (importObjectsChk.checked as string)
            setINISetting iniFile "Objects" "Bones" (bonesChk.checked as string)
            setINISetting iniFile "Objects" "Helpers" (helpersChk.checked as string)
            setINISetting iniFile "Objects" "Lights" (lightsChk.checked as string)
            setINISetting iniFile "Objects" "Attachments" (attachmentsChk.checked as string)
            setINISetting iniFile "Objects" "ParticleEmitters1" (particleEmittersOneChk.checked as string)
            setINISetting iniFile "Objects" "ParticleEmitters2" (particleEmittersTwoChk.checked as string)
            setINISetting iniFile "Objects" "EventObjects" (eventObjectsChk.checked as string)
            setINISetting iniFile "Objects" "RibbonEmitters" (ribbonEmittersChk.checked as string)
            setINISetting iniFile "Objects" "CollisionShapes" (collisionShapesChk.checked as string)
            setINISetting iniFile "Objects" "Cameras" (camerasChk.checked as string)
            
            setINISetting iniFile "Animations" "ImportAnimations" (importAnimationsChk.checked as string)
            setINISetting iniFile "Animations" "Rotations" (rotationsChk.checked as string)
            setINISetting iniFile "Animations" "Translation" (translationChk.checked as string)
            setINISetting iniFile "Animations" "Scale" (scaleChk.checked as string)
            setINISetting iniFile "Animations" "Parameter" (parameterChk.checked as string)
            setINISetting iniFile "Animations" "UnwrapAnimations" (unwrapAnimationsChk.checked as string)
            setINISetting iniFile "Animations" "TextureAnimations" (textureAnimationsCk.checked as string)
            setINISetting iniFile "Animations" "Visibility" (visibilityChk.checked as string)
            setINISetting iniFile "Animations" "Color" (colorChk.checked as string)
            
            setINISetting iniFile "Settings" "FastSettings" (fastSettingsRdo.state as string)
            
            setINISetting iniFile "HelperOptions" "ImportHelpersAsPointHelpers" (importHelpersAsPointHelpersChk.checked as string)
            
            setINISetting iniFile "Optimizer" "OptimizeGeometry" (optimizeGeometryChk.checked as string)
            setINISetting iniFile "Optimizer" "OptimizeBonesAndHelpers" (optimizeBonesAndHelpersChk.checked as string)
            setINISetting iniFile "MPQ" "SearchMPQ" (searchMPQChk.checked as string)
        )
        
        -- Funktion zum Laden der Einstellungen
        fn loadSettings =
        (
            local val
            
            val = getINISetting iniFile "Geometry" "Skinned"
            if val != "" then skinnedChk.checked = (val as booleanClass)
            
            val = getINISetting iniFile "Materials" "ImportMaterials"
            if val != "" then importMaterialsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Materials" "ImportTextures"
            if val != "" then importTexturesChk.checked = (val as booleanClass)
            
            val = getINISetting iniFile "Objects" "ImportObjects"
            if val != "" then importObjectsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Bones"
            if val != "" then bonesChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Helpers"
            if val != "" then helpersChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Lights"
            if val != "" then lightsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Attachments"
            if val != "" then attachmentsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "ParticleEmitters1"
            if val != "" then particleEmittersOneChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "ParticleEmitters2"
            if val != "" then particleEmittersTwoChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "EventObjects"
            if val != "" then eventObjectsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "RibbonEmitters"
            if val != "" then ribbonEmittersChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "CollisionShapes"
            if val != "" then collisionShapesChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Cameras"
            if val != "" then camerasChk.checked = (val as booleanClass)
            
            val = getINISetting iniFile "Animations" "ImportAnimations"
            if val != "" then importAnimationsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Rotations"
            if val != "" then rotationsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Translation"
            if val != "" then translationChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Scale"
            if val != "" then scaleChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Parameter"
            if val != "" then parameterChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "UnwrapAnimations"
            if val != "" then unwrapAnimationsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "TextureAnimations"
            if val != "" then textureAnimationsCk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Visibility"
            if val != "" then visibilityChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Color"
            if val != "" then colorChk.checked = (val as booleanClass)
            
            val = getINISetting iniFile "Settings" "FastSettings"
            if val != "" then fastSettingsRdo.state = (val as integer)
            
            val = getINISetting iniFile "HelperOptions" "ImportHelpersAsPointHelpers"
            if val != "" then importHelpersAsPointHelpersChk.checked = (val as booleanClass)
            
            val = getINISetting iniFile "Optimizer" "OptimizeGeometry"
            if val != "" then optimizeGeometryChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Optimizer" "OptimizeBonesAndHelpers"
            if val != "" then optimizeBonesAndHelpersChk.checked = (val as booleanClass)
            
            val = getINISetting iniFile "MPQ" "SearchMPQ"
            if val != "" then searchMPQChk.checked = (val as booleanClass)
        )
        
        fn setSettings =
        (
            ::WhiteoutDexImportSettings.importMaterials = importMaterialsChk.checked
            ::WhiteoutDexImportSettings.importSkinning = skinnedChk.checked
            ::WhiteoutDexImportSettings.importAnimations = importAnimationsChk.checked
            ::WhiteoutDexImportSettings.importObjects = importObjectsChk.checked
            ::WhiteoutDexImportSettings.importBones = bonesChk.checked
            ::WhiteoutDexImportSettings.importHelpers = helpersChk.checked
            ::WhiteoutDexImportSettings.importLights = lightsChk.checked
            ::WhiteoutDexImportSettings.importAttachments = attachmentsChk.checked
            ::WhiteoutDexImportSettings.importPE1 = particleEmittersOneChk.checked
            ::WhiteoutDexImportSettings.importPE2 = particleEmittersTwoChk.checked
            ::WhiteoutDexImportSettings.importRE = ribbonEmittersChk.checked
            ::WhiteoutDexImportSettings.importEO = eventObjectsChk.checked
            ::WhiteoutDexImportSettings.importCS = collisionShapesChk.checked
            ::WhiteoutDexImportSettings.importCameras = camerasChk.checked
            ::WhiteoutDexImportSettings.importTransAnim = translationChk.checked
            ::WhiteoutDexImportSettings.importRotAnim = rotationsChk.checked
            ::WhiteoutDexImportSettings.importScaleAnim = scaleChk.checked
            ::WhiteoutDexImportSettings.importTXAnim = textureAnimationsCk.checked
            ::WhiteoutDexImportSettings.importUVAnim = unwrapAnimationsChk.checked
            ::WhiteoutDexImportSettings.importVisAnim = visibilityChk.checked
            ::WhiteoutDexImportSettings.importColorAnim = colorChk.checked
            ::WhiteoutDexImportSettings.importParamAnim = parameterChk.checked
            ::WhiteoutDexImportSettings.importHelpersAsPointHelpers = importHelpersAsPointHelpersChk.checked
            ::WhiteoutDexImportSettings.optimizeGeometry = optimizeGeometryChk.checked
            ::WhiteoutDexImportSettings.optimizeBonesHelpers = optimizeBonesAndHelpersChk.checked
            ::WhiteoutDexImportSettings.searchMPQ = searchMPQChk.checked
        )


        fn checkObjects state =
        (
            importObjectsChk.checked = state
            bonesChk.checked = state
            helpersChk.checked = state
            lightsChk.checked = state
            attachmentsChk.checked = state
            particleEmittersOneChk.checked = state
            particleEmittersTwoChk.checked = state
            ribbonEmittersChk.checked = state
            eventObjectsChk.checked = state
            collisionShapesChk.checked = state
            camerasChk.checked = state
        )

        fn checkBH state =
        (
            importObjectsChk.checked = state
            bonesChk.checked = state
            helpersChk.checked = state
        )

        fn checkSkinning state =
        (
            skinnedChk.checked = state
        )

        fn checkMaterials state =
        (
            importMaterialsChk.checked = state
            importTexturesChk.checked = state
        )

        fn checkAnimations state =
        (
            importAnimationsChk.checked = state
            rotationsChk.checked = state
            translationChk.checked = state
            scaleChk.checked = state
            parameterChk.checked = state
            unwrapAnimationsChk.checked = state
            textureAnimationsCk.checked = state
            visibilityChk.checked = state
            colorChk.checked = state
        )

        fn enableAnimations state =
        (
            importAnimationsChk.enabled = state
            rotationsChk.enabled = state
            translationChk.enabled = state
            scaleChk.enabled = state
            parameterChk.enabled = state
            unwrapAnimationsChk.enabled = state
            textureAnimationsCk.enabled = state
            visibilityChk.enabled = state
            colorChk.enabled = state
        )

        fn enableObjects state =
        (
            importObjectsChk.enabled = state
            bonesChk.enabled = state
            helpersChk.enabled = state
            lightsChk.enabled = state
            attachmentsChk.enabled = state
            particleEmittersOneChk.enabled = state
            particleEmittersTwoChk.enabled = state
            ribbonEmittersChk.enabled = state
            eventObjectsChk.enabled = state
            collisionShapesChk.enabled = state
            camerasChk.enabled = state
        )
        
        -- Event Handler für alle Checkboxen - speichert bei jeder Änderung
        on skinnedChk changed state do saveSettings()
        on importMaterialsChk changed state do saveSettings()
        on importTexturesChk changed state do saveSettings()
        on importObjectsChk changed state do 
        (
            if onControlSet then
            (
                onControlSet = false
                checkObjects state
                enableObjects state
                importObjectsChk.enabled = true
                onControlSet = true
            )
            saveSettings()
        )
        on bonesChk changed state do 
        (
            if onControlSet then
            (
                onControlSet = false
                if not state then skinnedChk.checked = false
                onControlSet = true
            )
            saveSettings()
        )
        on helpersChk changed state do saveSettings()
        on lightsChk changed state do saveSettings()
        on attachmentsChk changed state do saveSettings()
        on particleEmittersOneChk changed state do saveSettings()
        on particleEmittersTwoChk changed state do saveSettings()
        on eventObjectsChk changed state do saveSettings()
        on ribbonEmittersChk changed state do saveSettings()
        on collisionShapesChk changed state do saveSettings()
        on camerasChk changed state do saveSettings()
        on importAnimationsChk changed state do 
        (
            if onControlSet then
            (
                onControlSet = false
                checkAnimations state
                enableAnimations state
                importAnimationsChk.enabled = true
                onControlSet = true
            )
            saveSettings()
        )
        on rotationsChk changed state do saveSettings()
        on translationChk changed state do saveSettings()
        on scaleChk changed state do saveSettings()
        on parameterChk changed state do saveSettings()
        on unwrapAnimationsChk changed state do saveSettings()
        on textureAnimationsCk changed state do saveSettings()
        on visibilityChk changed state do saveSettings()
        on colorChk changed state do saveSettings()
        on importHelpersAsPointHelpersChk changed state do saveSettings()
        on optimizeGeometryChk changed state do saveSettings()
        on optimizeBonesAndHelpersChk changed state do saveSettings()
        on searchMPQChk changed state do saveSettings()
        on fastSettingsRdo changed newState do
        (
            onControlSet = false

            -- Liste der Checkboxen die durch Fast Settings gesteuert werden sollen
            local controlledCheckboxes = #(
                skinnedChk, importMaterialsChk, importTexturesChk, importObjectsChk,
                bonesChk, helpersChk, lightsChk, attachmentsChk,
                particleEmittersOneChk, particleEmittersTwoChk, eventObjectsChk,
                ribbonEmittersChk, collisionShapesChk, camerasChk,
                importAnimationsChk, rotationsChk, translationChk, scaleChk,
                parameterChk, unwrapAnimationsChk, textureAnimationsCk,
                visibilityChk, colorChk
            )

            case newState of
            (
                1: -- Custom
                (
                    for chk in controlledCheckboxes do chk.enabled = true
                )

                2: -- Static No Materials
                (
                    for chk in controlledCheckboxes do chk.enabled = false
                    checkMaterials false
                    checkSkinning false
                    checkAnimations false
                    checkObjects false
                )

                3: -- Static Materials
                (
                    for chk in controlledCheckboxes do chk.enabled = false
                    checkMaterials true
                    checkSkinning false
                    checkAnimations false
                    checkObjects false
                )

                4: -- Animated No Skinning
                (
                    for chk in controlledCheckboxes do chk.enabled = false
                    checkMaterials true
                    checkSkinning false
                    checkAnimations true
                    checkObjects false
                    checkBH true
                )

                5: -- Animated No objects
                (
                    for chk in controlledCheckboxes do chk.enabled = false
                    checkMaterials true
                    checkSkinning true
                    checkAnimations true
                    checkObjects false
                    checkBH true
                )

                6: -- All
                (
                    for chk in controlledCheckboxes do chk.enabled = false
                    checkMaterials true
                    checkSkinning true
                    checkAnimations true
                    checkObjects true
                )
            )

            if not importAnimationsChk.checked then
            (
                enableAnimations false
                importAnimationsChk.enabled = newState == 1
            )

            if not importObjectsChk.checked then
            (
                enableObjects false
                importObjectsChk.enabled = newState == 1
            )

            onControlSet = true
            saveSettings()
        )
        
        -- Beim Öffnen des Dialogs Einstellungen laden
        on importerRoll open do
        (
            loadSettings()
            
            -- MPQ Checkbox nur aktiv wenn Pfad gesetzt
            if ::WhiteoutDexMPQ != undefined and ::WhiteoutDexMPQ.getDirectory() != "" then
                searchMPQChk.enabled = true
            else
            (
                searchMPQChk.enabled = false
                searchMPQChk.checked = false
            )
            
            -- Liste der Checkboxen die durch Fast Settings gesteuert werden
            local controlledCheckboxes = #(
                skinnedChk, importMaterialsChk, importTexturesChk, importObjectsChk,
                bonesChk, helpersChk, lightsChk, attachmentsChk,
                particleEmittersOneChk, particleEmittersTwoChk, eventObjectsChk,
                ribbonEmittersChk, collisionShapesChk, camerasChk,
                importAnimationsChk, rotationsChk, translationChk, scaleChk,
                parameterChk, unwrapAnimationsChk, textureAnimationsCk,
                visibilityChk, colorChk
            )
            
            -- Fix: Wenn Custom ausgewählt ist, aktiviere die gesteuerten Checkboxen
            if fastSettingsRdo.state == 1 then
            (
                for chk in controlledCheckboxes do chk.enabled = true
            )
            else
            (
                -- Für andere Presets: Deaktiviere die gesteuerten Checkboxen
                for chk in controlledCheckboxes do chk.enabled = false
            )
            
            -- Die drei unteren Checkboxen IMMER aktiviert lassen
            importHelpersAsPointHelpersChk.enabled = true
            optimizeGeometryChk.enabled = true
            optimizeBonesAndHelpersChk.enabled = true
            
            -- Stelle sicher, dass die Hauptcheckboxen immer aktiviert sind wenn nötig
            if importAnimationsChk.checked then
            (
                if fastSettingsRdo.state == 1 then
                    enableAnimations true
            )
            else
            (
                enableAnimations false
                if fastSettingsRdo.state == 1 then
                    importAnimationsChk.enabled = true
            )
            
            if importObjectsChk.checked then
            (
                if fastSettingsRdo.state == 1 then
                    enableObjects true
            )
            else
            (
                enableObjects false
                if fastSettingsRdo.state == 1 then
                    importObjectsChk.enabled = true
            )

            -- Localization
            if ::L != undefined then
            (
                importerRoll.title = ::L.t "imp_whiteoutdex_importer_title"
                importBtn.text = ::L.t "imp_import_btn"
                importBtn.tooltip = ::L.t "imp_start_the_import_process_with_tip"
                skinnedChk.text = ::L.t "imp_skinned_chk"
                skinnedChk.tooltip = ::L.t "imp_import_skinning_data_vertex_w_tip"
                importTexturesChk.text = ::L.t "imp_import_textures_chk"
                importTexturesChk.tooltip = ::L.t "imp_import_and_assign_texture_maps_tip"
                importObjectsChk.text = ::L.t "imp_import_objects_chk"
                importObjectsChk.tooltip = ::L.t "imp_import_all_scene_objects_mast_tip"
                bonesChk.text = ::L.t "imp_bones_chk"
                bonesChk.tooltip = ::L.t "imp_import_skeletal_bones_for_anim_tip"
                helpersChk.text = ::L.t "imp_helpers_chk"
                helpersChk.tooltip = ::L.t "imp_import_helper_objects_for_posi_tip"
                lightsChk.text = ::L.t "imp_lights_chk"
                lightsChk.tooltip = ::L.t "imp_import_light_sources_from_the_tip"
                attachmentsChk.text = ::L.t "imp_attachments_chk"
                attachmentsChk.tooltip = ::L.t "imp_import_attachment_points_for_tip"
                particleEmittersOneChk.text = ::L.t "imp_particle_emitters_1_chk"
                particleEmittersOneChk.tooltip = ::L.t "imp_import_mdlstyle_particle_emit_tip"
                particleEmittersTwoChk.text = ::L.t "imp_particle_emitters_2_chk"
                particleEmittersTwoChk.tooltip = ::L.t "imp_import_advanced_particle_emitt_tip"
                eventObjectsChk.text = ::L.t "imp_event_objects_chk"
                eventObjectsChk.tooltip = ::L.t "imp_import_event_objects_for_soun_tip"
                ribbonEmittersChk.text = ::L.t "imp_ribbon_emitters_chk"
                ribbonEmittersChk.tooltip = ::L.t "imp_import_ribbon_trail_emitter_sy_tip"
                collisionShapesChk.text = ::L.t "imp_collision_shapes_chk"
                collisionShapesChk.tooltip = ::L.t "imp_import_collision_geometry_box_tip"
                importAnimationsChk.text = ::L.t "imp_import_animations_chk"
                importAnimationsChk.tooltip = ::L.t "imp_import_all_animation_data_mas_tip"
                rotationsChk.text = ::L.t "imp_rotations_chk"
                rotationsChk.tooltip = ::L.t "imp_import_rotation_animation_trac_tip"
                translationChk.text = ::L.t "imp_translation_chk"
                translationChk.tooltip = ::L.t "imp_import_positionmovement_anima_tip"
                scaleChk.text = ::L.t "imp_scale_chk"
                scaleChk.tooltip = ::L.t "imp_import_scaling_animation_track_tip"
                parameterChk.text = ::L.t "imp_parameter_chk"
                parameterChk.tooltip = ::L.t "imp_import_parameter_animations_i_tip"
                unwrapAnimationsChk.text = ::L.t "imp_unwrap_animations_chk"
                unwrapAnimationsChk.tooltip = ::L.t "imp_import_uv_coordinate_animation_tip"
                textureAnimationsCk.text = ::L.t "imp_texture_animations_chk"
                textureAnimationsCk.tooltip = ::L.t "imp_import_texture_switching_anima_tip"
                visibilityChk.text = ::L.t "imp_visibility_chk"
                visibilityChk.tooltip = ::L.t "imp_import_visibilitytransparency_tip"
                importMaterialsChk.text = ::L.t "imp_import_materials_chk"
                importMaterialsChk.tooltip = ::L.t "imp_import_material_definitions_an_tip"
                fastSettingsRdo.tooltip = ::L.t "imp_quick_presets_for_common_impor_tip"
                modeRdo.tooltip = ::L.t "imp_choose_whether_to_replace_curr_tip"
                colorChk.text = ::L.t "imp_color_chk"
                colorChk.tooltip = ::L.t "imp_import_colortinting_animation_tip"
                camerasChk.text = ::L.t "imp_cameras_chk"
                camerasChk.tooltip = ::L.t "imp_import_camera_objects_and_thei_tip"
                importHelpersAsPointHelpersChk.text = ::L.t "imp_import_helpers_as_point_helpers_chk"
                importHelpersAsPointHelpersChk.tooltip = ::L.t "imp_import_helpers_as_point_object_tip"
                optimizeGeometryChk.text = ::L.t "imp_optimize_geometry_chk"
                optimizeGeometryChk.tooltip = ::L.t "imp_combine_nearby_vertices_and_si_tip"
                optimizeBonesAndHelpersChk.text = ::L.t "imp_optimize_bones_and_helpers_chk"
                optimizeBonesAndHelpersChk.tooltip = ::L.t "imp_remove_unnecessary_bones_and_h_tip"
                optimizeGeometryChk.text = ::L.t "imp_optimize_geometry_chk"
                optimizeBonesAndHelpersChk.text = ::L.t "imp_optimize_bones_and_helpers_chk"
                mpqGrp.text = ::L.t "imp_mpq_textures_grp"
                searchMPQChk.text = ::L.t "imp_search_mpq_chk"
                searchMPQChk.tooltip = ::L.t "imp_search_mpq_tip"
                idleLbl.text = ::L.t "imp_idle_lbl"
                idleLbl.tooltip = ::L.t "imp_displays_current_import_status_tip"
                progressBarPbar.tooltip = ::L.t "imp_shows_import_progress_tip"
            )
        )

        on importBtn pressed do
        (
            setSettings()
            saveSettings()
            
            -- Call the import function from WhiteoutDexImportFunctions.ms
            local result = ::WhiteoutDexExecuteImport ::WhiteoutDexImportedModel modelpath mode idleLbl progressBarPbar
            local noError = result[1]
            local message = result[2]
            
            idleLbl.caption = "Idle..."
            importBtn.enabled = false
            
            if noError then messageBox message title:(::L.t "imp_import_successful_ptitle")
            
            destroyDialog importerRoll
            
            -- Post-import actions (viewport settings, sequence manager update)
            ::WhiteoutDexPostImportActions()
        )
    )
	
    -- ============================================================================
    -- REFORGED IMPORTER ROLLOUT (v1200)
    -- ============================================================================
    rollout importerReforgedRoll "WhiteoutDex Importer (Reforged)" width:410 height:610
    (
        button importBtn "Import" pos:[149,578] width:58 height:22 tooltip:"Start the import process with current settings"
        GroupBox geometryGrp "Geometry" pos:[14,9] width:104 height:51
        checkbox skinnedChk "Skinned" pos:[30,29] width:70 height:16 enabled:false checked:true
        GroupBox objectsGrp "Objects" pos:[11,134] width:236 height:199
        GroupBox materialsGrp "Materials" pos:[13,68] width:114 height:63
        checkbox importTexturesChk "Import Textures" pos:[19,109] width:94 height:17 enabled:false checked:true
        checkbox importObjectsChk "Import Objects" pos:[22,150] width:125 height:18 enabled:false checked:true
        checkbox bonesChk "Bones" pos:[29,185] width:94 height:17 enabled:false checked:true
        checkbox helpersChk "Helpers" pos:[29,206] width:77 height:18 enabled:false checked:true
        checkbox lightsChk "Lights" pos:[29,228] width:60 height:17 enabled:false checked:true
        checkbox attachmentsChk "Attachments" pos:[29,251] width:82 height:17 enabled:false checked:true
        checkbox particleEmittersOneChk "Particle Emitters 1" pos:[127,185] width:107 height:15 enabled:false checked:true
        checkbox particleEmittersTwoChk "Particle Emitters 2" pos:[127,206] width:105 height:17 enabled:false checked:true
        checkbox eventObjectsChk "Event Objects" pos:[29,273] width:87 height:16 enabled:false checked:true
        checkbox ribbonEmittersChk "Ribbon Emitters" pos:[127,228] width:105 height:15 enabled:false checked:true
        checkbox collisionShapesChk "Collision Shapes" pos:[127,251] width:99 height:16 enabled:false checked:true
        checkbox camerasChk "Cameras" pos:[127,273] width:96 height:13 enabled:false checked:true
        -- Reforged-spezifische Objekte
        checkbox cornEmittersChk "Corn Emitters" pos:[29,295] width:90 height:16 enabled:false checked:true tooltip:"Import Popcorn FX particle emitters (Reforged)"
        checkbox faceFxChk "FaceFX" pos:[127,295] width:96 height:16 enabled:false checked:true tooltip:"Import FaceFX facial animation targets (Reforged)"

        GroupBox animationsGrp "Animations" pos:[136,10] width:257 height:124
        checkbox importAnimationsChk "Import Animations" pos:[144,26] width:108 height:19 enabled:false checked:true
        checkbox rotationsChk "Rotations" pos:[159,54] width:74 height:15 enabled:false checked:true
        checkbox translationChk "Translation" pos:[159,73] width:80 height:16 enabled:false checked:true
        checkbox scaleChk "Scale" pos:[159,93] width:79 height:15 enabled:false checked:true
        checkbox parameterChk "Parameter" pos:[249,54] width:89 height:17 enabled:false checked:true
        checkbox unwrapAnimationsChk "Unwrap Animations" pos:[249,73] width:114 height:17 enabled:false checked:true
        checkbox textureAnimationsCk "Texture Animations" pos:[249,93] width:113 height:14 enabled:false checked:true
        checkbox visibilityChk "Visibility" pos:[159,111] width:62 height:15 enabled:false checked:true
        checkbox importMaterialsChk "Import Materials" pos:[19,87] width:93 height:17 enabled:false checked:true
        GroupBox settingsGrp "Settings" pos:[251,134] width:145 height:200
        radiobuttons fastSettingsRdo "Fast Settings" pos:[262,149] width:128 height:110 enabled:true labels:#("Custom", "Static No Materials", "Static Materials", "Animated No Skinning", "Animated No objects", "All") default:6 columns:1
        radiobuttons modeRdo "Mode:" pos:[264,284] width:79 height:46 enabled:false labels:#("New Scene", "Merge") columns:1
        checkbox colorChk "Color" pos:[248,111] width:127 height:15 enabled:false checked:true

        GroupBox helperOptionsGrp "Helper Options" pos:[10,340] width:381 height:45
        checkbox importHelpersAsPointHelpersChk "Import Helpers as Point Helpers" pos:[25,360] width:250 height:16 checked:true

        groupBox optimizerGrp "Optimizer" pos:[10,392] width:381 height:68
        checkbox optimizeGeometryChk "Optimize Geometry" pos:[25,412] width:274 height:16 checked:true
        checkbox optimizeBonesAndHelpersChk "Optimize Bones and Helpers" pos:[26,434] width:256 height:14 checked:true

        groupBox cascGrp "CASC Textures" pos:[10,465] width:381 height:40
        checkbox searchCASCChk "Search CASC Archives for Textures" pos:[25,482] width:280 height:16 checked:false tooltip:"Search Warcraft III Reforged CASC archives for textures. Set the CASC directory in WhiteoutDex Settings first."

        GroupBox progressGrp "Progress" pos:[10,512] width:382 height:61
        progressBar progressBarPbar "ProgressBar" pos:[32,553] width:355 height:11 color:(color 30 10 190)
        label idleLbl "Idle..." pos:[35,529] width:158 height:17

        local iniFile = (getDir #plugcfg + "\\WhiteoutDexImporter.ini")

        fn saveSettings =
        (
            setINISetting iniFile "Geometry" "Skinned" (skinnedChk.checked as string)
            setINISetting iniFile "Materials" "ImportMaterials" (importMaterialsChk.checked as string)
            setINISetting iniFile "Materials" "ImportTextures" (importTexturesChk.checked as string)
            setINISetting iniFile "Objects" "ImportObjects" (importObjectsChk.checked as string)
            setINISetting iniFile "Objects" "Bones" (bonesChk.checked as string)
            setINISetting iniFile "Objects" "Helpers" (helpersChk.checked as string)
            setINISetting iniFile "Objects" "Lights" (lightsChk.checked as string)
            setINISetting iniFile "Objects" "Attachments" (attachmentsChk.checked as string)
            setINISetting iniFile "Objects" "ParticleEmitters1" (particleEmittersOneChk.checked as string)
            setINISetting iniFile "Objects" "ParticleEmitters2" (particleEmittersTwoChk.checked as string)
            setINISetting iniFile "Objects" "EventObjects" (eventObjectsChk.checked as string)
            setINISetting iniFile "Objects" "RibbonEmitters" (ribbonEmittersChk.checked as string)
            setINISetting iniFile "Objects" "CollisionShapes" (collisionShapesChk.checked as string)
            setINISetting iniFile "Objects" "Cameras" (camerasChk.checked as string)
            setINISetting iniFile "Objects" "CornEmitters" (cornEmittersChk.checked as string)
            setINISetting iniFile "Objects" "FaceFX" (faceFxChk.checked as string)
            setINISetting iniFile "Animations" "ImportAnimations" (importAnimationsChk.checked as string)
            setINISetting iniFile "Animations" "Rotations" (rotationsChk.checked as string)
            setINISetting iniFile "Animations" "Translation" (translationChk.checked as string)
            setINISetting iniFile "Animations" "Scale" (scaleChk.checked as string)
            setINISetting iniFile "Animations" "Parameter" (parameterChk.checked as string)
            setINISetting iniFile "Animations" "UnwrapAnimations" (unwrapAnimationsChk.checked as string)
            setINISetting iniFile "Animations" "TextureAnimations" (textureAnimationsCk.checked as string)
            setINISetting iniFile "Animations" "Visibility" (visibilityChk.checked as string)
            setINISetting iniFile "Animations" "Color" (colorChk.checked as string)
            setINISetting iniFile "Settings" "FastSettings" (fastSettingsRdo.state as string)
            setINISetting iniFile "HelperOptions" "ImportHelpersAsPointHelpers" (importHelpersAsPointHelpersChk.checked as string)
            setINISetting iniFile "Optimizer" "OptimizeGeometry" (optimizeGeometryChk.checked as string)
            setINISetting iniFile "Optimizer" "OptimizeBonesAndHelpers" (optimizeBonesAndHelpersChk.checked as string)
            setINISetting iniFile "CASC" "SearchCASC" (searchCASCChk.checked as string)
        )

        fn loadSettings =
        (
            local val
            val = getINISetting iniFile "Geometry" "Skinned"
            if val != "" then skinnedChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Materials" "ImportMaterials"
            if val != "" then importMaterialsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Materials" "ImportTextures"
            if val != "" then importTexturesChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "ImportObjects"
            if val != "" then importObjectsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Bones"
            if val != "" then bonesChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Helpers"
            if val != "" then helpersChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Lights"
            if val != "" then lightsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Attachments"
            if val != "" then attachmentsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "ParticleEmitters1"
            if val != "" then particleEmittersOneChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "ParticleEmitters2"
            if val != "" then particleEmittersTwoChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "EventObjects"
            if val != "" then eventObjectsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "RibbonEmitters"
            if val != "" then ribbonEmittersChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "CollisionShapes"
            if val != "" then collisionShapesChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "Cameras"
            if val != "" then camerasChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "CornEmitters"
            if val != "" then cornEmittersChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Objects" "FaceFX"
            if val != "" then faceFxChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "ImportAnimations"
            if val != "" then importAnimationsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Rotations"
            if val != "" then rotationsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Translation"
            if val != "" then translationChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Scale"
            if val != "" then scaleChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Parameter"
            if val != "" then parameterChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "UnwrapAnimations"
            if val != "" then unwrapAnimationsChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "TextureAnimations"
            if val != "" then textureAnimationsCk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Visibility"
            if val != "" then visibilityChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Animations" "Color"
            if val != "" then colorChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Settings" "FastSettings"
            if val != "" then fastSettingsRdo.state = (val as integer)
            val = getINISetting iniFile "HelperOptions" "ImportHelpersAsPointHelpers"
            if val != "" then importHelpersAsPointHelpersChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Optimizer" "OptimizeGeometry"
            if val != "" then optimizeGeometryChk.checked = (val as booleanClass)
            val = getINISetting iniFile "Optimizer" "OptimizeBonesAndHelpers"
            if val != "" then optimizeBonesAndHelpersChk.checked = (val as booleanClass)
            val = getINISetting iniFile "CASC" "SearchCASC"
            if val != "" then searchCASCChk.checked = (val as booleanClass)
        )

        fn setSettings =
        (
            ::WhiteoutDexImportSettings.importMaterials = importMaterialsChk.checked
            ::WhiteoutDexImportSettings.importSkinning = skinnedChk.checked
            ::WhiteoutDexImportSettings.importAnimations = importAnimationsChk.checked
            ::WhiteoutDexImportSettings.importObjects = importObjectsChk.checked
            ::WhiteoutDexImportSettings.importBones = bonesChk.checked
            ::WhiteoutDexImportSettings.importHelpers = helpersChk.checked
            ::WhiteoutDexImportSettings.importLights = lightsChk.checked
            ::WhiteoutDexImportSettings.importAttachments = attachmentsChk.checked
            ::WhiteoutDexImportSettings.importPE1 = particleEmittersOneChk.checked
            ::WhiteoutDexImportSettings.importPE2 = particleEmittersTwoChk.checked
            ::WhiteoutDexImportSettings.importRE = ribbonEmittersChk.checked
            ::WhiteoutDexImportSettings.importEO = eventObjectsChk.checked
            ::WhiteoutDexImportSettings.importCS = collisionShapesChk.checked
            ::WhiteoutDexImportSettings.importCameras = camerasChk.checked
            ::WhiteoutDexImportSettings.importTransAnim = translationChk.checked
            ::WhiteoutDexImportSettings.importRotAnim = rotationsChk.checked
            ::WhiteoutDexImportSettings.importScaleAnim = scaleChk.checked
            ::WhiteoutDexImportSettings.importTXAnim = textureAnimationsCk.checked
            ::WhiteoutDexImportSettings.importUVAnim = unwrapAnimationsChk.checked
            ::WhiteoutDexImportSettings.importVisAnim = visibilityChk.checked
            ::WhiteoutDexImportSettings.importColorAnim = colorChk.checked
            ::WhiteoutDexImportSettings.importParamAnim = parameterChk.checked
            ::WhiteoutDexImportSettings.importHelpersAsPointHelpers = importHelpersAsPointHelpersChk.checked
            ::WhiteoutDexImportSettings.optimizeGeometry = optimizeGeometryChk.checked
            ::WhiteoutDexImportSettings.optimizeBonesHelpers = optimizeBonesAndHelpersChk.checked
            ::WhiteoutDexImportSettings.searchMPQ = false
            ::WhiteoutDexImportSettings.searchCASC = searchCASCChk.checked
            ::WhiteoutDexImportSettings.importCornEmitters = cornEmittersChk.checked
            ::WhiteoutDexImportSettings.importFaceFX = faceFxChk.checked
        )

        fn checkObjects state =
        (
            importObjectsChk.checked = state
            bonesChk.checked = state
            helpersChk.checked = state
            lightsChk.checked = state
            attachmentsChk.checked = state
            particleEmittersOneChk.checked = state
            particleEmittersTwoChk.checked = state
            ribbonEmittersChk.checked = state
            eventObjectsChk.checked = state
            collisionShapesChk.checked = state
            camerasChk.checked = state
            cornEmittersChk.checked = state
            faceFxChk.checked = state
        )

        fn checkBH state =
        (
            importObjectsChk.checked = state
            bonesChk.checked = state
            helpersChk.checked = state
        )

        fn checkSkinning state = ( skinnedChk.checked = state )
        fn checkMaterials state = ( importMaterialsChk.checked = state; importTexturesChk.checked = state )

        fn checkAnimations state =
        (
            importAnimationsChk.checked = state
            rotationsChk.checked = state
            translationChk.checked = state
            scaleChk.checked = state
            parameterChk.checked = state
            unwrapAnimationsChk.checked = state
            textureAnimationsCk.checked = state
            visibilityChk.checked = state
            colorChk.checked = state
        )

        fn enableAnimations state =
        (
            importAnimationsChk.enabled = state
            rotationsChk.enabled = state
            translationChk.enabled = state
            scaleChk.enabled = state
            parameterChk.enabled = state
            unwrapAnimationsChk.enabled = state
            textureAnimationsCk.enabled = state
            visibilityChk.enabled = state
            colorChk.enabled = state
        )

        fn enableObjects state =
        (
            importObjectsChk.enabled = state
            bonesChk.enabled = state
            helpersChk.enabled = state
            lightsChk.enabled = state
            attachmentsChk.enabled = state
            particleEmittersOneChk.enabled = state
            particleEmittersTwoChk.enabled = state
            ribbonEmittersChk.enabled = state
            eventObjectsChk.enabled = state
            collisionShapesChk.enabled = state
            camerasChk.enabled = state
            cornEmittersChk.enabled = state
            faceFxChk.enabled = state
        )

        on skinnedChk changed state do saveSettings()
        on importMaterialsChk changed state do saveSettings()
        on importTexturesChk changed state do saveSettings()
        on importObjectsChk changed state do
        (
            if onControlSet then
            (
                onControlSet = false
                checkObjects state
                enableObjects state
                importObjectsChk.enabled = true
                onControlSet = true
            )
            saveSettings()
        )
        on bonesChk changed state do
        (
            if onControlSet then
            (
                onControlSet = false
                if not state then skinnedChk.checked = false
                onControlSet = true
            )
            saveSettings()
        )
        on helpersChk changed state do saveSettings()
        on lightsChk changed state do saveSettings()
        on attachmentsChk changed state do saveSettings()
        on particleEmittersOneChk changed state do saveSettings()
        on particleEmittersTwoChk changed state do saveSettings()
        on eventObjectsChk changed state do saveSettings()
        on ribbonEmittersChk changed state do saveSettings()
        on collisionShapesChk changed state do saveSettings()
        on camerasChk changed state do saveSettings()
        on cornEmittersChk changed state do saveSettings()
        on faceFxChk changed state do saveSettings()
        on importAnimationsChk changed state do
        (
            if onControlSet then
            (
                onControlSet = false
                checkAnimations state
                enableAnimations state
                importAnimationsChk.enabled = true
                onControlSet = true
            )
            saveSettings()
        )
        on rotationsChk changed state do saveSettings()
        on translationChk changed state do saveSettings()
        on scaleChk changed state do saveSettings()
        on parameterChk changed state do saveSettings()
        on unwrapAnimationsChk changed state do saveSettings()
        on textureAnimationsCk changed state do saveSettings()
        on visibilityChk changed state do saveSettings()
        on colorChk changed state do saveSettings()
        on importHelpersAsPointHelpersChk changed state do saveSettings()
        on optimizeGeometryChk changed state do saveSettings()
        on optimizeBonesAndHelpersChk changed state do saveSettings()
        on searchCASCChk changed state do saveSettings()
        on fastSettingsRdo changed newState do
        (
            onControlSet = false

            local controlledCheckboxes = #(
                skinnedChk, importMaterialsChk, importTexturesChk, importObjectsChk,
                bonesChk, helpersChk, lightsChk, attachmentsChk,
                particleEmittersOneChk, particleEmittersTwoChk, eventObjectsChk,
                ribbonEmittersChk, collisionShapesChk, camerasChk,
                cornEmittersChk, faceFxChk,
                importAnimationsChk, rotationsChk, translationChk, scaleChk,
                parameterChk, unwrapAnimationsChk, textureAnimationsCk,
                visibilityChk, colorChk
            )

            case newState of
            (
                1: ( for chk in controlledCheckboxes do chk.enabled = true )
                2: ( for chk in controlledCheckboxes do chk.enabled = false; checkMaterials false; checkSkinning false; checkAnimations false; checkObjects false )
                3: ( for chk in controlledCheckboxes do chk.enabled = false; checkMaterials true; checkSkinning false; checkAnimations false; checkObjects false )
                4: ( for chk in controlledCheckboxes do chk.enabled = false; checkMaterials true; checkSkinning false; checkAnimations true; checkObjects false; checkBH true )
                5: ( for chk in controlledCheckboxes do chk.enabled = false; checkMaterials true; checkSkinning true; checkAnimations true; checkObjects false; checkBH true )
                6: ( for chk in controlledCheckboxes do chk.enabled = false; checkMaterials true; checkSkinning true; checkAnimations true; checkObjects true )
            )

            if not importAnimationsChk.checked then
            (
                enableAnimations false
                importAnimationsChk.enabled = newState == 1
            )
            if not importObjectsChk.checked then
            (
                enableObjects false
                importObjectsChk.enabled = newState == 1
            )

            onControlSet = true
            saveSettings()
        )

        on importerReforgedRoll open do
        (
            loadSettings()

            local controlledCheckboxes = #(
                skinnedChk, importMaterialsChk, importTexturesChk, importObjectsChk,
                bonesChk, helpersChk, lightsChk, attachmentsChk,
                particleEmittersOneChk, particleEmittersTwoChk, eventObjectsChk,
                ribbonEmittersChk, collisionShapesChk, camerasChk,
                cornEmittersChk, faceFxChk,
                importAnimationsChk, rotationsChk, translationChk, scaleChk,
                parameterChk, unwrapAnimationsChk, textureAnimationsCk,
                visibilityChk, colorChk
            )

            if fastSettingsRdo.state == 1 then
                ( for chk in controlledCheckboxes do chk.enabled = true )
            else
                ( for chk in controlledCheckboxes do chk.enabled = false )

            importHelpersAsPointHelpersChk.enabled = true
            optimizeGeometryChk.enabled = true
            optimizeBonesAndHelpersChk.enabled = true
            
            -- CASC Checkbox: nur aktiv wenn Pfad gesetzt
            local cascIni2 = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
            local cascDir2 = getINISetting cascIni2 "CASC" "W3Path"
            if cascDir2 != "" and doesFileExist (cascDir2 + "\\.build.info") then
                searchCASCChk.enabled = true
            else
            (
                searchCASCChk.enabled = false
                searchCASCChk.checked = false
            )
            if importAnimationsChk.checked then
            (
                if fastSettingsRdo.state == 1 then enableAnimations true
            )
            else
            (
                enableAnimations false
                if fastSettingsRdo.state == 1 then importAnimationsChk.enabled = true
            )

            if importObjectsChk.checked then
            (
                if fastSettingsRdo.state == 1 then enableObjects true
            )
            else
            (
                enableObjects false
                if fastSettingsRdo.state == 1 then importObjectsChk.enabled = true
            )

            -- Localization
            if ::L != undefined then
            (
                importerReforgedRoll.title = ::L.t "imp_whiteoutdex_importer_reforged_title"
                importBtn.text = ::L.t "imp_import_btn_2"
                importBtn.tooltip = ::L.t "imp_start_the_import_process_with_tip_2"
                skinnedChk.text = ::L.t "imp_skinned_chk_2"
                importTexturesChk.text = ::L.t "imp_import_textures_chk_2"
                importObjectsChk.text = ::L.t "imp_import_objects_chk_2"
                bonesChk.text = ::L.t "imp_bones_chk_2"
                helpersChk.text = ::L.t "imp_helpers_chk_2"
                lightsChk.text = ::L.t "imp_lights_chk_2"
                attachmentsChk.text = ::L.t "imp_attachments_chk_2"
                particleEmittersOneChk.text = ::L.t "imp_particle_emitters_1_chk_2"
                particleEmittersTwoChk.text = ::L.t "imp_particle_emitters_2_chk_2"
                eventObjectsChk.text = ::L.t "imp_event_objects_chk_2"
                ribbonEmittersChk.text = ::L.t "imp_ribbon_emitters_chk_2"
                collisionShapesChk.text = ::L.t "imp_collision_shapes_chk_2"
                camerasChk.text = ::L.t "imp_cameras_chk_2"
                cornEmittersChk.text = ::L.t "imp_corn_emitters_chk"
                cornEmittersChk.tooltip = ::L.t "imp_import_popcorn_fx_particle_emi_tip"
                faceFxChk.text = ::L.t "imp_facefx_chk"
                faceFxChk.tooltip = ::L.t "imp_import_facefx_facial_animation_tip"
                importAnimationsChk.text = ::L.t "imp_import_animations_chk_2"
                rotationsChk.text = ::L.t "imp_rotations_chk_2"
                translationChk.text = ::L.t "imp_translation_chk_2"
                scaleChk.text = ::L.t "imp_scale_chk_2"
                parameterChk.text = ::L.t "imp_parameter_chk_2"
                unwrapAnimationsChk.text = ::L.t "imp_unwrap_animations_chk_2"
                textureAnimationsCk.text = ::L.t "imp_texture_animations_chk_2"
                visibilityChk.text = ::L.t "imp_visibility_chk_2"
                importMaterialsChk.text = ::L.t "imp_import_materials_chk_2"
                colorChk.text = ::L.t "imp_color_chk_2"
                importHelpersAsPointHelpersChk.text = ::L.t "imp_import_helpers_as_point_helpers_chk_2"
                optimizeGeometryChk.text = ::L.t "imp_optimize_geometry_chk_2"
                optimizeBonesAndHelpersChk.text = ::L.t "imp_optimize_bones_and_helpers_chk_2"
                idleLbl.text = ::L.t "imp_idle_lbl_2"
                searchCASCChk.text = ::L.t "imp_search_casc_chk"
			    searchCASCChk.tooltip = ::L.t "imp_search_casc_tip"
            )
        )

        on importBtn pressed do
        (
            setSettings()
            saveSettings()
            local result = ::WhiteoutDexExecuteImport ::WhiteoutDexImportedModel modelpath mode idleLbl progressBarPbar
            local noError = result[1]
            local message = result[2]
            idleLbl.caption = "Idle..."
            importBtn.enabled = false
            if noError then messageBox message title:(::L.t "imp_import_successful_ptitle_2")
            destroyDialog importerReforgedRoll
            ::WhiteoutDexPostImportActions()
        )
    )

    -- ============================================================================
    -- MPQ MODEL BROWSER ROLLOUT
    -- ============================================================================
    rollout mpqBrowserRoll "MPQ Model Browser" width:460 height:510
    (
        group "Search Models"
        (
            edittext searchEdt "" width:420 tooltip:"Type to filter MDX files (e.g. Footman, Human, Orc)"
            dropdownlist mpqTypeDDL "" width:180 items:#("All types", "Units", "Buildings", "Doodads", "Abilities", "Environment", "Objects") across:2
            dropdownlist mpqRaceDDL "" width:180 items:#("All races", "Human", "Orc", "Night Elf", "Undead", "Creeps", "Demon", "Other")
        )
        
        label countLbl "Models (0)" align:#left
        group "Models"
        (
            listbox mdxListBox "" width:420 height:24
        )
        
        label statusLbl "" align:#left
        
        button cancelMpqBtn "Cancel" width:214 height:28 across:2
        button importMpqBtn "Import Selected" width:214 height:28 enabled:false
        
        -- Internal state
        local allMdxFiles = #()
        local filteredFiles = #()
        
        fn getPathSegment fp idx =
        (
            local parts = filterString fp "/\\"
            if idx <= parts.count then toLower parts[idx] else ""
        )
        
        fn matchesType fp typeIdx =
        (
            if typeIdx <= 1 then return true
            local seg = getPathSegment fp 1
            case typeIdx of
            (
                2: seg == "units"
                3: seg == "buildings"
                4: seg == "doodads"
                5: seg == "abilities" or seg == "spells"
                6: seg == "environment"
                7: seg == "objects"
                default: true
            )
        )
        
        fn matchesRace fp raceIdx =
        (
            if raceIdx <= 1 then return true
            local seg = getPathSegment fp 2
            case raceIdx of
            (
                2: seg == "human"
                3: seg == "orc"
                4: seg == "nightelf"
                5: seg == "undead"
                6: seg == "creeps" or seg == "critters"
                7: seg == "demon"
                8: seg != "human" and seg != "orc" and seg != "nightelf" and seg != "undead" and seg != "creeps" and seg != "critters" and seg != "demon"
                default: true
            )
        )
        
        fn updateList =
        (
            local searchTerm = toLower searchEdt.text
            local typeIdx = mpqTypeDDL.selection
            local raceIdx = mpqRaceDDL.selection
            
            -- Get base list from MPQ
            local baseFiles = #()
            if ::WhiteoutDexMPQ != undefined then
                baseFiles = ::WhiteoutDexMPQ.filterMDXFiles ""
            
            -- Apply filters
            filteredFiles = #()
            for f in baseFiles do
            (
                if not (matchesType f typeIdx) then continue
                if not (matchesRace f raceIdx) then continue
                if searchTerm != "" and findString (toLower f) searchTerm == undefined then continue
                append filteredFiles f
            )
            
            -- Build display names
            local displayNames = #()
            for f in filteredFiles do
            (
                local fname = filenameFromPath f
                local dir = getFilenamePath f
                if dir.count > 0 then
                    append displayNames (fname + "  [" + (trimRight dir "\\") + "]")
                else
                    append displayNames fname
            )
            
            mdxListBox.items = displayNames
            countLbl.text = "Models (" + (filteredFiles.count as string) + ")"
            importMpqBtn.enabled = false
            statusLbl.text = ""
        )
        
        on searchEdt changed txt do updateList()
        on mpqTypeDDL selected i do updateList()
        on mpqRaceDDL selected i do updateList()
        
        on mdxListBox selected idx do
        (
            importMpqBtn.enabled = (idx > 0 and idx <= filteredFiles.count)
        )
        
        on mdxListBox doubleClicked idx do
        (
            if idx > 0 and idx <= filteredFiles.count then
                importMpqBtn.pressed()
        )
        
        on importMpqBtn pressed do
        (
            local idx = mdxListBox.selection
            if idx < 1 or idx > filteredFiles.count then return OK
            
            local mpqPath = filteredFiles[idx]
            statusLbl.text = "Extracting..."
            
            -- Extract MDX from MPQ
            local localPath = ::WhiteoutDexMPQ.extractMDX mpqPath
            if localPath == undefined then
            (
                statusLbl.text = "ERROR: Could not extract MDX"
                return OK
            )
            
            statusLbl.text = "Reading MDX..."
            
            -- Set modelpath for the importer (use temp folder)
            modelpath = getFilenamePath localPath
            mode = true  -- standard mode
            
            -- Detect version and read
            mdxVersion = peekMDXVersion localPath
            local noError = ::WhiteoutDexReadMDX localPath mode
            
            -- Close MPQs
            try ( ::WhiteoutDexMPQ.closeMPQs() ) catch()
            
            try ( destroyDialog mpqBrowserRoll ) catch()
            
            if noError then
            (
                if mdxVersion >= 1200 then
                    createDialog importerReforgedRoll
                else
                (
                    createDialog importerRoll
                    try ( importerRoll.searchMPQChk.checked = true ) catch()
                )
            )
        )
        
        on cancelMpqBtn pressed do
        (
            try ( ::WhiteoutDexMPQ.closeMPQs() ) catch()
            destroyDialog mpqBrowserRoll
        )
        
        on mpqBrowserRoll open do
        (
            statusLbl.text = "Loading file list..."
            
            -- Open MPQs and scan for MDX files
            if ::WhiteoutDexMPQ != undefined then
            (
                ::WhiteoutDexMPQ.scanAndOpenMPQs()
                allMdxFiles = ::WhiteoutDexMPQ.listMDXFiles()
            )
            
            updateList()
            statusLbl.text = ""
            
            -- Localization
            if ::L != undefined then
            (
                mpqBrowserRoll.title = ::L.t "mpq_browser_title"
                searchEdt.tooltip = ::L.t "mpq_browser_search_tip"
                importMpqBtn.text = ::L.t "mpq_browser_import_btn"
                cancelMpqBtn.text = ::L.t "mpq_browser_cancel_btn"
            )
        )
        
        on mpqBrowserRoll close do
        (
            -- Safety: close MPQs if still open
            try ( ::WhiteoutDexMPQ.closeMPQs() ) catch()
        )
    )

    -- ============================================================================
    -- CASC MODEL BROWSER ROLLOUT
    -- ============================================================================
    rollout cascBrowserRoll "CASC Model Browser" width:460 height:490
    (
        group "Search Models"
        (
            edittext cascSearchEdt "" width:420
            dropdownlist cascTypeDDL "" width:140 items:#("All types", "Units", "Buildings", "Doodads", "Abilities", "Environment", "Objects") across:3
            dropdownlist cascRaceDDL "" width:140 items:#("All races", "Human", "Orc", "Night Elf", "Undead", "Creeps", "Demon", "Other")
            dropdownlist cascFilterDDL "" width:80 items:#("All", "SD", "HD")
        )
        
        label cascCountLbl "Models (0)" align:#left
        group "Models"
        (
            listbox cascListBox "" width:420 height:22
        )
        
        label cascInfoLbl "" align:#left
        
        button cancelCascBtn "Cancel" width:214 height:28 across:2
        button importCascBtn "Import Selected" width:214 height:28 enabled:false
        
        local cascAllFiles = #()
        local cascPaths = #()
        local cascReady = false
        
        fn getCascTag cp =
        (
            -- New format: check for _hd.w3mod in the full CASC path
            local lower = toLower cp
            if findString lower "_hd.w3mod" != undefined then return "HD"
            
            -- Legacy: check for [HD]/[SD] prefix
            if cp.count > 5 and cp[1] == "[" then
            (
                local br = findString cp "] "
                if br != undefined then return (substring cp 2 (br - 2))
            )
            
            -- All WC3 Reforged files are either HD or SD
            "SD"
        )
        
        fn getCascDP cp =
        (
            local dp = cp
            -- Strip [HD]/[SD] prefix (legacy)
            if dp.count > 5 and dp[1] == "[" then
            ( local br = findString dp "] " ; if br != undefined then dp = substring dp (br+2) -1 )
            
            -- Strip container prefix: everything up to and including the last ':'
            -- e.g. "war3.w3mod:_hd.w3mod:units\human\arthas.mdx" → "units\human\arthas.mdx"
            local lastColon = 0
            for i = 1 to dp.count do
                if dp[i] == ":" then lastColon = i
            if lastColon > 0 then
                dp = substring dp (lastColon + 1) -1
            
            dp
        )
        
        fn fmtCascEntry cp =
        (
            local dp = getCascDP cp
            local fn2 = filenameFromPath dp
            local dir = trimRight (getFilenamePath dp) "/\\"
            local tag = getCascTag cp
            local tagStr = case tag of
            (
                "HD": "  (HD)"
                "SD": "  (SD)"
                default: ""
            )
            -- Add container label for addon files
            local lower = toLower cp
            if findString lower "_addons\\" == 1 then
                tagStr += "  [addon]"
            else if findString lower "_teen" != undefined then
                tagStr += "  [teen]"
            else if findString lower "_deprecated" != undefined then
                tagStr += "  [deprecated]"
            
            fn2 + (if dir != "" then ("   [" + dir + "]") else "") + tagStr
        )
        
        fn getPathSegment dp idx =
        (
            local parts = filterString dp "/\\"
            if idx <= parts.count then toLower parts[idx] else ""
        )
        
        fn matchesType dp typeIdx =
        (
            if typeIdx <= 1 then return true
            local seg = getPathSegment dp 1
            case typeIdx of
            (
                2: seg == "units"
                3: seg == "buildings"
                4: seg == "doodads"
                5: seg == "abilities" or seg == "spells"
                6: seg == "environment"
                7: seg == "objects"
                default: true
            )
        )
        
        fn matchesRace dp raceIdx =
        (
            if raceIdx <= 1 then return true
            local seg = getPathSegment dp 2
            case raceIdx of
            (
                2: seg == "human"
                3: seg == "orc"
                4: seg == "nightelf"
                5: seg == "undead"
                6: seg == "creeps" or seg == "critters"
                7: seg == "demon"
                8: seg != "human" and seg != "orc" and seg != "nightelf" and seg != "undead" and seg != "creeps" and seg != "critters" and seg != "demon"
                default: true
            )
        )
        
        fn loadCascModels =
        (
            if not cascReady then return()
            
            -- Load all MDX files once (cached)
            if cascAllFiles.count == 0 then
            (
                try (
                    local arr = ::WhiteoutDexCASC.filterFiles ".mdx"
                    if arr != undefined then
                        cascAllFiles = arr
                ) catch()
            )
            
            -- Apply filters
            local flt = cascSearchEdt.text
            local fl = toLower flt
            local typeIdx = cascTypeDDL.selection
            local raceIdx = cascRaceDDL.selection
            local qualIdx = cascFilterDDL.selection
            
            local filtered = #()
            for f in cascAllFiles do
            (
                -- Quality filter
                if qualIdx == 2 then ( local t = getCascTag f ; if t != "SD" then continue )
                if qualIdx == 3 then ( local t = getCascTag f ; if t != "HD" then continue )
                
                local dp = getCascDP f
                
                -- Type filter
                if not (matchesType dp typeIdx) then continue
                
                -- Race filter
                if not (matchesRace dp raceIdx) then continue
                
                -- Search filter
                if fl != "" and findString (toLower dp) fl == undefined then continue
                
                append filtered f
            )
            
            cascPaths = #()
            local items = #()
            local seen = Dictionary #string
            local mx = amin #(filtered.count, 4000)
            for i = 1 to mx do
            (
                local entry = fmtCascEntry filtered[i]
                local key = toLower entry
                if not (hasDictValue seen key) then
                (
                    seen[key] = true
                    append items entry
                    append cascPaths filtered[i]
                )
            )
            cascListBox.items = items
            cascCountLbl.text = "Models (" + items.count as string + ")"
            importCascBtn.enabled = false
            cascInfoLbl.text = ""
        )
        
        on cascSearchEdt entered t do loadCascModels()
        on cascTypeDDL selected i do loadCascModels()
        on cascRaceDDL selected i do loadCascModels()
        on cascFilterDDL selected i do loadCascModels()
        
        on cascListBox selected idx do
        (
            if idx > 0 and idx <= cascPaths.count then
            (
                local dp = getCascDP cascPaths[idx]
                local tg = getCascTag cascPaths[idx]
                cascInfoLbl.text = dp + (if tg != "" then ("  -  " + tg) else "")
                importCascBtn.enabled = true
            )
        )
        
        on cascListBox doubleClicked idx do
        (
            if idx > 0 and idx <= cascPaths.count then
                importCascBtn.pressed()
        )
        
        on importCascBtn pressed do
        (
            local idx = cascListBox.selection
            if idx < 1 or idx > cascPaths.count then return OK
            
            local cp = cascPaths[idx]
            cascInfoLbl.text = ::L.t "casc_browser_extracting"
            windows.processPostedMessages()
            
            -- Extract MDX from CASC (textures are found later via CASC search)
            local localPath = ::WhiteoutDexCASC.extractMDX cp
            
            if localPath == undefined or not doesFileExist localPath then
            (
                cascInfoLbl.text = "ERROR: Could not extract MDX"
                return OK
            )
            
            local dp = getCascDP cp
            
            -- Set modelpath for the importer
            modelpath = getFilenamePath localPath
            mode = true
            
            -- Detect version and read
            mdxVersion = peekMDXVersion localPath
            local noError = ::WhiteoutDexReadMDX localPath mode
            
            try ( destroyDialog cascBrowserRoll ) catch()
            
            if noError then
            (
                if mdxVersion >= 1200 then
                (
                    createDialog importerReforgedRoll
                    try ( importerReforgedRoll.searchCASCChk.checked = true ) catch()
                )
                else
                    createDialog importerRoll
            )
        )
        
        on cancelCascBtn pressed do
        (
            destroyDialog cascBrowserRoll
        )
        
        on cascBrowserRoll open do
        (
            cascInfoLbl.text = ::L.t "casc_browser_initializing"
            windows.processPostedMessages()
            
            -- Initialize CASC if not already
            if ::WhiteoutDexCASC != undefined then
            (
                if not ::WhiteoutDexCASC.isInitialized then
                (
                    local cascIni = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
                    local w3dir = getINISetting cascIni "CASC" "W3Path"
                    if w3dir != "" then ::WhiteoutDexCASC.init w3dir:w3dir
                )
                if ::WhiteoutDexCASC.isInitialized then
                (
                    cascReady = true
                    loadCascModels()
                    cascInfoLbl.text = ""
                )
                else
                    cascInfoLbl.text = ::L.t "casc_browser_init_failed"
            )
            else
                cascInfoLbl.text = "WhiteoutDexCASCHelper not loaded"
            
            -- Localization
            if ::L != undefined then
            (
                cascBrowserRoll.title = ::L.t "casc_browser_title"
                cascSearchEdt.tooltip = ::L.t "casc_browser_search_tip"
                importCascBtn.text = ::L.t "casc_browser_import_btn"
                cancelCascBtn.text = ::L.t "casc_browser_cancel_btn"
            )
        )
    )

    -- ============================================================================
    -- LOAD ROLLOUT (File Selector)
    -- ============================================================================
    rollout loadRoll "WhiteoutDex Importer" width:220 height:268
	(
		groupBox modeGrp "Mode" pos:[7,14] width:205 height:68
		radioButtons modeRdo pos:[21,36] width:96 height:32 labels:#("Standard Mode", "Debug Mode") columns:1
		label localLbl "— custom import —" pos:[45,93] width:130 height:14 style_sunkenedge:false
		button openModelBtn "Open Model" pos:[15,110] width:190 height:28
		label separatorLbl "— or import from game files —" pos:[30,148] width:160 height:14 style_sunkenedge:false
		button browseMPQBtn "Browse MPQ" pos:[15,165] width:190 height:28 tooltip:"Browse Warcraft III MPQ archives for models" enabled:false
		label separatorCascLbl "— Reforged —" pos:[75,198] width:80 height:14 style_sunkenedge:false
		button browseCASCBtn "Browse CASC" pos:[15,215] width:190 height:28 tooltip:"Browse Warcraft III Reforged CASC archives for models" enabled:false

		on loadRoll open do
		(
			if ::L != undefined then
			(
				loadRoll.title = ::L.t "imp_whiteoutdex_importer_title_2"
				openModelBtn.text = ::L.t "imp_open_model_btn"
				localLbl.text = ::L.t "imp_custom_import_lbl"
				separatorLbl.text = ::L.t "imp_game_files_lbl"
			    browseMPQBtn.text = ::L.t "mpq_browser_browse_btn"
			    browseMPQBtn.tooltip = ::L.t "mpq_browser_browse_tip"
			    separatorCascLbl.text = ::L.t "imp_reforged_lbl"
			    browseCASCBtn.text = ::L.t "casc_browser_browse_btn"
			    browseCASCBtn.tooltip = ::L.t "casc_browser_browse_tip"
			)
			-- Enable Browse MPQ only if valid MPQ directory is set
			if ::WhiteoutDexMPQ != undefined and ::WhiteoutDexMPQ.validateDirectory() then
				browseMPQBtn.enabled = true
			else
				browseMPQBtn.enabled = false
			-- Enable Browse CASC only if valid CASC directory is set
			local cascIni = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
			local cascDir = getINISetting cascIni "CASC" "W3Path"
			if cascDir != "" and doesFileExist (cascDir + "\\.build.info") then
				browseCASCBtn.enabled = true
			else
				browseCASCBtn.enabled = false
		)

		on openModelBtn pressed do
		(
			local infile_name = getOpenFileName types:"MDX File (*.mdx)|*.mdx"
			if infile_name == undefined then return OK
			
			-- set local macro variables
			modelpath = getFilenamePath infile_name
			mode = modeRdo.state == 1

			-- Detect MDX version before reading
			mdxVersion = peekMDXVersion infile_name

			-- Call the read function from WhiteoutDexImportFunctions.ms
			local noError = ::WhiteoutDexReadMDX infile_name mode

			try destroyDialog loadRoll catch return OK
			if noError then
			(
				if mdxVersion >= 1200 then
					createDialog importerReforgedRoll
				else
					createDialog importerRoll
			)
		)
		
		on browseMPQBtn pressed do
		(
			try ( destroyDialog loadRoll ) catch()
			createDialog mpqBrowserRoll modal:true
		)
		
		on browseCASCBtn pressed do
		(
			try ( destroyDialog loadRoll ) catch()
			createDialog cascBrowserRoll modal:true
		)
	)

	on execute do
	(
		try destroyDialog loadRoll catch return OK
		::WhiteoutDexImportSettings = ImporterSettings()
		createDialog loadRoll
	)
)
