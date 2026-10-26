// ============================================================================
// WhiteoutDex — fills a VertexPaint modifier with one colour, for the viewport.
//
// The "Wc3 Vertex Color" modifier (Wc3VertexColor.ms) shows its geoset colour
// in the viewport through the mesh's vertex illumination channel. On an
// Editable Mesh / Poly it writes the base object; a primitive (Teapot, Box...)
// has no channel to write, so the modifier puts a plain VertexPaint modifier
// under it and fills that. MaxScript cannot set the colours a VertexPaint
// holds; the SDK's IVertexPaint (istdplug.h, the VertexPaint of 3ds Max 6 and
// later) can - but only for a VertexPaint that is itself in the stack, not for
// one that is a scripted modifier's delegate (SetColors returns 0 there).
//
// MaxScript:
//   WdxFillVertexPaint <node> <vertexPaintModifier> <color>  → true/false
//     Every vertex the modifier sees on <node> gets <color>.
// ============================================================================

#include <vector>

// clang-format off
#include <max.h>
#include <istdplug.h>
#include <triobj.h>
#include <maxscript/maxscript.h>
// MUST be the LAST maxscript header — see the same note in dllmain.cpp.
#include <maxscript/macros/define_instantiation_functions.h>
// clang-format on

def_visible_primitive(WdxFillVertexPaint, "WdxFillVertexPaint");
Value* WdxFillVertexPaint_cf(Value** arg_list, int count) {
    check_arg_count(WdxFillVertexPaint, 3, count);
    INode* node = arg_list[0]->to_node();
    ReferenceTarget* mod = arg_list[1]->to_reftarg();
    const AColor c = arg_list[2]->to_acolor();
    if (!node || !mod)
        return &false_value;
    auto* paint = static_cast<IVertexPaint*>(mod->GetInterface(IVERTEXPAINT_INTERFACE_ID));
    if (!paint)
        return &false_value;

    // One colour per vertex of the tri-mesh the modifier works on. Nothing
    // that changes topology sits above a geoset's modifiers, so the node's
    // world state has the same vertices.
    const TimeValue t = GetCOREInterface()->GetTime();
    const ObjectState os = node->EvalWorldState(t);
    if (!os.obj || !os.obj->CanConvertToType(triObjectClassID))
        return &false_value;
    auto* tri = static_cast<TriObject*>(os.obj->ConvertToType(t, triObjectClassID));
    if (!tri)
        return &false_value;
    const int numVerts = tri->GetMesh().getNumVerts();
    if (tri != os.obj)
        tri->DeleteThis();
    if (numVerts <= 0)
        return &false_value;

    std::vector<Color> colors(static_cast<size_t>(numVerts), Color(c.r, c.g, c.b));
    IVertexPaint::VertColorTab tab;
    tab.SetCount(numVerts);
    for (int i = 0; i < numVerts; ++i)
        tab[i] = &colors[static_cast<size_t>(i)];
    return paint->SetColors(node, tab) ? &true_value : &false_value;
}
