// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GameObjects.h"
#include "RetainedEntities.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Conversion.h"

namespace EntityInterface
{
    namespace EntityTypeName
    {
        static const auto* GameObjectFolder = (const utf8*)"GameObjectFolder";
    }

    template<typename CharType>
        void Serialize(
            OutputStreamFormatter& formatter,
            const RetainedEntity& obj,
            const RetainedEntities& entities)
    {
        auto name = Conversion::Convert<std::basic_string<CharType>>(entities.GetTypeName(obj._type));
        auto eleId = formatter.BeginElement(AsPointer(name.cbegin()), AsPointer(name.cend()));
        if (!obj._children.empty()) formatter.NewLine();    // properties can continue on the same line, but only if we don't have children
        obj._properties.Serialize<CharType>(formatter);

        for (auto c=obj._children.cbegin(); c!=obj._children.cend(); ++c) {
            const auto* child = entities.GetEntity(obj._doc, c->second);
            if (child)
                Serialize<CharType>(formatter, *child, entities);
        }

        formatter.EndElement(eleId);
    }

    void ExportGameObjects(
        OutputStreamFormatter& formatter,
        const RetainedEntities& flexGobInterface,
        uint64 docId)
    {
            // Export the registered game objects, in a generic format
            // We're just going to dump out everything we have. This is
            // a flexible, but simple way to move data between the editor
            // and some target project.

        const auto typeGameObjectsFolder = 
            flexGobInterface.GetTypeId(EntityTypeName::GameObjectFolder);
        auto rootFolders = flexGobInterface.FindEntitiesOfType(typeGameObjectsFolder);

        for (const auto& s : rootFolders)
            if (s->_doc == docId) {
                Serialize<utf8>(formatter, *s, flexGobInterface);
                break;
            }
        
        formatter.Flush();
    }

}

