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