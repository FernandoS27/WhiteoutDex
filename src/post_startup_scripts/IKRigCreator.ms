/*
	IKRigCreator.ms
	===============
	IK Rig window: puts an IK rig on the arms and legs of an imported model (Star
	master, Circle per hand, Rectangle per foot, Ellipse swivel target per elbow /
	knee), moves every animation onto it and links weapons held beside a hand with
	a Link Constraint - or takes the rig off again.

	The work is done in C++ by the importer (src/native_sources/MDLXImporter/src/
	ik_rig.h explains how the rig stays exact on every frame); this window calls it
	through the MaxScript functions the importer publishes:
	    wdxIKRigDetect()                    -> #(#(label, upper, lower, end, isLeg, how), ...)
	    wdxIKRigBuild limbs:<array>         -> report lines
	    wdxIKRigRemove()                    -> report lines
	    wdxIKRigHas()                       -> true when the scene has a rig
	The import option "IK rig" calls the same C++ code right after an import.

	Compatibility: 3ds Max 2016 - 2027
*/

struct WdxIKRigTool
(
	toolName = "IK Rig",
	limbs = #(),           -- from wdxIKRigDetect
	report = #(),          -- lines of the last build / removal

	fn tr key fallback =
	(
		local s = undefined
		if ::WdxL != undefined do s = try (::WdxL.t key) catch (undefined)
		if s == undefined or s == "" or s == key then fallback else s
	),
	fn available = (::wdxIKRigBuild != undefined),
	fn detect =
	(
		limbs = if available() then (wdxIKRigDetect()) else #()
		limbs
	),
	fn build which:undefined =
	(
		report = if not available() then #(tr "ikrig_no_plugin" "The WhiteoutDex importer is not loaded.")
		         else if which == undefined then (wdxIKRigBuild())
		         else (wdxIKRigBuild limbs:which)
		report
	),
	fn remove =
	(
		report = if available() then (wdxIKRigRemove()) else #(tr "ikrig_no_plugin" "The WhiteoutDex importer is not loaded.")
		report
	),

	mainRollout = rollout mainRollout "IK Rig"
	(
		local self
		label lblInfo "" pos:[8,8] width:424 height:30 align:#left
		button btnDetect "Detect limbs" pos:[8,42] width:140 height:24 align:#left
		multiListBox lbLimbs "" pos:[8,72] width:424 height:6 align:#left
		button btnBuild "Build IK rig" pos:[8,166] width:140 height:26 align:#left
		button btnRemove "Remove IK rig" pos:[156,166] width:140 height:26 align:#left
		button btnSelect "Select controls" pos:[304,166] width:128 height:26 align:#left
		listBox lbReport "" pos:[8,200] width:424 height:7 align:#left

		fn refreshLimbs =
		(
			local items = #()
			for l in self.limbs do append items (l[1] + ":  " + l[2].name + "  >  " + l[3].name + "  >  " + l[4].name)
			lbLimbs.items = items
			lbLimbs.selection = #{1..items.count}
		)
		fn showReport = ( lbReport.items = self.report )

		on mainRollout open do
		(
			self = ::WdxIKRig
			mainRollout.title = self.tr "menu_ik_rig" "IK Rig"
			lblInfo.text = self.tr "ikrig_info" "Puts an IK rig on arms and legs and moves the animation onto it. Every frame stays as it was."
			btnDetect.caption = self.tr "ikrig_detect_btn" "Detect limbs"
			btnBuild.caption = self.tr "ikrig_build_btn" "Build IK rig"
			btnRemove.caption = self.tr "ikrig_remove_btn" "Remove IK rig"
			btnSelect.caption = self.tr "ikrig_select_btn" "Select controls"
			self.detect()
			refreshLimbs()
			showReport()
		)
		on btnDetect pressed do ( self.report = #(); self.detect(); refreshLimbs(); showReport() )
		on btnBuild pressed do
		(
			-- the ticked limbs whose nodes still exist (the scene may have changed since the list was made)
			local which = #()
			for i in (lbLimbs.selection as array) do
				if i <= self.limbs.count do
				(
					local l = self.limbs[i]
					if isValidNode l[2] and isValidNode l[3] and isValidNode l[4] do append which l
				)
			self.build which:which
			showReport()
		)
		on btnRemove pressed do ( self.remove(); self.detect(); refreshLimbs(); showReport() )
		on btnSelect pressed do
		(
			local ctl = #()
			for o in objects do
			(
				local r = (getUserProp o "WdxIKRig") as string
				if r == "control" or r == "pole" or r == "master" do append ctl o
			)
			if ctl.count > 0 do select ctl
		)
	)
)
global WdxIKRig = WdxIKRigTool()
