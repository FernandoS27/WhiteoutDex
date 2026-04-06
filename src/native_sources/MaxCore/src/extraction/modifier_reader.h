// MaxCore — Modifier stack reader
#pragma once

#include <max.h>
#include <inode.h>
#include <modstack.h>
#include <iskin.h>

namespace core {

class ModifierReader {
public:
    /// Find a modifier by ClassID on a node's modifier stack
    static Modifier* findModifier(INode* node, Class_ID classId);

    /// Find the Skin modifier and return ISkin interface
    static ISkin* findSkin(INode* node);

    /// Find Edit_Normals modifier
    static Modifier* findEditNormals(INode* node);
};

} // namespace core
