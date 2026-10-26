-----------------------------------------------------------------------------------------------------------
/*
--  Wc3 Event Object System <Platinum version>
--  Original Author Taylor Mouse @2020
--  Current update author LxXDINJIN @2025
*/
-----------------------------------------------------------------------------------------------------------
-- 注销旧回调（如果存在）
if (WdxDisplay_Wc3Event != undefined) then 
(
    UnregisterRedrawViewsCallback WdxDisplay_Wc3Event
)
function WdxDisplay_Wc3Event =
(
	gw.setTransform (matrix3 1)
	for obj in objects where ( classof obj == Wdx_Wc3Event and obj.ishidden == false and obj.isfrozen == false ) do -- 判定是否是事件对象且没被隐藏，没被冻结
	(
		-- Label with the MDX event name; events saved before `eventName`
		-- existed read "" (or undefined) there and keep showing the node name.
		local evtName = try (obj.eventName) catch (undefined)
		if evtName == undefined or evtName == "" do evtName = obj.name
		gw.text (obj.pos + [0,0,4]) (evtName as string) color:Green
	)
	gw.updateScreen()
)
RegisterRedrawViewsCallback WdxDisplay_Wc3Event

-----------------------------------------------------------------------------------------------------------
-- Event node labels
-- An event node is named after its MDX name plus, for a known data code, a
-- readable description in the current language:
--     SNDxDDSN (Sound - NightElfDissipate)     FPTxFPL1 (脚印 - 爪印大号左)
-- Only a label: the exporter writes the plug-in's `eventName` parameter, not
-- the node name (wc3_event_extractor.cpp). The importer names new event nodes
-- with WdxEventDisplayName, and the event panel renames a node when its code
-- changes - unless the user has given the node a name of their own.
-----------------------------------------------------------------------------------------------------------
global WdxEventDisplayName
global WdxEventIsAutoName
global WdxEventApplyName

-- "SNDxDDSN" -> "SNDxDDSN (Sound - NightElfDissipate)"; the code alone when
-- its data part has no description (custom codes, sound paths).
fn WdxEventDisplayName code =
(
	if code == undefined or code.count < 5 or ::WdxL == undefined then return code
	local dataKey = "evt_" + (substring code 5 -1)
	local desc = ::WdxL.t dataKey
	if desc == dataKey or desc == "" then return code
	local typeKey = "evt_type_" + (toUpper (substring code 1 3))
	local typeWord = ::WdxL.t typeKey
	if typeWord == typeKey or typeWord == "" then
		code + " (" + desc + ")"
	else
		code + " (" + typeWord + " - " + desc + ")"
)

-- True while a node still carries a name made from its code: the code itself,
-- the old "Obj:" form (with or without a NeoDex note behind it) or a label.
fn WdxEventIsAutoName nodeName code =
(
	if nodeName == undefined or nodeName == "" then return true
	if code == undefined or code == "" then return false
	if nodeName == code or nodeName == ("Obj:" + code) then return true
	if matchPattern nodeName pattern:(code + " (*)") ignoreCase:false then return true
	matchPattern nodeName pattern:("Obj:" + code + "(*)") ignoreCase:false
)

-- Renames node for newCode when its name is still the automatic one for oldCode.
fn WdxEventApplyName node oldCode newCode =
(
	if not (isValidNode node) then return false
	if not (WdxEventIsAutoName node.name oldCode) and not (WdxEventIsAutoName node.name newCode) then return false
	node.name = WdxEventDisplayName newCode
	true
)
