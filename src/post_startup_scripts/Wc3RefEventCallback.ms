-----------------------------------------------------------------------------------------------------------
/*
--  Wc3 Event Object System <Platinum version>
--  Original Author Taylor Mouse @2020
--  Current update author LxXDINJIN @2025
*/
-----------------------------------------------------------------------------------------------------------
-- 注销旧回调（如果存在）
if (Display_Wc3Event != undefined) then 
(
    UnregisterRedrawViewsCallback Display_Wc3Event
)
function Display_Wc3Event =
(
	gw.setTransform (matrix3 1)
	for obj in objects where ( classof obj == Wc3_Event and obj.ishidden == false and obj.isfrozen == false ) do -- 判定是否是事件对象且没被隐藏，没被冻结
	(
		gw.text (obj.pos + [0,0,4]) (obj.name as string) color:Green
	)
	gw.updateScreen()
)
RegisterRedrawViewsCallback Display_Wc3Event