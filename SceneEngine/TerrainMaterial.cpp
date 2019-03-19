// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainMaterial.h"
#include "../Utility/Meta/ClassAccessorsImpl.h"
#include "../Utility/Meta/AccessorSerialize.h"

template<> const ClassAccessors& GetAccessors<SceneEngine::TerrainMaterialConfig>()
{
    using Obj = SceneEngine::TerrainMaterialConfig;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add("DiffuseDims", DefaultGet(Obj, _diffuseDims),  DefaultSet(Obj, _diffuseDims));
        props.Add("NormalDims",  DefaultGet(Obj, _normalDims),   DefaultSet(Obj, _normalDims));
        props.Add("ParamDims",   DefaultGet(Obj, _paramDims),    DefaultSet(Obj, _paramDims));

        props.Add("Specular",        DefaultGet(Obj, _specularParameter),    DefaultSet(Obj, _specularParameter));
        props.Add("RoughnessMin",    DefaultGet(Obj, _roughnessMin),         DefaultSet(Obj, _roughnessMin));
        props.Add("RoughnessMax",    DefaultGet(Obj, _roughnessMax),         DefaultSet(Obj, _roughnessMax));
        props.Add("ShadowSoftness",  DefaultGet(Obj, _shadowSoftness),       DefaultSet(Obj, _shadowSoftness));

        props.AddChildList<Obj::GradFlagMaterial>(
            "GradFlagMaterial",
            DefaultCreate(Obj, _gradFlagMaterials),
            DefaultGetCount(Obj, _gradFlagMaterials),
            DefaultGetChildByIndex(Obj, _gradFlagMaterials),
            DefaultGetChildByKey(Obj, _gradFlagMaterials));

        props.AddChildList<Obj::ProcTextureSetting>(
            "ProcTextureSetting",
            DefaultCreate(Obj, _procTextures),
            DefaultGetCount(Obj, _procTextures),
            DefaultGetChildByIndex(Obj, _procTextures),
            DefaultGetChildByKey(Obj, _procTextures));

        init = true;
    }
    return props;
}

template<> const ClassAccessors& GetAccessors<SceneEngine::TerrainMaterialConfig::GradFlagMaterial>()
{
    using Obj = SceneEngine::TerrainMaterialConfig::GradFlagMaterial;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add("MaterialId",  DefaultGet(Obj, _id),   DefaultSet(Obj, _id));

		props.Add("Texture0", DefaultGetArray(Obj, _texture, 0), DefaultSetArray(Obj, _texture, 0));
		props.Add("Texture1", DefaultGetArray(Obj, _texture, 1), DefaultSetArray(Obj, _texture, 1));
		props.Add("Texture2", DefaultGetArray(Obj, _texture, 2), DefaultSetArray(Obj, _texture, 2));
		props.Add("Texture3", DefaultGetArray(Obj, _texture, 3), DefaultSetArray(Obj, _texture, 3));
		props.Add("Texture4", DefaultGetArray(Obj, _texture, 4), DefaultSetArray(Obj, _texture, 4));

		props.Add("Mapping0", DefaultGetArray(Obj, _mappingConstant, 0), DefaultSetArray(Obj, _mappingConstant, 0));
		props.Add("Mapping1", DefaultGetArray(Obj, _mappingConstant, 1), DefaultSetArray(Obj, _mappingConstant, 1));
		props.Add("Mapping2", DefaultGetArray(Obj, _mappingConstant, 2), DefaultSetArray(Obj, _mappingConstant, 2));
		props.Add("Mapping3", DefaultGetArray(Obj, _mappingConstant, 3), DefaultSetArray(Obj, _mappingConstant, 3));
		props.Add("Mapping4", DefaultGetArray(Obj, _mappingConstant, 4), DefaultSetArray(Obj, _mappingConstant, 4));

        props.Add(
            "Key", 
            [](const Obj& mat) { return mat._id; },
            nullptr);

        init = true;
    }
    return props;
}

template<> const ClassAccessors& GetAccessors<SceneEngine::TerrainMaterialConfig::ProcTextureSetting>()
{
    using Obj = SceneEngine::TerrainMaterialConfig::ProcTextureSetting;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add("Name", DefaultGet(Obj, _name), DefaultSet(Obj, _name));

		props.Add("Texture0", DefaultGetArray(Obj, _texture, 0), DefaultSetArray(Obj, _texture, 0));
		props.Add("Texture1", DefaultGetArray(Obj, _texture, 1), DefaultSetArray(Obj, _texture, 1));

		props.Add("HGrid", DefaultGet(Obj, _hgrid), DefaultSet(Obj, _hgrid));
        props.Add("Gain", DefaultGet(Obj, _gain), DefaultSet(Obj, _gain));
        init = true;
    }
    return props;
}

namespace SceneEngine
{

///////////////////////////////////////////////////////////////////////////////////////////////////

    void TerrainMaterialConfig::Write(OutputStreamFormatter& formatter) const
    {
        AccessorSerialize(formatter, *this);
    }

    TerrainMaterialConfig::TerrainMaterialConfig()
    {
        _diffuseDims = _normalDims = _paramDims = UInt2(32, 32);
        _specularParameter = 0.05f;
        _shadowSoftness = 15.f;
        _roughnessMin = 0.7f;
        _roughnessMax = 1.f;
    }

    TerrainMaterialConfig::TerrainMaterialConfig(
        InputStreamFormatter<utf8>& formatter,
        const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DepValPtr& depVal)
    : TerrainMaterialConfig()
    {
        AccessorDeserialize(formatter, *this);
		_depVal = depVal;
    }

    TerrainMaterialConfig::~TerrainMaterialConfig() {}

    TerrainMaterialConfig::GradFlagMaterial::GradFlagMaterial()
    {
        _id = 0;
        for (auto&m:_mappingConstant) m = 0.f;
    }

    TerrainMaterialConfig::StrataMaterial::StrataMaterial()
    {
        _id = 0;
    }

    TerrainMaterialConfig::StrataMaterial::Strata::Strata()
    {
        for (auto&m:_mappingConstant) m = 1.f;
        _endHeight = 0.f;
    }

    TerrainMaterialConfig::ProcTextureSetting::ProcTextureSetting() 
        : _hgrid(100.f), _gain(.5f) {}
}

#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/StreamDOM.h"
#include "../Utility/StringUtils.h"
#include "../Utility/Conversion.h"

namespace SceneEngine
{

///////////////////////////////////////////////////////////////////////////////////////////////////
        // Previous implementations (for performance comparisons)...

    template<typename InputType>
        ::Assets::rstring AsRString(InputType input) { return Conversion::Convert<::Assets::rstring>(input); }

    template<typename InputType>
        ::Assets::rstring AsRString(StringSection<InputType> input) { return Conversion::Convert<::Assets::rstring>(input.AsString()); }
        
    static const utf8* TextureNames[] = { u("Texture0"), u("Texture1"), u("Slopes") };

    TerrainMaterialConfig::TerrainMaterialConfig(
        InputStreamFormatter<utf8>& formatter,
        const ::Assets::DirectorySearchRules& searchRules,
        bool)
    : TerrainMaterialConfig()
    {
        Document<InputStreamFormatter<utf8>> doc(formatter);

        for (auto matCfg=doc.FirstChild(); matCfg; matCfg=matCfg.NextSibling()) {
            if (XlEqString(matCfg.Name(), u("StrataMaterial"))) {

                StrataMaterial mat;
                mat._id = Deserialize(matCfg, u("MaterialId"), 0u);

                auto strata = matCfg.Element(u("Strata"));
                unsigned strataCount = 0;
                for (auto c = strata.FirstChild(); c; c = c.NextSibling()) { ++strataCount; }

                unsigned strataIndex = 0;
                for (auto c = strata.FirstChild(); c; c = c.NextSibling(), ++strataIndex) {
                    StrataMaterial::Strata newStrata;
                    for (unsigned t=0; t<dimof(TextureNames); ++t) {
                        auto tName = c.Attribute(TextureNames[t]).Value();
                        if (XlCompareStringI(tName, u("null"))!=0)
                            newStrata._texture[t] = Conversion::Convert<::Assets::rstring>(tName.AsString());
                    }

                    newStrata._endHeight = Deserialize(c, u("EndHeight"), 0.f);
                    auto mappingConst = Deserialize(c, u("Mapping"), Float4(1.f, 1.f, 1.f, 1.f));
                    newStrata._mappingConstant[0] = mappingConst[0];
                    newStrata._mappingConstant[1] = mappingConst[1];
                    newStrata._mappingConstant[2] = mappingConst[2];

                    mat._strata.push_back(newStrata);
                }

                _strataMaterials.push_back(std::move(mat));

            } else if (XlEqString(matCfg.Name(), u("GradFlagMaterial"))) {

                GradFlagMaterial mat;
                mat._id = Deserialize(matCfg, u("MaterialId"), 0);
            
                mat._texture[0] = AsRString(matCfg.Attribute(u("Texture0")).Value());
                mat._texture[1] = AsRString(matCfg.Attribute(u("Texture1")).Value());
                mat._texture[2] = AsRString(matCfg.Attribute(u("Texture2")).Value());
                mat._texture[3] = AsRString(matCfg.Attribute(u("Texture3")).Value());
                mat._texture[4] = AsRString(matCfg.Attribute(u("Texture4")).Value());

                char buffer[512];
                auto mappingAttr = matCfg.Attribute(u("Mapping")).Value();
                auto parsedType = ImpliedTyping::Parse(
                    mappingAttr,
                    buffer, sizeof(buffer));
                ImpliedTyping::Cast(
                    mat._mappingConstant, sizeof(mat._mappingConstant), 
                    ImpliedTyping::TypeDesc(ImpliedTyping::TypeCat::Float, dimof(mat._mappingConstant)),
                    buffer, parsedType);
                
                _gradFlagMaterials.push_back(mat);

            } else if (XlEqString(matCfg.Name(), u("GradFlagMaterial"))) {

                ProcTextureSetting mat;
                mat._name = AsRString(matCfg.Attribute(u("Name")).Value());
                mat._texture[0] = AsRString(matCfg.Attribute(u("Texture0")).Value());
                mat._texture[1] = AsRString(matCfg.Attribute(u("Texture1")).Value());
                mat._hgrid = Deserialize(matCfg, u("HGrid"), mat._hgrid);
                mat._gain = Deserialize(matCfg, u("Gain"), mat._gain);
                _procTextures.push_back(mat);

            }
        }

        _searchRules = searchRules;
    }

    #if 0
        Serialize(formatter, u("DiffuseDims"), _diffuseDims);
        Serialize(formatter, u("NormalDims"), _normalDims);
        Serialize(formatter, u("ParamDims"), _paramDims);

        for (auto mat=_strataMaterials.cbegin(); mat!=_strataMaterials.cend(); ++mat) {
            auto matEle = formatter.BeginElement(u("StrataMaterial"));
            Serialize(formatter, "MaterialId", mat->_id);

            auto strataList = formatter.BeginElement(u("Strata"));
            unsigned strataIndex = 0;
            for (auto s=mat->_strata.cbegin(); s!=mat->_strata.cend(); ++s, ++strataIndex) {
                auto strata = formatter.BeginElement((StringMeld<64, utf8>() << "Strata" << strataIndex).get());
                for (unsigned t=0; t<dimof(TextureNames); ++t)
                    formatter.WriteAttribute(TextureNames[t], Conversion::Convert<std::basic_string<utf8>>(s->_texture[t]));

                Serialize(formatter, "EndHeight", s->_endHeight);
                Serialize(formatter, "Mapping", Float4(s->_mappingConstant[0], s->_mappingConstant[1], s->_mappingConstant[2], 1.f));
                formatter.EndElement(strata);
            }
            formatter.EndElement(strataList);
            formatter.EndElement(matEle);
        }

        for (auto mat=_gradFlagMaterials.cbegin(); mat!=_gradFlagMaterials.cend(); ++mat) {
            auto matEle = formatter.BeginElement(u("GradFlagMaterial"));
            Serialize(formatter, "MaterialId", mat->_id);
            
            Serialize(formatter, "Texture0", mat->_texture[0]);
            Serialize(formatter, "Texture1", mat->_texture[1]);
            Serialize(formatter, "Texture2", mat->_texture[2]);
            Serialize(formatter, "Texture3", mat->_texture[3]);
            Serialize(formatter, "Texture4", mat->_texture[4]);
            using namespace ImpliedTyping;
            Serialize(formatter, "Mapping", 
                AsString(
                    mat->_mappingConstant, sizeof(mat->_mappingConstant),
                    TypeDesc(TypeCat::Float, dimof(mat->_mappingConstant)), true));
            formatter.EndElement(matEle);
        }

        for (auto mat=_procTextures.cbegin(); mat!=_procTextures.cend(); ++mat) {
            auto matEle = formatter.BeginElement(u("ProcTextureSetting"));
            Serialize(formatter, "Name", mat->_name);
            Serialize(formatter, "Texture0", mat->_texture[0]);
            Serialize(formatter, "Texture1", mat->_texture[1]);
            Serialize(formatter, "HGrid", mat->_hgrid);
            Serialize(formatter, "Gain", mat->_gain);
            formatter.EndElement(matEle);
        }
    #endif

}

