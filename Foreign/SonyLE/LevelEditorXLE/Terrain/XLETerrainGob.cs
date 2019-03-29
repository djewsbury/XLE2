﻿// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Windows.Forms;

using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Dom;

using LevelEditorCore;
using LevelEditorXLE.Extensions;

namespace LevelEditorXLE.Terrain
{
    using TerrainST = Schema.terrainType;

    static class Helpers
    {
        public static T Clamp<T>(this T val, T min, T max) where T : IComparable<T>
        {
            if (val.CompareTo(min) < 0) return min;
            else if (val.CompareTo(max) > 0) return max;
            else return val;
        }
    }

    class XLETerrainGob : DomNodeAdapter, IListable, ICommandClient, IContextMenuCommandProvider, IExportable, IHierarchical, XLEBridgeUtils.INativeObjectAdapter
    {
        public XLETerrainGob() {}
        public void GetInfo(ItemInfo info)
        {
            info.ImageIndex = Util.GetTypeImageIndex(DomNode.Type, info.GetImageList());
            info.Label = "Terrain";
        }

        public static DomNode Create() { return new DomNode(TerrainST.Type); }
        public static DomNode CreateWithConfigure()
        {
            var result = new DomNode(TerrainST.Type);
            var adapter = result.As<XLETerrainGob>();
            if (adapter != null && adapter.DoModalConfigure())
                return result;
            return null;
        }

        public Uri UberSurfaceDirectory
        {
            get { return GetAttribute<Uri>(TerrainST.UberSurfaceDirAttribute); }
            set { SetAttribute(TerrainST.UberSurfaceDirAttribute, value); }
        }

        public Uri CellsDirectory
        {
            get { return GetAttribute<Uri>(TerrainST.CellsDirAttribute); }
            set { SetAttribute(TerrainST.CellsDirAttribute, value); }
        }

        public uint NodeDimensions
        {
            get { return GetAttribute<uint>(TerrainST.NodeDimensionsAttribute); }
            set { SetAttribute(TerrainST.NodeDimensionsAttribute, ClampNodeDimensions(value)); }
        }

        public uint Overlap
        {
            get { return GetAttribute<uint>(TerrainST.OverlapAttribute); }
            set { SetAttribute(TerrainST.OverlapAttribute, value); }
        }

        public float Spacing
        {
            get { return GetAttribute<float>(TerrainST.SpacingAttribute); }
            set { SetAttribute(TerrainST.SpacingAttribute, value); }
        }

        public uint CellTreeDepth
        {
            get { return GetAttribute<uint>(TerrainST.CellTreeDepthAttribute); }
            set { SetAttribute(TerrainST.CellTreeDepthAttribute, ClampCellTreeDepth(value)); }
        }

        public bool HasEncodedGradientFlags
        {
            get { return GetAttribute<bool>(TerrainST.HasEncodedGradientFlagsAttribute); }
            set { SetAttribute(TerrainST.HasEncodedGradientFlagsAttribute, value); }
        }

        public float SunPathAngle
        {
            get { return GetAttribute<float>(TerrainST.SunPathAngleAttribute); }
            set { SetAttribute(TerrainST.SunPathAngleAttribute, value); }
        }

        public uint[] CellCount
        {
            get { return GetAttribute<uint[]>(TerrainST.CellCountAttribute); }
            set { SetAttribute(TerrainST.CellCountAttribute, value); }
        }

        public float GradFlagSlopeThreshold0
        {
            get { return GetAttribute<float>(TerrainST.GradFlagSlopeThreshold0Attribute); }
            set { SetAttribute(TerrainST.GradFlagSlopeThreshold0Attribute, value); }
        }

        public float GradFlagSlopeThreshold1
        {
            get { return GetAttribute<float>(TerrainST.GradFlagSlopeThreshold1Attribute); }
            set { SetAttribute(TerrainST.GradFlagSlopeThreshold1Attribute, value); }
        }

        public float GradFlagSlopeThreshold2
        {
            get { return GetAttribute<float>(TerrainST.GradFlagSlopeThreshold2Attribute); }
            set { SetAttribute(TerrainST.GradFlagSlopeThreshold2Attribute, value); }
        }

        public Uri ConfigExportTarget
        {
            get 
            {
                var fn = GetAttribute<string>(TerrainST.ConfigFileTargetAttribute);
                var game = DomNode.GetRoot().As<Game.GameExtensions>();
                if (game != null)
                    return new Uri(game.ExportDirectory, fn);
                return Utils.CurrentDirectoryAsUri();
            }
        }

        public DomNode BaseTexture
        {
            get { return DomNode.GetChild(TerrainST.baseTextureChild); }
            set { DomNode.SetChild(TerrainST.baseTextureChild, value); }
        }

        public DomNode VegetationSpawn
        {
            get { return DomNode.GetChild(TerrainST.VegetationSpawnChild); }
            set { DomNode.SetChild(TerrainST.VegetationSpawnChild, value); }
        }

        public IList<XLETerrainCoverage> CoverageLayers
        {
            get
            {
                return GetChildList<XLETerrainCoverage>(TerrainST.coverageChild);
            }
        }

        public bool HasCoverageLayer(uint layerId)
        {
            var layers = CoverageLayers;
            foreach (var l in layers)
                if (l.LayerId == layerId) return true;
            return false;
        }

        private static uint ClampNodeDimensions(uint input)                 { return input.Clamp(1u, 1024u); }
        private static uint ClampCellTreeDepth(uint input)                  { return input.Clamp(1u, 16u); }
        private static bool IsDerivedFrom(DomNode node, DomNodeType type)   { return node.Type.Lineage.FirstOrDefault(t => t == type) != null; }
        private bool m_isLoaded = false;

        public bool CanAddChild(object child)
        {
            var domNode = child.As<DomNode>();
            if (domNode == null) return false;
            return  IsDerivedFrom(domNode, Schema.vegetationSpawnConfigType.Type)
                ||  IsDerivedFrom(domNode, Schema.abstractTerrainMaterialDescType.Type)
                ||  IsDerivedFrom(domNode, Schema.terrainBaseTextureType.Type)
                ;
        }
        public bool AddChild(object child)
        {
            var domNode = child.As<DomNode>();
            if (domNode != null && IsDerivedFrom(domNode, Schema.vegetationSpawnConfigType.Type))
            {
                SetChild(Schema.terrainType.VegetationSpawnChild, domNode);
                return true;
            }

            if (domNode != null && IsDerivedFrom(domNode, Schema.terrainBaseTextureType.Type))
            {
                SetChild(Schema.terrainType.baseTextureChild, domNode);
                return true;
            }

            if (domNode != null && IsDerivedFrom(domNode, Schema.abstractTerrainMaterialDescType.Type))
            {
                if (BaseTexture == null)
                    BaseTexture = new DomNode(Schema.terrainBaseTextureType.Type);
                BaseTexture.GetChildList(Schema.terrainBaseTextureType.materialChild).Add(domNode);
                return true;
            }

            return false;
        }

        internal IEnumerable<uint> GetAllLayerIds()
        {
            List<uint> result = new List<uint>();
            result.Add(1);
            foreach (var l in CoverageLayers)
                result.Add(l.LayerId);
            return result;
        }

        #region NativeObjectAdapter
        // protected override void OnNodeSet() {}
        public void OnAddToDocument(XLEBridgeUtils.INativeDocumentAdapter doc)
        {
            // This should be called after the node is fully created and the
            // terrain is ready to be loaded. We can call Reload() to build
            // the native terrain from here.
            if (!m_isLoaded) Reload();
        }
        public void OnRemoveFromDocument(XLEBridgeUtils.INativeDocumentAdapter doc)
        {
            // Note --  potential issues here if there are currently locks on the
            //          terrain. The unload won't succeed in that case.
            if (m_isLoaded) Unload();
        }
        public void OnSetParent(XLEBridgeUtils.INativeObjectAdapter newParent, uint childListId, int insertionPosition) { }
        #endregion

        #region IExportable
        public Uri CacheExportTarget { get { return new Uri(CellsDirectory, "cached.dat"); } }
        public string ExportCategory { get { return "Terrain"; } }

        public IEnumerable<PendingExport> BuildPendingExports()
        {
            var sceneMan = this.GetSceneManager();
            var result = new List<PendingExport>();
            result.Add(new PendingExport(CacheExportTarget, sceneMan.ExportTerrainCachedData()));
            result.Add(
                new PendingExport(
                    ConfigExportTarget, 
                    sceneMan.ExportTerrain(BuildEngineConfig(BuildDialogConfig()))));
            return result;
        }
        #endregion

        #region Internal Low Level
        internal void Unload()
        {
            try
            {
                this.GetSceneManager().UnloadTerrain();
                m_isLoaded = false;
            } 
            catch (Exception e)
            {
                ControlsLibrary.BasicControls.ExceptionReport.Show(e, "Unloading terrain");
            }
        }

        internal void Reload()
        {
            try
            {
                this.GetSceneManager().ReloadTerrain(BuildEngineConfig());
                m_isLoaded = true;

                GUILayer.EditorInterfaceUtils.SetShortCircuitSettings(
                    this.GetSceneManager(),
                    HasEncodedGradientFlags, Spacing,
                    GradFlagSlopeThreshold0, GradFlagSlopeThreshold1, GradFlagSlopeThreshold2);
            }
            catch (Exception e)
            {
                ControlsLibrary.BasicControls.ExceptionReport.Show(e, "Unloading terrain");
            }
        }

        private GUILayer.TerrainConfig BuildEngineConfig(TerrainConfig.Config cfg)
        {
            var result = new GUILayer.TerrainConfig(
                cfg.CellsDirectory,
                cfg.NodeDimensions, cfg.CellTreeDepth, cfg.Overlap,
                cfg.Spacing, (float)(cfg.SunPathAngle * Math.PI / 180.0f),
                cfg.HasEncodedGradientFlags);

            result.CellCount = new GUILayer.VectorUInt2(CellCount[0], CellCount[1]);

            var layers = CoverageLayers;
            foreach (var l in layers)
            {
                if (!l.Enable) continue;

                    // we should avoid adding multiple layers with the same id
                var id = l.LayerId;
                bool alreadyHere = false;
                for (uint c=0; c<result.CoverageLayerCount; ++c)
                    if (result.GetCoverageLayer(c).Id == id)
                    {
                        alreadyHere = true;
                        break;
                    }

                if (alreadyHere) break;

                var d = GUILayer.EditorInterfaceUtils.DefaultCoverageLayer(
                    result, cfg.UberSurfaceDirectory, id);
                d.NodeDims = new GUILayer.VectorUInt2(
                    (uint)(l.Resolution * cfg.NodeDimensions), 
                    (uint)(l.Resolution * cfg.NodeDimensions));
                if (l.Format != 0)
                {
                    d.FormatCat = l.Format;
                    d.FormatArrayCount = 1;
                }
                d.ShaderNormalizationMode = l.ShaderNormalizationMode;
                result.Add(d);
            }

            return result;
        }
        #endregion

        #region Configure Steps
        internal TerrainConfig.Config BuildDialogConfig()
        {
            var cfg = new TerrainConfig.Config();
            cfg.NodeDimensions = NodeDimensions;
            cfg.Overlap = Overlap;
            cfg.Spacing = Spacing;
            cfg.CellTreeDepth = CellTreeDepth;
            cfg.UberSurfaceDirectory = UberSurfaceDirectory != null ? UberSurfaceDirectory.LocalPath : "";
            cfg.CellsDirectory = CellsDirectory != null ? CellsDirectory.LocalPath : "";
            cfg.HasEncodedGradientFlags = HasEncodedGradientFlags;
            cfg.SunPathAngle = SunPathAngle;
            cfg.SlopeThreshold0 = GradFlagSlopeThreshold0;
            cfg.SlopeThreshold1 = GradFlagSlopeThreshold1;
            cfg.SlopeThreshold2 = GradFlagSlopeThreshold2;
            return cfg;
        }

        internal void CommitDialogConfig(TerrainConfig.Config cfg)
        {
            NodeDimensions = cfg.NodeDimensions;
            Overlap = cfg.Overlap;
            CellTreeDepth = cfg.CellTreeDepth;
            Spacing = cfg.Spacing;
            UberSurfaceDirectory = new Uri(cfg.UberSurfaceDirectory.TrimEnd('\\') + "\\");
            CellsDirectory = new Uri(cfg.CellsDirectory.TrimEnd('\\') + "\\");
            HasEncodedGradientFlags = cfg.HasEncodedGradientFlags;
            SunPathAngle = cfg.SunPathAngle;
            GradFlagSlopeThreshold0 = cfg.SlopeThreshold0;
            GradFlagSlopeThreshold1 = cfg.SlopeThreshold1;
            GradFlagSlopeThreshold2 = cfg.SlopeThreshold2;
        }

        internal GUILayer.TerrainConfig BuildEngineConfig()
        {
            return BuildEngineConfig(BuildDialogConfig());
        }

        static internal void Show(Exception e, string whileMessage)
        {
            ControlsLibrary.BasicControls.ExceptionReport.Show(e, whileMessage);
        }

        internal bool Reconfigure(TerrainConfig.Config cfg)
        {
            cfg.NodeDimensions = ClampNodeDimensions(cfg.NodeDimensions);
            cfg.CellTreeDepth = ClampCellTreeDepth(cfg.CellTreeDepth);

            Unload();

            try
            {
                var newCellCount = new GUILayer.VectorUInt2(0, 0);
                using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
                {
                    bool rebuildCellFiles = false;

                    // if there is a source DEM file specified then we should
                    // attempt to build the starter uber surface.
                    if (cfg.Import == TerrainConfig.Config.ImportType.DEMFile
                        && cfg.SourceDEMFile != null && cfg.SourceDEMFile.Length > 0)
                    {
                        cfg.ImportOp.ExecuteForHeights(cfg.UberSurfaceDirectory, progress);
                        rebuildCellFiles = true;
                    } 
                    else if (cfg.Import == TerrainConfig.Config.ImportType.NewBlankTerrain
                        && cfg.NewCellCountX != 0 && cfg.NewCellCountY != 0)
                    {
                        GUILayer.EditorInterfaceUtils.GenerateBlankUberSurface(
                            cfg.UberSurfaceDirectory, cfg.NewCellCountX, cfg.NewCellCountY,
                            cfg.NodeDimensions, cfg.CellTreeDepth,
                            progress);
                        rebuildCellFiles = true;
                    }

                    var engineCfg = BuildEngineConfig(cfg);
                    engineCfg.InitCellCountFromUberSurface(cfg.UberSurfaceDirectory);
                    newCellCount = engineCfg.CellCount;

                        // fill in the cells directory with starter cells (if they don't already exist)
                        // (and build empty uber surface files for any that are missing)
                    GUILayer.EditorInterfaceUtils.GenerateMissingUberSurfaceFiles(
                        engineCfg, cfg.UberSurfaceDirectory, progress); 
                    GUILayer.EditorInterfaceUtils.GenerateCellFiles(
                        engineCfg, cfg.UberSurfaceDirectory, rebuildCellFiles,
                        cfg.SlopeThreshold0, cfg.SlopeThreshold1, cfg.SlopeThreshold2,
                        progress);
                }

                    // if the above completed without throwing an exception, we can commit the values
                CommitDialogConfig(cfg);
                CellCount = new uint[2] { newCellCount.X, newCellCount.Y };
            }
            catch (Exception e)
            {
                Show(e, "terrain configure operation");
                return false;   // this will prevent reload on exception. Terrain will remain in an unloaded state
            }

            Reload();
            return true;
        }

        internal bool Reconfigure() { return Reconfigure(BuildDialogConfig()); }

        internal bool DoModalConfigure()
        {
                // open the configuration dialog
            using (var dlg = new TerrainConfig())
            {
                dlg.Value = BuildDialogConfig();
                var result = dlg.ShowDialog();
                if (result == DialogResult.OK)
                    return Reconfigure(dlg.Value);
            }
            return false;
        }
        #endregion

        #region Commands

        void DoGenerateShadows()
        {
            Unload();

            try
            {
                using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
                {
                    GUILayer.EditorInterfaceUtils.GenerateShadowsSurface(
                        BuildEngineConfig(), UberSurfaceDirectory.LocalPath,
                        progress);
                }

                using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
                {
                    GUILayer.EditorInterfaceUtils.GenerateAmbientOcclusionSurface(
                        BuildEngineConfig(), UberSurfaceDirectory.LocalPath,
                        progress);
                }
            }
            catch (Exception e)
            {
                Show(e, "shadow generation operation");
            }

            Reload();
        }

        void DoFlushToDisk()
        {
            using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
            {
                GUILayer.EditorInterfaceUtils.FlushTerrainToDisk(this.GetSceneManager(), progress);
            }
        }

        void DoRebuildCellFiles()
        {
            Unload();

            try
            {
                using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
                {
                    GUILayer.EditorInterfaceUtils.GenerateCellFiles(
                        BuildEngineConfig(), UberSurfaceDirectory.LocalPath, true,
                        GradFlagSlopeThreshold0, GradFlagSlopeThreshold1, GradFlagSlopeThreshold2, progress);
                }
            }
            catch (Exception e)
            {
                Show(e, "rebuilding cell files");
            }

            Reload();
        }

        internal void DoExportHeights()
        {
            var fileDlg = new SaveFileDialog();
            fileDlg.Filter = "Tiff files|*.tiff;*.tif";
            if (fileDlg.ShowDialog() == DialogResult.OK)
            {
                try
                {
                    Unload();
                    using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
                    {
                        GUILayer.EditorInterfaceUtils.ExecuteTerrainExport(
                            fileDlg.FileName,
                            BuildEngineConfig(),
                            UberSurfaceDirectory.LocalPath,
                            1, progress);
                    }
                    Reload();
                }
                catch (Exception e)
                {
                    Show(e, "Terrain export to tiff");
                }
            }
        }

        #region ICommandClient Members
        bool ICommandClient.CanDoCommand(object commandTag)
        {
            if (commandTag is Command)
            {
                switch ((Command)commandTag)
                {
                    case Command.CreateBaseTexture:
                        return BaseTexture == null;

                    case Command.CreateVegetationSpawn:
                        return VegetationSpawn == null;

                    case Command.Configure:
                    case Command.GenerateShadows:
                    case Command.FlushToDisk:
                    case Command.RebuildCellFiles:
                    case Command.Reload:
                    case Command.AddGenericCoverage:
                    case Command.ExportToTiff:
                        return true;

                    case Command.AddShadows:
                    case Command.AddAO:
                    case Command.AddBaseMaterialCoverage:
                    case Command.AddDecorationCoverage:
                        return !HasCoverageLayer(AssociatedLayerId((Command)commandTag));

                    case Command.Unload:
                        return m_isLoaded;
                }
            }
            return false;
        }

        uint AssociatedLayerId(Command cmd)
        {
            switch (cmd)
            {
                case Command.AddShadows: return 2;
                case Command.AddAO: return 3;
                case Command.AddBaseMaterialCoverage: return 1000;
                case Command.AddDecorationCoverage: return 1001;
                default: return 0;
            }
        }

        void ICommandClient.DoCommand(object commandTag)
        {
            if (!(commandTag is Command)) return;

            switch ((Command)commandTag)
            {
                case Command.CreateBaseTexture:
                    {
                        ApplicationUtil.Insert(
                            DomNode.GetRoot(), this, new DomNode(Schema.terrainBaseTextureType.Type), 
                            "Create Terrain Texturing", null);
                        break;
                    }

                case Command.CreateVegetationSpawn:
                    {
                        ApplicationUtil.Insert(
                            DomNode.GetRoot(), this, new DomNode(Schema.vegetationSpawnConfigType.Type),
                            "Create Vegetation Config", null);
                        break;
                    }

                case Command.Configure:
                    {
                        DoModalConfigure();
                        break;
                    }

                case Command.GenerateShadows:
                    {
                        DoGenerateShadows();
                        break;
                    }

                case Command.FlushToDisk:
                    {
                        DoFlushToDisk();
                        break;
                    }

                case Command.RebuildCellFiles:
                    {
                        DoRebuildCellFiles();
                        break;
                    }

                case Command.Reload:
                    {
                        Reload();
                        break;
                    }

                case Command.Unload:
                    {
                        Unload();
                        break;
                    }

                case Command.ExportToTiff:
                    DoExportHeights();
                    break;

                case Command.AddShadows:
                case Command.AddAO:
                case Command.AddBaseMaterialCoverage:
                case Command.AddDecorationCoverage:
                    {
                        var layerId = AssociatedLayerId((Command)commandTag);
                        if (!HasCoverageLayer(layerId)) 
                        {
                            var layer = XLETerrainCoverage.CreateWithConfigure(this, layerId).As<XLETerrainCoverage>();
                            if (layer != null)
                            {
                                if (!HasCoverageLayer(layer.LayerId))
                                {
                                    CoverageLayers.Add(layer);
                                    Reconfigure();
                                }
                                else
                                {
                                    MessageBox.Show(
                                        "Layer id conflicts with existing id. You can't have 2 layers with the same id. Try using a unique id", "Error adding layer",
                                        MessageBoxButtons.OK,
                                        MessageBoxIcon.Error);
                                }
                            }
                        }
                        break;
                    }

                case Command.AddGenericCoverage:
                    {
                        var layerId = 1003u;
                        while (HasCoverageLayer(layerId)) ++layerId;

                        var layer = XLETerrainCoverage.CreateWithConfigure(this, layerId).As<XLETerrainCoverage>();
                        if (layer != null)
                        {
                            if (!HasCoverageLayer(layer.LayerId))
                            {
                                CoverageLayers.Add(layer);
                                Reconfigure();
                            }
                            else
                            {
                                MessageBox.Show(
                                    "Layer id conflicts with existing id. You can't have 2 layers with the same id. Try using a unique id", "Error adding layer",
                                    MessageBoxButtons.OK,
                                    MessageBoxIcon.Error);
                            }
                        }
                        break;
                    }
            }
        }

        void ICommandClient.UpdateCommand(object commandTag, CommandState commandState)
        { }
        #endregion

        private enum Command
        {
            [Description("Add Texturing Settings")] CreateBaseTexture,
            [Description("Add Decoration Settings")] CreateVegetationSpawn,

            [Description("Configure Terrain...")] Configure,
            [Description("Generate Shadows")] GenerateShadows,
            [Description("Commit to disk")] FlushToDisk,
            [Description("Rebuild cell files")] RebuildCellFiles,
            [Description("Unload terrain")] Unload,
            [Description("Reload terrain")] Reload,
            [Description("Export to TIFF")] ExportToTiff,

            [Description("Add Shadows")] AddShadows,
            [Description("Add Ambient Occlusion")] AddAO,
            [Description("Add Base Material Coverage")] AddBaseMaterialCoverage,
            [Description("Add Decoration Coverage")] AddDecorationCoverage,
            [Description("Add Generic Converage")] AddGenericCoverage
        }

        IEnumerable<object> IContextMenuCommandProvider.GetCommands(object context, object target)
        {
            foreach (Command command in Enum.GetValues(typeof(Command)))
            {
                yield return command;
            }
        }

        #endregion
    }

    class XLETerrainCoverage : DomNodeAdapter, IListable, ICommandClient, IContextMenuCommandProvider
    {
        public void GetInfo(ItemInfo info)
        {
            info.ImageIndex = Util.GetTypeImageIndex(DomNode.Type, info.GetImageList());
            info.Label = "Coverage: " + LayerName;
        }

        public static DomNode Create() { return new DomNode(Schema.terrainCoverageLayer.Type); }
        public static DomNode CreateWithConfigure(XLETerrainGob terrain, uint id)
        {
            var result = new DomNode(Schema.terrainCoverageLayer.Type);
            var adapter = result.As<XLETerrainCoverage>();
            if (adapter != null)
            {
                adapter.LayerId = id;
                if (adapter.DoModalConfigure(terrain))
                    return result;
            }
            return null;
        }

        public uint LayerId
        {
            get { return GetAttribute<uint>(Schema.terrainCoverageLayer.IdAttribute); }
            set { SetAttribute(Schema.terrainCoverageLayer.IdAttribute, value); }
        }

        public string LayerName
        {
            get
            {
                switch (LayerId)
                {
                case 0: 
                case 1: return "Heights";
                case 2: return "Shadows";
                case 3: return "AO";
                case 1000: return "Base Material";
                case 1001: return "Decoration";
                default: return LayerId.ToString();
                }
            }
        }

        public float Resolution
        {
            get { return GetAttribute<float>(Schema.terrainCoverageLayer.ResolutionAttribute); }
            set { SetAttribute(Schema.terrainCoverageLayer.ResolutionAttribute, value); }
        }

        public Uri SourceFile
        {
            get { return GetAttribute<Uri>(Schema.terrainCoverageLayer.SourceFileAttribute); }
            set { SetAttribute(Schema.terrainCoverageLayer.SourceFileAttribute, value); }
        }

        public uint Overlap
        {
            get { return GetAttribute<uint>(Schema.terrainCoverageLayer.OverlapAttribute); }
            set { SetAttribute(Schema.terrainCoverageLayer.OverlapAttribute, value); }
        }

        public bool Enable
        {
            get { return GetAttribute<bool>(Schema.terrainCoverageLayer.EnableAttribute); }
            set { SetAttribute(Schema.terrainCoverageLayer.EnableAttribute, value); }
        }

        public uint Format
        {
            get { return GetAttribute<uint>(Schema.terrainCoverageLayer.FormatAttribute); }
            set { SetAttribute(Schema.terrainCoverageLayer.FormatAttribute, value); }
        }

        public uint ShaderNormalizationMode
        {
            get { return GetAttribute<uint>(Schema.terrainCoverageLayer.ShaderNormalizationModeAttribute); }
            set { SetAttribute(Schema.terrainCoverageLayer.ShaderNormalizationModeAttribute, value); }
        }

        public XLETerrainGob Parent { get { return DomNode.Parent.As<XLETerrainGob>(); } }

        #region Configure Steps
        internal TerrainCoverageConfig.Config BuildDialogConfig(XLETerrainGob terrain)
        {
            var cfg = new TerrainCoverageConfig.Config();

            cfg.NodeDimensions = terrain.NodeDimensions;
            cfg.CellTreeDepth = terrain.CellTreeDepth;

            cfg.Resolution = Resolution;
            cfg.SourceFile = SourceFile != null ? SourceFile.LocalPath : "";
            cfg.Enable = Enable;
            cfg.Id = LayerId;
            cfg.ShaderNormalizationMode = ShaderNormalizationMode;
            return cfg;
        }

        internal void CommitDialogConfig(TerrainCoverageConfig.Config cfg)
        {
            Resolution = cfg.Resolution;
            SourceFile = String.IsNullOrEmpty(cfg.SourceFile) ? null : new Uri(cfg.SourceFile);
            Enable = cfg.Enable;
            LayerId = cfg.Id;
            ShaderNormalizationMode = cfg.ShaderNormalizationMode;
        }

        internal bool Reconfigure(TerrainCoverageConfig.Config cfg, XLETerrainGob terrain)
        {
            if (terrain != null)
                terrain.Unload();   // (have to unload to write to ubersurface)

            try
            {
                // If an import or create operation was requested, we 
                // must perform those now. Note that this might require
                // some format changes.
                if (cfg.Import == TerrainCoverageConfig.Config.ImportType.DEMFile
                    && cfg.SourceFile != null && cfg.SourceFile.Length > 0
                    && terrain!=null)
                {
                    using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
                    {
                        cfg.ImportOp.Execute(
                            terrain.UberSurfaceDirectory.LocalPath,
                            cfg.Id, cfg.ImportOp.ImportCoverageFormat,
                            progress);
                    }
                    Format = cfg.ImportOp.ImportCoverageFormat;
                }
                else if (cfg.Import == TerrainCoverageConfig.Config.ImportType.NewBlankTerrain)
                {
                    // todo -- create new blank coverage with the given format
                }
            }
            catch (Exception e)
            {
                XLETerrainGob.Show(e, "Terrain import operation");
                return false;
            }

            CommitDialogConfig(cfg);

            if (terrain!=null)
                terrain.Reconfigure();
            return true;
        }

        internal bool DoModalConfigure(XLETerrainGob terrain)
        {
            // open the configuration dialog
            using (var dlg = new TerrainCoverageConfig())
            {
                dlg.Value = BuildDialogConfig(terrain);
                var result = dlg.ShowDialog();
                if (result == System.Windows.Forms.DialogResult.OK)
                {
                    return Reconfigure(dlg.Value, terrain);
                }
            }
            return false;
        }
        #endregion

        internal void DoExport(XLETerrainGob terrain)
        {
            if (terrain == null) return;

            var fileDlg = new SaveFileDialog();
            fileDlg.Filter = "Tiff files|*.tiff;*.tif";
            if (fileDlg.ShowDialog() == DialogResult.OK)
            {
                try
                {
                    terrain.Unload();
                    using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
                    {
                        GUILayer.EditorInterfaceUtils.ExecuteTerrainExport(
                            fileDlg.FileName,
                            terrain.BuildEngineConfig(),
                            terrain.UberSurfaceDirectory.LocalPath,
                            LayerId, progress);
                    }
                    terrain.Reload();
                }
                catch (Exception e) 
                {
                    XLETerrainGob.Show(e, "Terrain export to tiff");
                }
            }
        }

        void DoRebuildCellFiles(XLETerrainGob terrain)
        {
            if (terrain == null) return;

            terrain.Unload();

            try
            {
                using (var progress = new ControlsLibrary.ProgressDialog.ProgressInterface())
                {
                    GUILayer.EditorInterfaceUtils.GenerateCellFiles(
                        terrain.BuildEngineConfig(), terrain.UberSurfaceDirectory.LocalPath, true,
                        LayerId, progress);
                }
            }
            catch (Exception e)
            {
                XLETerrainGob.Show(e, "Rebuilding cell files");
            }

            terrain.Reload();
        }

        #region ICommandClient Members
        bool ICommandClient.CanDoCommand(object commandTag)
        {
            if (commandTag is Command)
            {
                switch ((Command)commandTag)
                {
                    case Command.Configure:
                    case Command.Export:
                    case Command.RebuildLayerCellFiles:
                        return true;
                }
            }
            return false;
        }

        void ICommandClient.DoCommand(object commandTag)
        {
            if (!(commandTag is Command)) return;

            switch ((Command)commandTag)
            {
                case Command.Configure:
                    DoModalConfigure(Parent);
                    break;

                case Command.Export:
                    DoExport(Parent);
                    break;

                case Command.RebuildLayerCellFiles:
                    DoRebuildCellFiles(Parent);
                    break;
            }
        }

        void ICommandClient.UpdateCommand(object commandTag, CommandState commandState)
        { }
        #endregion

        private enum Command
        {
            [Description("Configure...")] Configure,
            [Description("Export...")] Export,
            [Description("Rebuild layer cell files")] RebuildLayerCellFiles
        }

        IEnumerable<object> IContextMenuCommandProvider.GetCommands(object context, object target)
        {
            foreach (Command command in Enum.GetValues(typeof(Command)))
            {
                yield return command;
            }
        }
    }
}
