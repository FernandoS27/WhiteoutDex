// MaxCore — Common Max SDK utility functions
#include "max_helpers.h"
#include "../util/class_ids.h"
#include <modstack.h>

namespace core {

TriObject* getTriObject(INode* node, TimeValue t, bool& needDelete) {
    needDelete = false;
    ObjectState os = node->EvalWorldState(t);
    Object* obj = os.obj;
    if (!obj) return nullptr;

    if (obj->CanConvertToType(triObjectClassID)) {
        TriObject* tri = static_cast<TriObject*>(obj->ConvertToType(t, triObjectClassID));
        needDelete = (tri != obj);
        return tri;
    }
    return nullptr;
}

Matrix3 getLocalTM(INode* node, TimeValue t) {
    Matrix3 nodeTM = node->GetNodeTM(t);
    INode* parent = node->GetParentNode();
    if (parent && !parent->IsRootNode()) {
        Matrix3 parentTM = parent->GetNodeTM(t);
        return nodeTM * Inverse(parentTM);
    }
    return nodeTM;
}

INode* getMaxRoot() {
    return GetCOREInterface()->GetRootNode();
}

bool hasSkinModifier(INode* node) {
    return findModifierByClassID(node, core_ids::SKIN_CLASS_ID) != nullptr;
}

Modifier* findModifierByClassID(INode* node, Class_ID id) {
    Object* obj = node->GetObjectRef();
    if (!obj) return nullptr;

    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* dobj = static_cast<IDerivedObject*>(obj);
        int numMods = dobj->NumModifiers();
        for (int i = 0; i < numMods; ++i) {
            Modifier* mod = dobj->GetModifier(i);
            if (mod && mod->ClassID() == id)
                return mod;
        }
        obj = dobj->GetObjRef();
    }
    return nullptr;
}

} // namespace core
