// MaxCore — Modifier stack reader implementation
#include "modifier_reader.h"
#include "../util/class_ids.h"

namespace core {

Modifier* ModifierReader::findModifier(INode* node, Class_ID classId) {
    if (!node) return nullptr;
    Object* obj = node->GetObjectRef();
    if (!obj) return nullptr;

    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* dObj = static_cast<IDerivedObject*>(obj);
        for (int i = 0; i < dObj->NumModifiers(); ++i) {
            Modifier* mod = dObj->GetModifier(i);
            if (mod && mod->ClassID() == classId)
                return mod;
        }
        obj = dObj->GetObjRef();
    }
    return nullptr;
}

ISkin* ModifierReader::findSkin(INode* node) {
    Modifier* mod = findModifier(node, core_ids::SKIN_CLASS_ID);
    if (!mod) return nullptr;
    return static_cast<ISkin*>(mod->GetInterface(I_SKIN));
}

Modifier* ModifierReader::findEditNormals(INode* node) {
    return findModifier(node, core_ids::EDIT_NORMALS_CLASS_ID);
}

} // namespace core
