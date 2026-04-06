plugin simpleManipulator Wc3_Event
    name: "Event Object"
    classID:#(0x189dc89e, 0x2e9f652d)
    --classID:#(0x956e6a9b, 0x87f39a9e) For versions 2020 and below
    category:"Warcraft 3 Systems"
(
    local listOfKeys        = #()
    local selectedKey
    local EventMessageTitle = "Warcraft III Event Info"
    local EventMessageText  = "The events will happen on the places where the keys are set.\nThe names of the objects can be found in the official Warcraft III Art Tools Documentation"
    ----------------------------------------------------------------------
    parameters main rollout:params
    (
        keyList type:#IntTab tabSizeVariable:true
    )
    parameters main rollout:General
    (
        adsorption  type:#string ui:Temp_adsorption 
    )
    rollout General "General Functions"
    (
        PickButton btn_adsorption "Select Obj(Adsorb)" pos:[12,7] width:138 height:24
        edittext Temp_adsorption "" pos:[9999,0] width:0 height:0
        on btn_adsorption picked obj do
        (
            try
            (
                selection[1].pos	 = obj.pos
                selection[1].parent	 = obj
                Temp_adsorption.text = obj.name
                btn_adsorption.text	 = "Parent: " + Temp_adsorption.text
            )catch()
        )
        on General open do
        (
            if ::L != undefined then
                General.title = ::L.t "event_general_functions_title"
        )
    )
    rollout params "Event Object"
    (
        group "Event Type"
        ( 
            label l1 "· · · · · · · · Type · · · · · · · ·" pos:[12,26] align:#center width:138
            dropdownlist ddlEventType "" items:#("FPT(FootPrint)", "SPL(BloodSplat)", "UBR(UberSplat)", "SND(Sound)", "SPN(SpawnObject)") pos:[12,44] width:138 selection:1
            label l2 "· · · · · · · · Data · · · · · · · ·" pos:[12,72] align:#center width:138
            dropdownlist ddlEventData "" pos:[12,90] width:138 selection:1
            label l3 "· · · · Custom Data · · · ·" pos:[12,115] align:#center width:138
            edittext edtCustomData "" pos:[12,131] width:138 height:20 tooltip:"Custom event data code or sound path (overrides dropdown when non-empty)"
            button btnConfirmRename "Confirm" pos:[12,157] width:138 height:24
            label huanhangfu3 "" height:0
        )

        group "Basic attribute"
        (
            label lbl1 "Move the animation slider to indicate the position of the key" pos:[12,216] width:138 height:45
            dotNetControl lvNotes "System.Windows.Forms.ListView" pos:[12,258] width:138 height:150
            button btnAddNote    "Add"       pos:[12,418] width:138 height:24
            button btnRemNote    "Remove"    pos:[12,447] width:138 height:24
            button btnRemAllNote "Clear All" pos:[12,476] width:138 height:24
            label huanhangfu2 "" height:0
        )

        -- Builds a localized "CODE - Description" string
        -- Checks ::L for evt_CODE translation, falls back to English
        fn localizedEntry code englishDesc =
        (
            if ::L != undefined then
            (
                local key = "evt_" + code
                local translated = try(::L.t key) catch(undefined)
                if translated != undefined and translated != "" and translated != key then
                    return code + " - " + translated
            )
            code + " - " + englishDesc
        )

        -- =====================================================================
        -- EVENT DATA (Warcraft III Art Tools Documentation Appendices C-F)
        -- =====================================================================

        fn getFootprintData = #(
            localizedEntry "FAL0" "BareSmallLeft",localizedEntry "FAL1" "BareLargeLeft",localizedEntry "FAL2" "BareGiantLeft",localizedEntry "FAL3" "BareHugeLeft",
            localizedEntry "FAR0" "BareSmallRight",localizedEntry "FAR1" "BareLargeRight",localizedEntry "FAR2" "BareGiantRight",localizedEntry "FAR3" "BareHugeRight",
            localizedEntry "FBL0" "BootSmallLeft0",localizedEntry "FBL1" "BootLargeLeft0",localizedEntry "FBL2" "BootSmallLeft1",localizedEntry "FBL3" "BootLargeLeft1",localizedEntry "FBL4" "BootGiantLeft1",
            localizedEntry "FBR0" "BootSmallRight0",localizedEntry "FBR1" "BootLargeRight0",localizedEntry "FBR2" "BootSmallRight1",localizedEntry "FBR3" "BootLargeRight1",localizedEntry "FBR4" "BootGiantRight1",
            localizedEntry "FCL0" "ClovenSmallLeft",localizedEntry "FCR0" "ClovenSmallRight",localizedEntry "FCL1" "ClovenLargeLeft",localizedEntry "FCR1" "ClovenLargeRight",
            localizedEntry "FCL2" "ClovenReallySmallLeft",localizedEntry "FCR2" "ClovenReallySmallRight",localizedEntry "FCL3" "ClovenXtraLargeLeft",localizedEntry "FCR3" "ClovenXtraLargeRight",
            localizedEntry "FHL0" "HorseSmallLeft",localizedEntry "FHL1" "HorseLargeLeft",localizedEntry "FHR0" "HorseSmallRight",localizedEntry "FHR1" "HorseLargeRight",
            localizedEntry "FPL0" "PawLeft0",localizedEntry "FPL1" "PawLargeLeft",localizedEntry "FPR0" "PawRight0",localizedEntry "FPR1" "PawLargeRight",localizedEntry "FPL2" "PawBearLeft",localizedEntry "FPR2" "PawBearRight",
            localizedEntry "FRL0" "RootLeft",localizedEntry "FRR0" "RootRight",localizedEntry "FRL1" "RootLeftSmall",localizedEntry "FRR1" "RootRightSmall",
            localizedEntry "FTL0" "TrollLeft",localizedEntry "FTR0" "TrollRight",
            localizedEntry "FWL0" "WheelLeft0",localizedEntry "FWR0" "WheelRight0",localizedEntry "FWL1" "WheelLeft1",localizedEntry "FWR1" "WheelRight1",
            localizedEntry "FML0" "MurlocLeft",localizedEntry "FMR0" "MurlocRight",
            localizedEntry "FSL0" "SkeletonRight",localizedEntry "FSR0" "SkeletonLeft",localizedEntry "FSL1" "SkeletonGiantRight",localizedEntry "FSR1" "SkeletonGiantLeft",
            localizedEntry "FFL0" "FlameLeft",localizedEntry "FFR0" "FlameRight",localizedEntry "FFL1" "FlameGiantLeft",localizedEntry "FFR1" "FlameGiantRight",
            localizedEntry "FKL0" "SpikeLeft",localizedEntry "FKR0" "SpikeRight",localizedEntry "FKL1" "SpikeLargeLeft",localizedEntry "FKR1" "SpikeLargeRight",localizedEntry "FKL2" "SpikeGiantLeft",localizedEntry "FKR2" "SpikeGiantRight",
            localizedEntry "FLSL" "LizzardSmallLeft",localizedEntry "FLSR" "LizzardSmallRight",localizedEntry "FLLL" "LizzardLargeLeft",localizedEntry "FLLR" "LizzardLargeRight",
            localizedEntry "FSSL" "SnakeSmallLeft",localizedEntry "FSSR" "SnakeSmallRight",localizedEntry "FSLL" "SnakeLargeLeft",localizedEntry "FSLR" "SnakeLargeRight",
            localizedEntry "FDSL" "DragSmallLeft",localizedEntry "FDSR" "DragSmallRight",localizedEntry "FDLL" "DragLargeLeft",localizedEntry "FDLR" "DragLargeRight"
        )

        fn getSplatData = #(
            localizedEntry "HBL0" "HumanBloodLarge0",localizedEntry "HBL1" "HumanBloodLarge1",localizedEntry "HBL2" "HumanBloodLarge2",localizedEntry "HBL3" "HumanBloodLarge3",
            localizedEntry "HBS0" "HumanBloodSmall0",localizedEntry "HBS1" "HumanBloodSmall1",localizedEntry "HBS2" "HumanBloodSmall2",localizedEntry "HBS3" "HumanBloodSmall3",
            localizedEntry "OBL0" "OrcBloodLarge0",localizedEntry "OBL1" "OrcBloodLarge1",localizedEntry "OBL2" "OrcBloodLarge2",localizedEntry "OBL3" "OrcBloodLarge3",
            localizedEntry "OBS0" "OrcBloodSmall0",localizedEntry "OBS1" "OrcBloodSmall1",localizedEntry "OBS2" "OrcBloodSmall2",localizedEntry "OBS3" "OrcBloodSmall3",
            localizedEntry "UBL0" "UndeadBloodLarge0",localizedEntry "UBL1" "UndeadBloodLarge1",localizedEntry "UBL2" "UndeadBloodLarge2",localizedEntry "UBL3" "UndeadBloodLarge3",
            localizedEntry "UBS0" "UndeadBloodSmall0",localizedEntry "UBS1" "UndeadBloodSmall1",localizedEntry "UBS2" "UndeadBloodSmall2",localizedEntry "UBS3" "UndeadBloodSmall3",
            localizedEntry "DBL0" "DemonBloodLarge0",localizedEntry "DBL1" "DemonBloodLarge1",localizedEntry "DBL2" "DemonBloodLarge2",localizedEntry "DBL3" "DemonBloodLarge3",
            localizedEntry "DBS0" "DemonBloodSmall0",localizedEntry "DBS1" "DemonBloodSmall1",localizedEntry "DBS2" "DemonBloodSmall2",localizedEntry "DBS3" "DemonBloodSmall3",
            localizedEntry "EBL0" "NightElfBloodLarge0",localizedEntry "EBL1" "NightElfBloodLarge1",localizedEntry "EBL2" "NightElfBloodLarge2",localizedEntry "EBL3" "NightElfBloodLarge3",
            localizedEntry "EBS0" "NightElfBloodSmall0",localizedEntry "EBS1" "NightElfBloodSmall1",localizedEntry "EBS2" "NightElfBloodSmall2",localizedEntry "EBS3" "NightElfBloodSmall3",
            localizedEntry "WSL0" "WaterSplashLarge0",localizedEntry "WSL1" "WaterSplashLarge1",localizedEntry "WSS0" "WaterSplashSmall0",localizedEntry "WSS1" "WaterSplashSmall1",
            localizedEntry "WHL0" "WaterHumanBloodLarge0",localizedEntry "WHL1" "WaterHumanBloodLarge1",localizedEntry "WHS0" "WaterHumanBloodSmall0",localizedEntry "WHS1" "WaterHumanBloodSmall1",
            localizedEntry "WOL0" "WaterOrcBloodLarge0",localizedEntry "WOL1" "WaterOrcBloodLarge1",localizedEntry "WOS0" "WaterOrcBloodSmall0",localizedEntry "WOS1" "WaterOrcBloodSmall1",
            localizedEntry "WEL0" "WaterNEBloodLarge0",localizedEntry "WEL1" "WaterNEBloodLarge1",localizedEntry "WES0" "WaterNEBloodSmall0",localizedEntry "WES1" "WaterNEBloodSmall1",
            localizedEntry "WUL0" "WaterUndeadBloodLarge0",localizedEntry "WUL1" "WaterUndeadBloodLarge1",localizedEntry "WUS0" "WaterUndeadBloodSmall0",localizedEntry "WUS1" "WaterUndeadBloodSmall1",
            localizedEntry "WDL0" "WaterDemonBloodLarge0",localizedEntry "WDL1" "WaterDemonBloodLarge1",localizedEntry "WDS0" "WaterDemonBloodSmall0",localizedEntry "WDS1" "WaterDemonBloodSmall1",
            localizedEntry "WSX0" "WaterSplashOnly0",localizedEntry "WSX1" "WaterSplashOnly1"
        )

        fn getUberSplatData = #(
            localizedEntry "LSDS" "LordSummerDirtSmall",localizedEntry "LSDM" "LordSummerDirtMed",localizedEntry "LSDL" "LordSummerDirtLarge",
            localizedEntry "HCRT" "HumanCrater",localizedEntry "UDSU" "UndeadUberSplat",localizedEntry "DNCS" "DeathNeutralCitySmall",
            localizedEntry "HMTP" "HumanMassTeleport",localizedEntry "SCTP" "ScrollOfTownPortal",localizedEntry "AMRC" "AmuletOfRecall",localizedEntry "DRKC" "DarkConversion",
            localizedEntry "DOSB" "DeathOrcSmallBldg",localizedEntry "DOMB" "DeathOrcMedBldg",localizedEntry "DOLB" "DeathOrcLargeBldg",
            localizedEntry "DHSB" "DeathHumanSmallBldg",localizedEntry "DHMB" "DeathHumanMedBldg",localizedEntry "DHLB" "DeathHumanLargeBldg",
            localizedEntry "DUSB" "DeathUndeadSmallBldg",localizedEntry "DUMB" "DeathUndeadMedBldg",localizedEntry "DULB" "DeathUndeadLargeBldg",
            localizedEntry "DNSB" "DeathNESmallBldg",localizedEntry "DNMB" "DeathNEMedBldg",localizedEntry "DNSA" "DeathNESmallAncient",localizedEntry "DNMA" "DeathNEMedAncient",
            localizedEntry "HSMA" "HumanUberSmall",localizedEntry "HMED" "HumanUberMed",localizedEntry "HLAR" "HumanUberLarge",
            localizedEntry "OSMA" "OrcUberSmall",localizedEntry "OMED" "OrcUberMed",localizedEntry "OLAR" "OrcUberLarge",
            localizedEntry "USMA" "UndeadUberSmall",localizedEntry "UMED" "UndeadUberMed",localizedEntry "ULAR" "UndeadUberLarge",
            localizedEntry "ESMA" "AncientUberSmall",localizedEntry "EMDA" "AncientUberMed",localizedEntry "ESMB" "NightElfUberSmall",localizedEntry "EMDB" "NightElfUberMed",
            localizedEntry "HTOW" "TownHallUber",localizedEntry "HCAS" "CastleUber",localizedEntry "NGOL" "GoldmineUber",
            localizedEntry "THND" "ThunderClap",localizedEntry "NDGS" "DemonGateSplat",localizedEntry "CLTS" "ThornyShieldSplat",
            localizedEntry "HFS1" "FlameStrike1",localizedEntry "HFS2" "FlameStrike2",localizedEntry "USBR" "Burrow",
            localizedEntry "NLAR" "NagaUberLarge",localizedEntry "NMED" "NagaUberMed",localizedEntry "DPSW" "DarkPortalSW",localizedEntry "DPSE" "DarkPortalSE"
        )

        fn getSoundData = #(
            -- Ability / Spell Sounds
            localizedEntry "AAMS" "AntiMagicshell",
            localizedEntry "AAST" "AncestralSpirit",
            localizedEntry "AAVE" "ObsidianStatueMorph",
            localizedEntry "ABLO" "Bloodlust",
            localizedEntry "ABPD" "BothGlueScreenPopDown",
            localizedEntry "ABPU" "BothGlueScreenPopUp",
            localizedEntry "ABRW" "Burrow",
            localizedEntry "ABSK" "BerserkerRage",
            localizedEntry "ABTR" "BattleRoar",
            localizedEntry "ACAN" "Cannibalize",
            localizedEntry "ACBC" "BreathOfFire",
            localizedEntry "ACBF" "BreathOfFrost",
            localizedEntry "ACCV" "CrushingWave",
            localizedEntry "ACLB" "CinematicLightningBolt",
            localizedEntry "ACRH" "CorrosiveBreathMissileHit",
            localizedEntry "ACRI" "Cripple",
            localizedEntry "ACRL" "CorrosiveBreathMissileLaunch",
            localizedEntry "ACRS" "Curse",
            localizedEntry "ACSI" "Silence",
            localizedEntry "ACSL" "CreepSleep",
            localizedEntry "ACWD" "CrushingWaveDamage",
            localizedEntry "ACYB" "CycloneBirth",
            localizedEntry "ACYD" "CycloneDeath",
            localizedEntry "ADCA" "DruidOfTheClawMorphAlternate",
            localizedEntry "ADCM" "DruidOfTheClawMorph",
            localizedEntry "ADEF" "Defend",
            localizedEntry "ADEV" "Devour",
            localizedEntry "ADHM" "DemonHunterMorph",
            localizedEntry "ADIS" "DispelMagic",
            localizedEntry "ADTA" "DruidOfTheTalonMorphAlternate",
            localizedEntry "ADTM" "DruidOfTheTalonMorph",
            localizedEntry "ADVP" "DevourPuke",
            localizedEntry "AEAT" "EatTreeMunch",
            localizedEntry "AEBA" "Barkskin",
            localizedEntry "AEBD" "Earthbind",
            localizedEntry "AEBL" "BlinkCaster",
            localizedEntry "AEBT" "BlinkTarget",
            localizedEntry "AEFK" "FanOfKnives",
            localizedEntry "AEFN" "ForceOfNatureBirth",
            localizedEntry "AEID" "ImmolationDecay",
            localizedEntry "AEIM" "ImmolationTarget",
            localizedEntry "AEMB" "EntangleMineBirth",
            localizedEntry "AEMD" "EntangleMineDeath",
            localizedEntry "AEMS" "EntangleMineStand",
            localizedEntry "AENA" "NatureTouch",
            localizedEntry "AENM" "EnsnareMissile",
            localizedEntry "AENS" "Ensnare",
            localizedEntry "AEQK" "Earthquake",
            localizedEntry "AERD" "EntanglingRootsDecay",
            localizedEntry "AERV" "ReviveNightElf",
            localizedEntry "AESC" "ScoutBirth",
            localizedEntry "AESD" "ScoutDeath",
            localizedEntry "AESE" "Sleep",
            localizedEntry "AESF" "StarfallArea",
            localizedEntry "AESH" "ShadowStrikeBirth",
            localizedEntry "AESM" "ShadowStrikeMissileBirth",
            localizedEntry "AESO" "ManaBurn",
            localizedEntry "AEST" "StarfallTarget",
            localizedEntry "AESV" "EntanglingRoots",
            localizedEntry "AETQ" "Tranquility",
            localizedEntry "AETT" "TranquilityTarget",
            localizedEntry "AEYE" "SentryWardBirth",
            localizedEntry "AFAE" "FaerieFire",
            localizedEntry "AFBK" "Feedback",
            localizedEntry "AFL1" "Flare1",
            localizedEntry "AFL2" "Flare2",
            localizedEntry "AFL3" "Flare3",
            localizedEntry "AFL4" "Flare4",
            localizedEntry "AFLK" "FlakCannonHit",
            localizedEntry "AFRZ" "FreezingBreath",
            localizedEntry "AFSH" "FragShards",
            localizedEntry "AGB1" "OrcGlueBear1",
            localizedEntry "AGB2" "OrcGlueBear2",
            localizedEntry "AGB3" "OrcGlueBear3",
            localizedEntry "AGBC" "GlueScreenBNetClose",
            localizedEntry "AGBE" "GlueScreenBNetExit",
            localizedEntry "AGBO" "GlueScreenBNetOpen",
            localizedEntry "AGBS" "GlueScreenBNetSlam",
            localizedEntry "AGEQ" "GlueScreenEarthquake",
            localizedEntry "AGMH" "GlueScreenMeteorHit",
            localizedEntry "AGMT" "GlueScreenMeteorLaunch",
            localizedEntry "AGVB" "GraveMarkerBirth",
            localizedEntry "AGVD" "GraveMarkerDeath",
            localizedEntry "AHAV" "Avatar",
            localizedEntry "AHBZ" "Blizzard",
            localizedEntry "AHDD" "DivineShieldDeath",
            localizedEntry "AHDI" "DivineShield",
            localizedEntry "AHDR" "SiphonManaCaster",
            localizedEntry "AHEA" "Heal",
            localizedEntry "AHER" "LevelUp",
            localizedEntry "AHFS" "FlameStrike",
            localizedEntry "AHFT" "FlameStrikeTarget",
            localizedEntry "AHHB" "HolyBolt",
            localizedEntry "AHMC" "MarkOfChaos",
            localizedEntry "AHMF" "ManaFlareMissile",
            localizedEntry "AHMT" "MassTeleport",
            localizedEntry "AHRE" "Resurrect",
            localizedEntry "AHRV" "ReviveHuman",
            localizedEntry "AHSL" "StormBoltLaunch",
            localizedEntry "AHTB" "StormBolt",
            localizedEntry "AHTC" "ThunderClap",
            localizedEntry "AHWD" "HealingWardBirth",
            localizedEntry "AICB" "OrbOfCorruptionLaunch",
            localizedEntry "AICH" "OrbOfCorruptionHit",
            localizedEntry "AIDC" "NeutralizationWandHit",
            localizedEntry "AIFT" "FinalCinematic",
            localizedEntry "AILL" "ItemIllusion",
            localizedEntry "AIMA" "ManaPotion",
            localizedEntry "AINB" "InfernalBirth",
            localizedEntry "AINF" "InnerFire",
            localizedEntry "AIRE" "RestorationPotion",
            localizedEntry "AISO" "SoulGem",
            localizedEntry "AITM" "Tome",
            localizedEntry "AIVS" "Invisibility",
            localizedEntry "AKDL" "KodoDrumLeft",
            localizedEntry "AKDR" "KodoDrumRight",
            localizedEntry "ALPD" "LeftGlueScreenPopDown",
            localizedEntry "ALPU" "LeftGlueScreenPopUp",
            localizedEntry "ALSD" "LightningShield",
            localizedEntry "ANBA" "BlackArrowHit",
            localizedEntry "ANDO" "DoomTarget",
            localizedEntry "ANDT" "RevealMap",
            localizedEntry "ANEU" "NeutralBuildingActivate",
            localizedEntry "ANHT" "HowlOfTerror",
            localizedEntry "ANMO" "MonsoonBolt",
            localizedEntry "ANPA" "Parasite",
            localizedEntry "ANSA" "SacrificeUnit",
            localizedEntry "ANSD" "StrongDrink",
            localizedEntry "ANSM" "StrongDrinkMissile",
            localizedEntry "ANSS" "SpellShieldAmulet",
            localizedEntry "AOAG" "WardBirth",
            localizedEntry "AOCR" "CriticalStrike",
            localizedEntry "AOHW" "HealingWaveTarget",
            localizedEntry "AOLB" "LightningBolt",
            localizedEntry "AOMC" "MirrorImage",
            localizedEntry "AOMI" "MirrorImageDeath",
            localizedEntry "AORE" "Reincarnation",
            localizedEntry "AORV" "ReviveOrc",
            localizedEntry "AOSD" "FeralSpiritDone",
            localizedEntry "AOSF" "FeralSpiritTarget",
            localizedEntry "AOSH" "ShockWave",
            localizedEntry "AOVD" "VoodooBirth",
            localizedEntry "AOWS" "Warstomp",
            localizedEntry "AOWW" "Whirlwind",
            localizedEntry "APHS" "PhaseShift",
            localizedEntry "APHX" "PhoenixBirth",
            localizedEntry "APLA" "PolymorphAir",
            localizedEntry "APLD" "PolymorphDone",
            localizedEntry "APOL" "Polymorph",
            localizedEntry "APRG" "Purge",
            localizedEntry "APSH" "PossessionMissileHit",
            localizedEntry "APSL" "PossessionMissileLaunch",
            localizedEntry "APXB" "PhoenixEggBirth",
            localizedEntry "AREJ" "Rejuvenation",
            localizedEntry "AREP" "Repair",
            localizedEntry "AROO" "Root",
            localizedEntry "ARPD" "RightGlueScreenPopDown",
            localizedEntry "ARPU" "RightGlueScreenPopUp",
            localizedEntry "ASHP" "ShadowPact",
            localizedEntry "ASKA" "RaiseSkeletonArcher",
            localizedEntry "ASKW" "RaiseSkeletonWarrior",
            localizedEntry "ASLC" "SlowCaster",
            localizedEntry "ASLO" "Slow",
            localizedEntry "ASPL" "SpiritLink",
            localizedEntry "ASPM" "SpellStealMissileLaunch",
            localizedEntry "ASPS" "SpellStealTarget",
            localizedEntry "AST2" "StoneFormMorph2",
            localizedEntry "AST3" "StoneFormMorph3",
            localizedEntry "ASTA" "StoneFormMorphAlternate",
            localizedEntry "ASTB" "StasisTotemBirth",
            localizedEntry "ASTH" "StampedeHit",
            localizedEntry "ASTO" "StoneFormMorph1",
            localizedEntry "ASTS" "StasisTotemDeath",
            localizedEntry "ASWB" "SpiritWolfBirth",
            localizedEntry "ASWE" "WaterElementalBirth",
            localizedEntry "ATAU" "Taunt",
            localizedEntry "ATRB" "TreeWallBirth",
            localizedEntry "AUB1" "UndeadBuildingBirth1",
            localizedEntry "AUB2" "UndeadBuildingBirth2",
            localizedEntry "AUB3" "UndeadBuildingBirth3",
            localizedEntry "AUB4" "UndeadBuildingBirth4",
            localizedEntry "AUCB" "ScarabBirth",
            localizedEntry "AUCD" "CarrionSwarmDamage",
            localizedEntry "AUCH" "Charm",
            localizedEntry "AUCO" "UnstableConcoction",
            localizedEntry "AUCS" "CarrionSwarmLaunch",
            localizedEntry "AUDA" "DarkRitual",
            localizedEntry "AUDC" "DeathCoil",
            localizedEntry "AUDM" "DarkSummoningMissileLaunch",
            localizedEntry "AUDP" "DeathPactTarget",
            localizedEntry "AUDS" "DarkSummoningTarget",
            localizedEntry "AUDT" "DeathAndDecayTarget",
            localizedEntry "AUFA" "FrostArmor",
            localizedEntry "AUFN" "FrostNova",
            localizedEntry "AUGA" "GatherShadowsMorphAlternate",
            localizedEntry "AUGS" "GatherShadowsMorph",
            localizedEntry "AUHF" "UnholyFrenzy",
            localizedEntry "AUIH" "ImpaleLand",
            localizedEntry "AUIM" "Impale",
            localizedEntry "AUIT" "ImpaleHit",
            localizedEntry "AUPR" "Uproot",
            localizedEntry "AURV" "ReviveUndead",
            localizedEntry "AWBS" "BigWaterStep",
            localizedEntry "AWEB" "Web",
            localizedEntry "AWRS" "Pulverize",
            localizedEntry "AWST" "WaterStep",
            -- Death Sounds
            localizedEntry "DABA" "AbominationAlternateDeath",
            localizedEntry "DABO" "AbominationDeath",
            localizedEntry "DACO" "AcolyteDeath",
            localizedEntry "DADR" "DruidOfTheTalonDeath",
            localizedEntry "DALB" "AlbatrossDeath",
            localizedEntry "DAMG" "HeroArchMageDeath",
            localizedEntry "DANG" "WardDeath",
            localizedEntry "DANP" "TreantDeath",
            localizedEntry "DARC" "ArachnathidDeath",
            localizedEntry "DARG" "ArmorGolemDeath",
            localizedEntry "DART" "ArtilleryExplodeDeath",
            localizedEntry "DASS" "AssassinDeath",
            localizedEntry "DBAL" "BallistaDeath",
            localizedEntry "DBAN" "BansheeDeath",
            localizedEntry "DBAT" "BatRiderDeath",
            localizedEntry "DBES" "HeroBloodElfDeath",
            localizedEntry "DBLA" "HeroBladeMasterDeath",
            localizedEntry "DBNT" "BanditDeath",
            localizedEntry "DBRG" "DeathBridge",
            localizedEntry "DBRI" "BristleBackDeath",
            localizedEntry "DBSF" "BlackStagFemaleDeath",
            localizedEntry "DBSM" "BlackStagMaleDeath",
            localizedEntry "DBSP" "BattleShipDeath",
            localizedEntry "DBSX" "ObsidianAvengerDeath",
            localizedEntry "DBTM" "BeastmasterDeath",
            localizedEntry "DCAT" "CatapultDeath",
            localizedEntry "DCBL" "DeathCityBuilding",
            localizedEntry "DCEN" "CentaurDeath",
            localizedEntry "DCNA" "CentaurArcherDeath",
            localizedEntry "DCRL" "HeroCryptLordDeath",
            localizedEntry "DDCA" "DruidOfTheClawDeathAlternate",
            localizedEntry "DDEM" "HeroDemonHunterDeath",
            localizedEntry "DDHK" "DragonHawkDeath",
            localizedEntry "DDKN" "HeroDeathKnightDeath",
            localizedEntry "DDKR" "DarkRangerDeath",
            localizedEntry "DDMA" "HeroDemonHunterDeathAlternate",
            localizedEntry "DDMG" "DoomGuardDeath",
            localizedEntry "DDMN" "DemonessDeath",
            localizedEntry "DDNW" "DuneWormDeath",
            localizedEntry "DDOC" "DruidOfTheClawDeath",
            localizedEntry "DDRA" "DragonDeath",
            localizedEntry "DDRL" "HeroDreadLordDeath",
            localizedEntry "DDRN" "DraeneiDeath",
            localizedEntry "DDRS" "DragonspawnDeath",
            localizedEntry "DDRY" "DryadDeath",
            localizedEntry "DDSH" "HumanDissipate",
            localizedEntry "DDSN" "NightElfDissipate",
            localizedEntry "DDSO" "OrcDissipate",
            localizedEntry "DDTA" "DruidOfTheTalonDeathAlternate",
            localizedEntry "DDWF" "DireWolfDeath",
            localizedEntry "DEBA" "DeathWalkingNightElfBuilding",
            localizedEntry "DEBC" "DeathNightElfBuildingCancel",
            localizedEntry "DEGS" "EggSackDeath",
            localizedEntry "DELB" "DeathNightElfLargeBuilding",
            localizedEntry "DELS" "DeathNightElfSmallBuilding",
            localizedEntry "DENT" "EntDeath",
            localizedEntry "DFAR" "HeroFarSeerDeath",
            localizedEntry "DFCO" "FacelessOneDeath",
            localizedEntry "DFDR" "FaerieDragonDeath",
            localizedEntry "DFEL" "FelhoundDeath",
            localizedEntry "DFLG" "FelguardDeath",
            localizedEntry "DFOO" "FootmanDeath",
            localizedEntry "DFOR" "ForgottenOneDeath",
            localizedEntry "DFRG" "FrogDeath",
            localizedEntry "DFRM" "FrostmourneDeath",
            localizedEntry "DFRT" "ForestTrollDeath",
            localizedEntry "DFRW" "FrostWyrmDeath",
            localizedEntry "DFSP" "ForestTrollShadowPriestDeath",
            localizedEntry "DFTN" "ForgottenOneTentacleDeath",
            localizedEntry "DFUR" "FurbolgDeath",
            localizedEntry "DGAR" "GargoyleDeath",
            localizedEntry "DGAS" "GargoyleStoneDeath",
            localizedEntry "DGAT" "GateDeath",
            localizedEntry "DGHO" "GhoulDeath",
            localizedEntry "DGLD" "GoldMineDeath",
            localizedEntry "DGLM" "GoblinLandMineDeath",
            localizedEntry "DGNA" "GnollArcherDeath",
            localizedEntry "DGNL" "GnollDeath",
            localizedEntry "DGOB" "GemstoneObeliskDeath",
            localizedEntry "DGRS" "BearSwimDeath",
            localizedEntry "DGRU" "GruntDeath",
            localizedEntry "DGRY" "GryphonRiderDeath",
            localizedEntry "DGRZ" "GrizzlyBearDeath",
            localizedEntry "DGSD" "GoblinSapperDeath",
            localizedEntry "DGSP" "GoblinSapperExplode",
            localizedEntry "DGST" "GiantSeaTurtleDeath",
            localizedEntry "DGYR" "GyrocopterDeath",
            localizedEntry "DGZP" "GoblinZeppelinDeath",
            localizedEntry "DHBC" "DeathHumanBuildingCancel",
            localizedEntry "DHDA" "HydraDeath",
            localizedEntry "DHDS" "HydraDeathSwim",
            localizedEntry "DHIP" "HippogryphDeath",
            localizedEntry "DHLS" "DeathHumanSmallBuilding",
            localizedEntry "DHMC" "HermitCrabDeath",
            localizedEntry "DHOR" "HorseDeath",
            localizedEntry "DHRP" "HarpyDeath",
            localizedEntry "DHUN" "HeadHunterDeath",
            localizedEntry "DHWD" "HealingWardDeath",
            localizedEntry "DHYD" "HydraliskDeath",
            localizedEntry "DICT" "IceTrollDeath",
            localizedEntry "DINF" "InfernalDeath",
            localizedEntry "DINM" "InfernalMachineDeath",
            localizedEntry "DIPW" "PrisonWagonDeath",
            localizedEntry "DIRG" "IronGolemDeath",
            localizedEntry "DJAN" "JainaDeath",
            localizedEntry "DKBS" "KoboldShovelerDeath",
            localizedEntry "DKEE" "HeroKeeperOfTheGroveDeath",
            localizedEntry "DKNI" "KnightDeath",
            localizedEntry "DKOB" "KoboldDeath",
            localizedEntry "DKOD" "KodoBeastDeath",
            localizedEntry "DLIC" "HeroLichDeath",
            localizedEntry "DLOC" "LocustDeath",
            localizedEntry "DLVR" "LeverDeath",
            localizedEntry "DMAG" "MagnataurDeath",
            localizedEntry "DMAK" "MakruraDeath",
            localizedEntry "DMAM" "MammothDeath",
            localizedEntry "DMGS" "MurgulDeathSwim",
            localizedEntry "DMGT" "MountainGiantDeath",
            localizedEntry "DMKG" "HeroMountainKingDeath",
            localizedEntry "DMLF" "MalfurionDeath",
            localizedEntry "DMOO" "HeroMoonPriestessDeath",
            localizedEntry "DMOR" "MortarTeamDeath",
            localizedEntry "DMTW" "MeatWagonDeath",
            localizedEntry "DMUR" "MurlocDeath",
            localizedEntry "DMYR" "NagaMyrmidonDeath",
            localizedEntry "DMYS" "NagaMyrmidonDeathSwim",
            localizedEntry "DNDR" "NetherDragonDeath",
            localizedEntry "DNDS" "HumanDissipate",
            localizedEntry "DNEC" "NecromancerDeath",
            localizedEntry "DNSR" "NagaSirenDeath",
            localizedEntry "DNSS" "NagaSirenDeathSwim",
            localizedEntry "DNSW" "NagaSeaWitchDeath",
            localizedEntry "DNWS" "NagaSeaWitchDeathSwim",
            localizedEntry "DOBS" "ObsidianStatueDeath",
            localizedEntry "DOGR" "OgreDeath",
            localizedEntry "DOLS" "DeathOrcSmallBuilding",
            localizedEntry "DORW" "OrcWarlockDeath",
            localizedEntry "DOWB" "OwlbearDeath",
            localizedEntry "DOWL" "SnowOwlDeath",
            localizedEntry "DPAL" "HeroPaladinDeath",
            localizedEntry "DPEN" "PenguinDeath",
            localizedEntry "DPEO" "PeonDeath",
            localizedEntry "DPES" "PeasantDeath",
            localizedEntry "DPHX" "PhoenixDeath",
            localizedEntry "DPIG" "PigDeath",
            localizedEntry "DPIT" "CryptFiendDeath",
            localizedEntry "DPLD" "PitlordDeath",
            localizedEntry "DPMB" "PandarenBrewmasterDeath",
            localizedEntry "DPRS" "PriestDeath",
            localizedEntry "DQBS" "QuillBeastDeath",
            localizedEntry "DRAI" "RaiderDeath",
            localizedEntry "DRAN" "ArcherDeath",
            localizedEntry "DRAT" "RatDeath",
            localizedEntry "DREV" "RevenantDeath",
            localizedEntry "DRHG" "RiddenHippogryphDeath",
            localizedEntry "DRIF" "RiflemanDeath",
            localizedEntry "DRKG" "RockGolemDeath",
            localizedEntry "DRKW" "RockWallDeath",
            localizedEntry "DSAT" "SatyrDeath",
            localizedEntry "DSCB" "ScarabDeath",
            localizedEntry "DSEL" "SealDeath",
            localizedEntry "DSEN" "SentinelDeath",
            localizedEntry "DSGT" "SeaGiantDeath",
            localizedEntry "DSGW" "SeaGiantSwimDeath",
            localizedEntry "DSHD" "ShadeDeath",
            localizedEntry "DSHH" "HeroShadowHunterDeath",
            localizedEntry "DSHM" "ShamanDeath",
            localizedEntry "DSHP" "SheepDeath",
            localizedEntry "DSHW" "SheepDeathSwim",
            localizedEntry "DSKE" "SkeletonDeath",
            localizedEntry "DSKK" "SkinkDeath",
            localizedEntry "DSLG" "SludgeMonsterDeath",
            localizedEntry "DSND" "SnapDragonDeath",
            localizedEntry "DSNS" "SnapDragonDeathSwim",
            localizedEntry "DSOR" "SorceressDeath",
            localizedEntry "DSPB" "SpellBreakerDeath",
            localizedEntry "DSPC" "SpiderCrabDeath",
            localizedEntry "DSPD" "SpiderDeath",
            localizedEntry "DSPL" "SplatDeath",
            localizedEntry "DSPV" "SpiritOfVengeanceDeath",
            localizedEntry "DSTT" "SteamTankDeath",
            localizedEntry "DTAU" "TaurenDeath",
            localizedEntry "DTCH" "HeroTaurenChieftainDeath",
            localizedEntry "DTDS" "GiantSeaTurtleDeathSwim",
            localizedEntry "DTRW" "TreeWallDeath",
            localizedEntry "DTUS" "TuskarrDeath",
            localizedEntry "DUAB" "UndeadAirBargeDeath",
            localizedEntry "DUBC" "DeathUndeadBuildingCancel",
            localizedEntry "DUDS" "UndeadDissipate",
            localizedEntry "DULS" "DeathUndeadSmallBuilding",
            localizedEntry "DUNB" "UnbrokenDeath",
            localizedEntry "DVLC" "VillagerChildDeath",
            localizedEntry "DVLM" "VillagerManDeath",
            localizedEntry "DVLW" "VillagerWomanDeath",
            localizedEntry "DVNG" "VengeanceDeath",
            localizedEntry "DVUL" "VultureDeath",
            localizedEntry "DWAR" "WarlockDeath",
            localizedEntry "DWAT" "WaterElementalDeath",
            localizedEntry "DWCD" "WyvernCageDeath",
            localizedEntry "DWDS" "WingedSerpentDeath",
            localizedEntry "DWEN" "WendigoDeath",
            localizedEntry "DWIT" "WitchDoctorDeath",
            localizedEntry "DWLD" "WarlordDeath",
            localizedEntry "DWLF" "WolfDeath",
            localizedEntry "DWRD" "HeroWardenDeath",
            localizedEntry "DWRE" "WarEagleDeath",
            localizedEntry "DWSP" "WispDeath",
            localizedEntry "DWTC" "WatcherDeath",
            localizedEntry "DWYV" "WyvernRiderDeath",
            localizedEntry "DZOM" "ZombieDeath",
            -- Footstep Sounds
            localizedEntry "FBCL" "TestFootstep",
            localizedEntry "FBCR" "TestFootstep",
            localizedEntry "FDFL" "DeepFootstep",
            localizedEntry "FDFR" "DeepFootstep",
            localizedEntry "FHCL" "HeroCinematicStep",
            localizedEntry "FHCR" "HeroCinematicStep",
            -- Glue Screen / UI Sounds
            localizedEntry "GSMN" "ExpansionGlueMonster",
            -- Attack Sounds
            localizedEntry "KANG" "AncestralGuardianAttack1",
            localizedEntry "KANP" "AncientProtectorMissileAttack",
            localizedEntry "KAOE" "AncientOfTheEarthAttack1",
            localizedEntry "KAOM" "AncientOfTheMoonAttack1",
            localizedEntry "KAP1" "AncientProtectorMeleeAttack1",
            localizedEntry "KAP2" "AncientProtectorMeleeAttack2",
            localizedEntry "KAW1" "AncientOfTheWildAttack1",
            localizedEntry "KAW2" "AncientOfTheWildAttack2",
            localizedEntry "KAZB" "AzureDragonAttack1",
            localizedEntry "KBAL" "BalrogAttack1",
            localizedEntry "KBLB" "BlackDragonAttack1",
            localizedEntry "KBLL" "BallistaAttack",
            localizedEntry "KBM1" "HeroBladeMasterAttack1",
            localizedEntry "KBM2" "HeroBladeMasterAttack2",
            localizedEntry "KBRB" "BronzeDragonAttack1",
            localizedEntry "KBST" "BeastmasterAttack",
            localizedEntry "KCAN" "CannonTowerAttack",
            localizedEntry "KCAT" "CatapultAttack1",
            localizedEntry "KCL1" "CryptLordAttack1",
            localizedEntry "KCL2" "CryptLordAttack2",
            localizedEntry "KDH1" "HeroDemonHunterAttack1",
            localizedEntry "KDH2" "HeroDemonHunterAttack2",
            localizedEntry "KDK1" "HeroDeathKnightAttack1",
            localizedEntry "KFAR" "HeroFarSeerAttack1",
            localizedEntry "KFRB" "FrostWyrmAttack1",
            localizedEntry "KGRB" "GreenDragonAttack1",
            localizedEntry "KGUA" "GuardTowerAttack",
            localizedEntry "KGYR" "GyrocopterAttack",
            localizedEntry "KIN1" "InfernalAttack1",
            localizedEntry "KIN2" "InfernalAttack2",
            localizedEntry "KINJ" "InfernalJuggernaughtAttack",
            localizedEntry "KINM" "InfernalMachineAttack",
            localizedEntry "KIRG" "IronGolemAttack1",
            localizedEntry "KLIC" "HeroLichAttack1",
            localizedEntry "KMKG" "HeroMountainKingAttack1",
            localizedEntry "KMT1" "MortarTeamAttack1",
            localizedEntry "KMT2" "MortarTeamAttack2",
            localizedEntry "KMTW" "MeatWagonAttack1",
            localizedEntry "KPB1" "BrewmasterAttack1",
            localizedEntry "KPB2" "BrewmasterAttack2",
            localizedEntry "KPD1" "PitLordAttack1",
            localizedEntry "KPD2" "PitLordAttack2",
            localizedEntry "KPD3" "PitLordAttack3",
            localizedEntry "KPL1" "HeroPaladinAttack1",
            localizedEntry "KPL2" "HeroPaladinAttack2",
            localizedEntry "KPS1" "PitLordAttackSlam1",
            localizedEntry "KPS2" "PitLordAttackSlam2",
            localizedEntry "KRDB" "RedDragonAttack1",
            localizedEntry "KRG1" "RockGolemAttack1",
            localizedEntry "KRG2" "RockGolemAttack2",
            localizedEntry "KRIF" "RiflemanAttack1",
            localizedEntry "KRN1" "HeroRangerAttack1",
            localizedEntry "KRN2" "HeroRangerAttack2",
            localizedEntry "KSTT" "SteamTankAttack",
            localizedEntry "KTC1" "HeroTaurenChieftainAttack1",
            localizedEntry "KTC2" "HeroTaurenChieftainAttack2",
            localizedEntry "KTOL" "TreeOfLifeAttack1",
            localizedEntry "KWAR" "WardenAttack",
            -- Missile Sounds
            localizedEntry "MABS" "AbsorbManaLaunch",
            localizedEntry "MANG" "AncestralGuardianMissileHit",
            localizedEntry "MANL" "AncestralGuardianMissileLaunch",
            localizedEntry "MANP" "AncientProtectorMissileHit",
            localizedEntry "MAPL" "AncientProtectorMissileLaunch",
            localizedEntry "MARL" "ArrowLaunch",
            localizedEntry "MARR" "ArrowHit",
            localizedEntry "MAXE" "AxeMissileHit",
            localizedEntry "MAXL" "AxeMissileLaunch",
            localizedEntry "MBAL" "BallistaMissileHit",
            localizedEntry "MBAN" "BansheeMissileHit",
            localizedEntry "MBHL" "BoatMissileLaunch",
            localizedEntry "MBHT" "BoatMissileHit",
            localizedEntry "MBLT" "Bolt",
            localizedEntry "MBML" "BloodMageMissileLaunch",
            localizedEntry "MBNL" "BansheeMissileLaunch",
            localizedEntry "MBRH" "BristleBackMissileHit",
            localizedEntry "MBRL" "BristleBackMissileLaunch",
            localizedEntry "MBSH" "PriestMissileHit",
            localizedEntry "MBSL" "PriestMissileLaunch",
            localizedEntry "MCAH" "ChimaeraAcidHit",
            localizedEntry "MCAL" "ChimaeraAcidLaunch",
            localizedEntry "MCAN" "CannonTowerMissile",
            localizedEntry "MCAT" "Catapult",
            localizedEntry "MCDA" "ColdArrow",
            localizedEntry "MCRH" "CryptFiendMissileHit",
            localizedEntry "MCRL" "CryptFiendMissileLaunch",
            localizedEntry "MDCL" "DeathCoilMissile",
            localizedEntry "MDEM" "DemonHunterMissileHit",
            localizedEntry "MDLL" "DestroyerMissileLaunch",
            localizedEntry "MDML" "DemonHunterMissileLaunch",
            localizedEntry "MDOC" "WitchDoctorMissileLaunch",
            localizedEntry "MDOH" "WitchDoctorMissileHit",
            localizedEntry "MDRY" "DryadMissile",
            localizedEntry "MDTH" "DruidOfTheTalonMissileHit",
            localizedEntry "MDTL" "DruidOfTheTalonMissileLaunch",
            localizedEntry "MDVM" "DevourMagicLaunch",
            localizedEntry "MFAH" "FrostArrowHit",
            localizedEntry "MFAL" "FrostArrowLaunch",
            localizedEntry "MFAR" "FarseerMissile",
            localizedEntry "MFBH" "FrostBoltHit",
            localizedEntry "MFBL" "FrostBoltLaunch",
            localizedEntry "MFDL" "FaerieDragonLaunch",
            localizedEntry "MFKH" "FanOfKnivesHit",
            localizedEntry "MFLA" "SearingArrowHit",
            localizedEntry "MFLL" "SearingArrowLaunch",
            localizedEntry "MFRB" "Fireball",
            localizedEntry "MFRL" "FireballLaunch",
            localizedEntry "MGML" "GryphonRiderMissileLaunch",
            localizedEntry "MGRH" "GargoyleMissileHit",
            localizedEntry "MGRL" "GargoyleMissileLaunch",
            localizedEntry "MGUA" "GuardTowerMissileHit",
            localizedEntry "MHAR" "HarpyMissileHit",
            localizedEntry "MHNL" "HunterMissileLaunch",
            localizedEntry "MHRL" "HarpyMissileLaunch",
            localizedEntry "MHUN" "HunterMissileHit",
            localizedEntry "MKGH" "KeeperOfTheGroveMissileHit",
            localizedEntry "MKGL" "KeeperOfTheGroveMissileLaunch",
            localizedEntry "MKMH" "NecromancerMissileHit",
            localizedEntry "MKML" "NecromancerMissileLaunch",
            localizedEntry "MLIC" "LichMissile",
            localizedEntry "MLSH" "BansheeMissileHit",
            localizedEntry "MLSL" "BansheeMissileLaunch",
            localizedEntry "MMEA" "MeatWagonMissileHit",
            localizedEntry "MMTI" "Mortar",
            localizedEntry "MNCH" "NecromancerMissileHit",
            localizedEntry "MNCL" "NecromancerMissileLaunch",
            localizedEntry "MPAH" "PoisonArrowHit",
            localizedEntry "MPMH" "PriestMissileHit",
            localizedEntry "MPML" "PriestMissileLaunch",
            localizedEntry "MPXL" "PhoenixMissileLaunch",
            localizedEntry "MRAN" "RangerMissile",
            localizedEntry "MRIF" "Rifle",
            localizedEntry "MSBH" "PriestMissileHit",
            localizedEntry "MSBL" "PriestMissileLaunch",
            localizedEntry "MSEH" "SentinelMissileHit",
            localizedEntry "MSEL" "SentinelMissileLaunch",
            localizedEntry "MSHD" "ShadowHunterMissileLaunch",
            localizedEntry "MSHH" "ShadowHunterMissileHit",
            localizedEntry "MSMH" "SorceressMissileHit",
            localizedEntry "MSML" "SorceressMissileLaunch",
            localizedEntry "MSNL" "SnapDragonMissileLaunch",
            localizedEntry "MSPR" "Spear",
            localizedEntry "MSVH" "GargoyleMissileHit",
            localizedEntry "MSVL" "GargoyleMissileLaunch",
            localizedEntry "MTBL" "TrollBatriderMissileLaunch",
            localizedEntry "MWAT" "WaterElementalMissile",
            localizedEntry "MWEB" "WebMissileLaunch",
            localizedEntry "MWIN" "DragonHawkMissileHit",
            localizedEntry "MWNL" "DragonHawkMissileLaunch",
            localizedEntry "MWYV" "WyvernSpearMissile",
            localizedEntry "MZFH" "ZigguratFrostMissileHit",
            localizedEntry "MZFL" "ZigguratFrostMissileLaunch",
            localizedEntry "MZGH" "ZigguratMissileHit",
            localizedEntry "MZIG" "ZigguratMissileLaunch"
        )


        fn getSpawnData = #(
            localizedEntry "UEGG" "CryptFiendEggsack",localizedEntry "GCBL" "GargoyleCrumble",
            localizedEntry "UDIS" "UndeadDissipate",localizedEntry "EDIS" "NightElfDissipate",localizedEntry "DDIS" "DemonDissipate",localizedEntry "ODIS" "OrcDissipate",localizedEntry "HDIS" "HumanDissipate",
            localizedEntry "HBS0" "HumanBloodSmall0",localizedEntry "HBS1" "HumanBloodSmall1",localizedEntry "HBL0" "HumanBloodLarge0",localizedEntry "HBL1" "HumanBloodLarge1",
            localizedEntry "EENT" "EntBirthTarget",localizedEntry "DNAM" "NEDeathMedium",localizedEntry "DNAS" "NEDeathSmall",localizedEntry "DUME" "UDeathMedium",localizedEntry "DUSM" "UDeathSmall",
            localizedEntry "INFR" "InfernalMeteor",localizedEntry "INFL" "InfernalMeteor2",localizedEntry "INFU" "InfernalMeteor3",
            localizedEntry "HBF0" "HumanBloodFootman",localizedEntry "HBK0" "HumanBloodKnight",localizedEntry "HBM0" "HumanBloodMortar",
            localizedEntry "HBP0" "HumanBloodPeasant",localizedEntry "HBPR" "HumanBloodPriest",localizedEntry "HBR0" "HumanBloodRifleman",localizedEntry "HBSR" "HumanBloodSorceress",
            localizedEntry "HBNE" "UndeadBloodNecro",localizedEntry "NBVW" "NpcBloodVillagerWoman",
            localizedEntry "OBHE" "OrcBloodHeadhunter",localizedEntry "OBHS" "OrcBloodHellScream",localizedEntry "OBFS" "OrcBloodFarSeer",
            localizedEntry "OBTC" "OrcBloodTaurenChief",localizedEntry "OBKB" "OrcBloodKodoBeast",localizedEntry "OBWD" "OrcBloodWitchDoctor",
            localizedEntry "OBWR" "OrcBloodWolfrider",localizedEntry "OBWY" "OrcBloodWyvernRider",localizedEntry "OBWV" "OrcBloodRiderlessWyvern",
            localizedEntry "OBT0" "OrcBloodTauren",localizedEntry "OBG0" "OrcBloodGrunt",localizedEntry "OBP0" "OrcBloodPeon",localizedEntry "OKBP" "KodoBeastPuke",
            localizedEntry "UBGA" "UndeadBloodGargoyle",localizedEntry "UBGH" "UndeadBloodGhoul",localizedEntry "UBAB" "UndeadBloodAbomination",
            localizedEntry "UBAC" "UndeadBloodAcolyte",localizedEntry "DBCR" "UndeadBloodCryptFiend",
            localizedEntry "NBAR" "NEBloodArcher",localizedEntry "NBDC" "NEBloodDruidClaw",localizedEntry "NBDT" "NEBloodDruidTalon",
            localizedEntry "NBDR" "NEBloodDryad",localizedEntry "NBHU" "NEBloodHuntress",localizedEntry "NBDB" "NEBloodDruidBear",localizedEntry "NBDA" "NEBloodDruidRaven",
            localizedEntry "NBDH" "NEBloodDemonHunter",localizedEntry "NBKG" "NEBloodKeeper",localizedEntry "NBMP" "NEBloodMoonPriestess",
            localizedEntry "NBCH" "NEBloodChimaera",localizedEntry "NBHG" "NEBloodHippogryph",
            localizedEntry "DBPT" "DemonBloodPitlord",localizedEntry "DNBL" "NeutralBldgExplosion",localizedEntry "CLID" "ImpaleTargetDust",
            localizedEntry "HFSS" "SmallFlameSpawn",localizedEntry "UBSC" "ObsidianCrumble",localizedEntry "UBCC" "ObsidianCrumble2",
            localizedEntry "HBBM" "BloodElfBlood",localizedEntry "HBSB" "BloodElfSpellThief",localizedEntry "NBMF" "MalfurionBlood",
            localizedEntry "OBBT" "BattrollBlood",localizedEntry "OBSH" "ShadowHunterBlood",localizedEntry "DBPB" "BrewmasterBlood",localizedEntry "DBBM" "BeastmasterBlood",
            localizedEntry "PEFI" "ImmolationRedTarget",localizedEntry "DNBD" "NagaDeath",localizedEntry "FTSO" "FlameThrowerSpawn",localizedEntry "TOBO" "ToonBoom",
            localizedEntry "CBAL" "CritterBloodAlbatross",localizedEntry "IFP0" "IllidanFootPrint0",localizedEntry "IFP1" "IllidanFootPrint1",localizedEntry "IFPW" "IllidanWaterFootPrint",
            localizedEntry "HBCE" "HumanBloodCinematic",localizedEntry "OBCE" "OrcBloodCinematic",localizedEntry "FRBS" "FragBoomSpawn",
            localizedEntry "PBSX" "BrewmasterExplosionUlt",localizedEntry "GDCR" "GoldCredit",localizedEntry "NBWS" "NagaBloodWindserpent"
        )



        -- Extracts just the code from a "CODE - Description" string
        fn getCodeFromItem itemStr =
        (
            local spaceIdx = findString itemStr " "
            if spaceIdx != undefined then
                substring itemStr 1 (spaceIdx - 1)
            else
                itemStr
        )

        fn getDataForType typeIdx =
        (
            case typeIdx of
            (
                1: getFootprintData()
                2: getSplatData()
                3: getUberSplatData()
                4: getSoundData()
                5: getSpawnData()
                default: #()
            )
        )

        -- =====================================================================
        -- EVENT HANDLERS
        -- =====================================================================

        on ddlEventType selected sel do
        (
            ddlEventData.items = getDataForType sel
            ddlEventData.selection = 1
            edtCustomData.text = ""
        )

        on btnConfirmRename pressed do
        (
            local Type
            if (ddlEventType.selection == 1) then (Type = "FPTx")
            if (ddlEventType.selection == 2) then (Type = "SPLx")
            if (ddlEventType.selection == 3) then (Type = "UBRx")
            if (ddlEventType.selection == 4) then (Type = "SNDx")
            if (ddlEventType.selection == 5) then (Type = "SPNx")
            
            local code
            if edtCustomData.text != "" then
                code = edtCustomData.text
            else
                code = getCodeFromItem (ddlEventData.selected)
            
            selection[1].name = "Obj:" + Type + code
        )

        ----------------------------Update keys after editing---------------------------
        function SustainUpdateListOfKeys =
        (
            local items = #()
            listOfKeys = #()
            lvNotes.Items.Clear()
            for i = 1 to keyList.count do
            (
                local liebiao = dotNetObject "System.Windows.Forms.ListViewItem"
                liebiao.Text = keyList[i] as string + "f"
                append items liebiao
            )
            lvNotes.Items.AddRange items
        )
        ----------------------------Initial key update----------------------------
        function UpdateListOfKeys =
        (
            local items = #()
            listOfKeys = #()
            lvNotes.Items.Clear()
            for k in keyList do
            (
                local liebiao = dotNetObject "System.Windows.Forms.ListViewItem"
                liebiao.Text = k as string + "f"
                append items liebiao
            )
            lvNotes.Items.AddRange items
        )
        ------------------------------------------------------------------
        on params open do
        (
            lvNotes.View = (dotNetClass "System.Windows.Forms.View").Details
            lvNotes.FullRowSelect = true
            lvNotes.HideSelection = false
            lvNotes.Columns.Add "Execution time" -2
            lvNotes.Sorting = (dotNetClass "System.Windows.Forms.SortOrder").None
            lvNotes.BackColor = (dotNetClass "System.Drawing.Color").FromArgb 240 240 240
            lvNotes.HeaderStyle = (dotNetClass "System.Windows.Forms.ColumnHeaderStyle").Nonclickable
            UpdateListOfKeys()

            -- Populate data dropdown for current type
            ddlEventData.items = getDataForType ddlEventType.selection

            -- Auto-detect type and data from object name
            try
            (
                local objName = selection[1].name
                if findString objName "Obj:" == 1 then
                (
                    local code = substring objName 5 (objName.count - 4)
                    local prefix = substring code 1 3
                    local dataCode = substring code 5 (code.count - 4)
                    
                    local typeIdx = case prefix of
                    (
                        "FPT": 1
                        "SPL": 2
                        "UBR": 3
                        "SND": 4
                        "SPN": 5
                        default: 0
                    )
                    if typeIdx > 0 then
                    (
                        ddlEventType.selection = typeIdx
                        ddlEventData.items = getDataForType typeIdx
                        
                        -- Search for the data code in the predefined dropdown
                        local foundInList = false
                        for i = 1 to ddlEventData.items.count do
                        (
                            if findString ddlEventData.items[i] dataCode == 1 then
                            (
                                ddlEventData.selection = i
                                edtCustomData.text = ""
                                foundInList = true
                                exit
                            )
                        )
                        
                        -- Code not in predefined list: show in custom data field
                        if not foundInList then
                        (
                            edtCustomData.text = dataCode
                            ddlEventData.selection = 1
                        )
                    )
                )
            ) catch()

            -- Localization
            if ::L != undefined then
            (
                params.title = ::L.t "event_event_object_title"
                lbl1.text = ::L.t "event_move_the_animation_slider_to_i_lbl"
                btnAddNote.text = ::L.t "event_add_btn"
                btnRemNote.text = ::L.t "event_remove_btn"
                btnRemAllNote.text = ::L.t "event_clear_all_btn"
                l1.text = ::L.t "event__type_lbl"
                l2.text = ::L.t "event__data_lbl"
                btnConfirmRename.text = ::L.t "event_confirm_btn"
            )
        )
        ----------------------Key list item selection changed parameter--------------------
        on lvNotes ItemSelectionChanged arg do (if lvNotes.SelectedItems.Count > 0 do (selectedKey = arg.Item.Text as integer))
        ------------------------------------------------------------------

        on btnAddNote pressed do
        (
            local keyFrame = (sliderTime as integer)/ticksperframe
            if (findItem keyList keyFrame) == 0 then
            (
                append keyList keyFrame
                local tmp = for v in keyList collect v
                sort tmp
                keyList = #()
                for v in tmp do append keyList v
            )
            SustainUpdateListOfKeys()
        )
        
        on btnRemNote pressed do
        (
            if selectedKey == undefined then return undefined
            local idx = findItem keyList selectedKey
            if idx > 0 then deleteItem keyList idx
            selectedKey = undefined
            SustainUpdateListOfKeys()
        )
        
        on btnRemAllNote pressed do
        (
            keyList = #()
            SustainUpdateListOfKeys()
        )

    )

    ----------------------Create helper object in viewport----------------------
    tool create
    (
        on mousePoint click do
        (
            case click of
           (
                1:
                (
                    nodeTM.translation = gridPoint
                    #stop
                ) 
           )
        )
    )

    function BuildGizmo points Drawcolor =
    (
        local unselectedColor = Drawcolor
        local selectedColor = Drawcolor
        local giz = manip.makeGizmoShape()
        giz.startNewLine()
        for pnt in points do giz.addpoint pnt
        this.addGizmoShape giz 0 unselectedColor selectedColor
    )

    function BuildGizmoCircle radius Drawcolor =
    (
        local unselectedColor = Drawcolor
        local selectedColor = Drawcolor
        local giz = manip.makeCircle [0,0,0] radius 32
        this.addGizmoShape giz 0 unselectedColor selectedColor
    )

    on updateGizmos do
    (
        this.clearGizmos()
        local Drawcolor = [0,255,0]
		Drawcolor = [Drawcolor.x/255,Drawcolor.y/255,Drawcolor.z/255]
        BuildGizmoCircle 5.0 Drawcolor
        BuildGizmo #([-3.5,-3.5,0], [-3.5,3.5,0], [3.5,3.5,0], [3.5,-3.5,0],[-3.5,-3.5,0], [0,0,4], [3.5,3.5,0]) Drawcolor
        BuildGizmo #([-3.5,3.5,0], [0,0,4], [3.5,-3.5,0]) Drawcolor
    )
    ---------------------------------------------------------------
)
