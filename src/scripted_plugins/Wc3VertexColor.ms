plugin modifier Wdx_Wc3VertexMod
name:"Wc3 Vertex Color" classID:#(0x7A1B2C07, 0x3D4E5F07) 
extends:VertexPaint replaceUI:true version:1
(
	parameters main rollout:params
	(
		VertexColor type:#color animatable:true ui:Scolor default:[255,255,255]
		UsesDropShadow type:#boolean animatable:false ui:uds1 default:false
		UsesColor type:#boolean animatable:false ui:udc1 default:true
	)
	rollout params "Basic Parameters"
	(
		label       l1				"—— -  Overlay Color  - ——"	align:#center
		colorPicker Scolor			                            pos:[8,25]  width:142 height:32 color:(color 255 255 255)
		label       l2              "Drop Shadow ............."   pos:[12,63] width:124 align:#left
		checkbox 	uds1                                        pos:[137,63]
		label       l3              "Use Color .................."   pos:[12,87] width:124 align:#left
		checkbox 	udc1                                        pos:[137,87]

		on params open do
		(
			if ::WdxL != undefined then
			(
				params.title = ::WdxL.t "vtx_basic_parameters_title"
				l1.text = ::WdxL.t "vtx_overlay_color_lbl"
				l2.text = ::WdxL.t "vtx_drop_shadow_lbl"
				l3.text = ::WdxL.t "vtx_use_color_lbl"
			)
		)
	)
)
