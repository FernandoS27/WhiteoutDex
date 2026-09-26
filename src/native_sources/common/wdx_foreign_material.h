#pragma once

// Materials that are not Warcraft 3 materials - Standard, Physical, OpenPBR,
// Arnold and the like, which old scenes and other tools leave on meshes. The
// exporter and the renderer read them the way Material Fix converts them to
// a Wc3 material (scene_monitor_fixes.cpp; NeoDex does the same): the colour
// texture becomes the layer's texture and the material's opacity its alpha.

#include <imtl.h>
#include <iparamb2.h>
#include <stdmat.h>

#include <cwchar>
#include <string>

namespace wdx::material {

// A parameter of any of the material's parameter blocks, by internal name,
// case-insensitively (MAXScript shows Physical's "Transparency" capitalised).
inline bool FindParam(Animatable* a, const wchar_t* name, IParamBlock2*& pb, ParamID& id)
{
    for (int b = 0; a && b < a->NumParamBlocks(); ++b) {
        IParamBlock2* p = a->GetParamBlock(b);
        ParamBlockDesc2* desc = p ? p->GetDesc() : nullptr;
        if (!desc)
            continue;
        for (int i = 0; i < desc->Count(); ++i) {
            const ParamID pid = desc->IndextoID(i);
            const ParamDef& def = desc->GetParamDef(pid);
            if (def.int_name && _wcsicmp(def.int_name, name) == 0) {
                pb = p;
                id = pid;
                return true;
            }
        }
    }
    return false;
}

// The texmap in a texmap parameter, unless its "<name>_on" switch is off.
inline Texmap* TexmapParam(Mtl* m, const wchar_t* name)
{
    IParamBlock2* pb = nullptr;
    ParamID id = 0;
    if (!FindParam(m, name, pb, id) || pb->GetParamDef(id).type != TYPE_TEXMAP)
        return nullptr;
    Texmap* t = pb->GetTexmap(id, 0);
    if (!t)
        return nullptr;
    const std::wstring onName = std::wstring(name) + L"_on";
    IParamBlock2* onPb = nullptr;
    ParamID onId = 0;
    if (FindParam(m, onName.c_str(), onPb, onId) && onPb->GetParamDef(onId).type == TYPE_BOOL &&
        !onPb->GetInt(onId, 0))
        return nullptr;
    return t;
}

// A slot's English name. MtlBase::GetSubTexmapSlotName only takes
// `localized` since Max 2022; older SDKs give the UI language's name.
inline MSTR SlotName(MtlBase* m, int i)
{
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
    return m->GetSubTexmapSlotName(i, /*localized=*/false);
#else
    return m->GetSubTexmapSlotName(i);
#endif
}

// The texture that carries the material's colour:
//   Standard - the Diffuse slot, if its map is enabled;
//   Physical - base_color_map (MAXScript Help, "Physical_Material"), and the
//              other PBR materials' usual names for it;
//   anything else - the first slot whose English name says diffuse, colour
//              or albedo (Material Fix's rule; before Max 2022 the names
//              are localised, so this last step may find nothing there).
inline Texmap* ColorTexmap(Mtl* m)
{
    if (!m)
        return nullptr;
    if (auto* std = dynamic_cast<StdMat2*>(m))
        return std->MapEnabled(ID_DI) ? std->GetSubTexmap(ID_DI) : nullptr;
    for (const wchar_t* name : {L"base_color_map", L"baseColorMap", L"diffuseMap", L"diffuse_map",
                                L"albedoMap", L"albedo_map", L"base_color_shader"}) {
        if (Texmap* t = TexmapParam(m, name))
            return t;
    }
    for (int i = 0; i < m->NumSubTexmaps(); ++i) {
        Texmap* t = m->GetSubTexmap(i);
        if (!t)
            continue;
        const MSTR slot = SlotName(m, i);
        const wchar_t* s = slot.data();
        if (s && (wcsstr(s, L"iffuse") || wcsstr(s, L"olor") || wcsstr(s, L"lbedo")))
            return t;
    }
    return nullptr;
}

// The material's opacity texture, Material Fix's #opacity role: Standard's
// Opacity map if enabled, the cutout / opacity maps of the PBR materials by
// name, else a slot whose English name says cutout or opacity.
inline Texmap* OpacityTexmap(Mtl* m)
{
    if (!m)
        return nullptr;
    if (auto* std = dynamic_cast<StdMat2*>(m))
        return std->MapEnabled(ID_OP) ? std->GetSubTexmap(ID_OP) : nullptr;
    for (const wchar_t* name : {L"cutout_map", L"geometry_opacity_map", L"opacityMap", L"opacity_map"}) {
        if (Texmap* t = TexmapParam(m, name))
            return t;
    }
    for (int i = 0; i < m->NumSubTexmaps(); ++i) {
        Texmap* t = m->GetSubTexmap(i);
        if (!t)
            continue;
        const MSTR slot = SlotName(m, i);
        const wchar_t* s = slot.data();
        if (s && (wcsstr(s, L"utout") || wcsstr(s, L"pacity")))
            return t;
    }
    return nullptr;
}

// The material's opacity, 0..1: Standard's Opacity, one minus Physical's
// Transparency or OpenPBR's transmission_weight (MAXScript Help); 1 when the
// material has none of them. MaterialFix.ms sourceOpacity reads the same.
inline float Opacity(Mtl* m, TimeValue t)
{
    if (auto* std = dynamic_cast<StdMat2*>(m))
        return std->GetOpacity(t);
    for (const wchar_t* name : {L"transparency", L"transmission_weight"}) {
        IParamBlock2* pb = nullptr;
        ParamID id = 0;
        if (FindParam(m, name, pb, id) && pb->GetParamDef(id).type == TYPE_FLOAT)
            return 1.0f - pb->GetFloat(id, t);
    }
    return 1.0f;
}

} // namespace wdx::material
