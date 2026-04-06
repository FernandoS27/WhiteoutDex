if ( DisplayWarcraftIIIAttachments != undefined ) then 
(
	UnregisterRedrawViewsCallback DisplayWarcraftIIIAttachments
)	
function DisplayWarcraftIIIAttachments=
(
	gw.setTransform ( matrix3 1 )
	for obj in objects where ( classof obj == Wc3_AttachPoint and obj.ishidden == false and obj.isfrozen == false ) do
	(
		local s = attachmentList[obj.nameFirst]
		if obj.nameAdd1 != 1 then s = s + " " + attachmentAdd1List[obj.nameAdd1]
		if obj.nameAdd2 != 1 then s = s + " " + attachmentAdd2List[obj.nameAdd2]
		local RefName = s + " Ref"
		---------------------------------------------------------------------------------------------------
		gw.text ( obj.pos + [0, 0, 0] ) (RefName) color:Yellow
	)
	gw.updateScreen()
)
RegisterRedrawViewsCallback DisplayWarcraftIIIAttachments