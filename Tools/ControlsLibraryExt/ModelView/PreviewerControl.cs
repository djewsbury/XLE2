﻿// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.ComponentModel.Composition;
using System.Windows.Forms;

using Sce.Atf;
using Sce.Atf.Applications;
using Sce.Atf.Controls.Adaptable;

#pragma warning disable 0649        // Field '...' is never assigned to, and will always have its default value null

namespace ControlsLibraryExt.ModelView
{
    [Export(typeof(PreviewerContext))]
    [PartCreationPolicy(CreationPolicy.NonShared)]
    public class PreviewerContext
    {
        public GUILayer.ModelVisSettings ModelSettings
        {
            set
            {
                LayerController.SetModelSettings(value);
                OnModelSettingsChange?.Invoke(this, null);
            }
            get { return LayerController.GetModelSettings(); }
        }

        public GUILayer.VisOverlaySettings OverlaySettings
        {
            set
            {
                LayerController.SetOverlaySettings(value);
                OnOverlaySettingsChange?.Invoke(this, null);
            }
            get { return LayerController.GetOverlaySettings(); }
        }

        public GUILayer.VisMouseOver MouseOver { get { return LayerController.MouseOver; } }

        public GUILayer.VisLayerController LayerController
        {
            get
            {
                if (_layerController == null)
                {
                    // (Create on demand because MEF tends to create and destroy dummy versions of this object during initialization)
                    _layerController = new GUILayer.VisLayerController();
                }
                return _layerController;
            }
        }
        private GUILayer.VisLayerController _layerController = null;

        public GUILayer.TechniqueDelegateWrapper TechniqueOverrides
        {
            set
            {
                LayerController.SetTechniqueOverrides(value);
                OnMiscChange?.Invoke(this, null);
            }
        }

        public GUILayer.MaterialDelegateWrapper MaterialOverrides
        {
            set
            {
                LayerController.SetMaterialDelegate(value);
                OnMiscChange?.Invoke(this, null);
            }
        }

        public event EventHandler OnModelSettingsChange;
        public event EventHandler OnOverlaySettingsChange;
        public event EventHandler OnMiscChange;
    }

    [Export(typeof(PreviewerControl))]
    [PartCreationPolicy(CreationPolicy.NonShared)]
    public partial class PreviewerControl : AdaptableControl
    {
        public PreviewerControl()
        {
            InitializeComponent();
            _view.MouseClick += OnViewerMouseClick;
            _view.MouseDown += OnViewerMouseDown;
            _view.MouseMove += OnViewerMouseMove;
            _ctrls.OverlaySettings_OnChange += (object sender, EventArgs args) => {
                var previewContext = ContextAs<PreviewerContext>();
                if (previewContext != null) {
                    previewContext.OverlaySettings = ((CtrlStrip)sender).OverlaySettings;
                }
                _view.Invalidate();
            };
            _ctrls.ModelSettings_OnChange += (object sender, EventArgs args) => {
                var previewContext = ContextAs<PreviewerContext>();
                if (previewContext != null)
                {
                    previewContext.ModelSettings = ((CtrlStrip)sender).ModelSettings;
                }
                _view.Invalidate();
            };
            _animationCtrls.OnInvalidateViews += (object sender, EventArgs args) => {
                _view.Invalidate();
            };
            _ctrls.OnResetCamera += (object sender, EventArgs args) => {
                var context = ContextAs<PreviewerContext>();
                if (context != null)
                {
                    context.LayerController.ResetCamera();
                }
            };
        }

        private bool _isContextMenuPrimed = false;

        private void OnViewerMouseMove(object sender, MouseEventArgs e)
        {
            _isContextMenuPrimed = false;
        }

        private void OnViewerMouseDown(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
                _isContextMenuPrimed = true;
        }

        public void SetContext(PreviewerContext context)
        {
            var existingContext = ContextAs<PreviewerContext>();
            if (existingContext != null)
            {
                existingContext.LayerController.DetachFromView(_view.Underlying);
                existingContext.OnModelSettingsChange -= OnModelSettingsChange;
                existingContext.OnOverlaySettingsChange -= OnOverlaySettingsChange;
                existingContext.OnMiscChange -= OnMiscChange;
            }
            Context = context;
            if (context != null)
            {
                context.LayerController.AttachToView(_view.Underlying);
                _ctrls.OverlaySettings = context.OverlaySettings;
                _ctrls.ModelSettings = context.ModelSettings;
                _animationCtrls.AnimationState = context.LayerController.AnimationState;
                context.OnModelSettingsChange += OnModelSettingsChange;
                context.OnOverlaySettingsChange += OnOverlaySettingsChange;
                context.OnMiscChange += OnMiscChange;
            }
            _view.Invalidate();
        }

        public Material.ActiveMaterialContext ActiveMaterialContext { get; set; }

        public void Invalidate3DView() { _view.Invalidate(); }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                SetContext(null);
                if (components != null) components.Dispose();
                if (_view != null)
                {
                    _view.MouseClick -= OnViewerMouseClick;
                    _view.Dispose();
                }
            }
            base.Dispose(disposing);
        }

        private void OnModelSettingsChange(object sender, EventArgs args)
        {
            var context = ContextAs<PreviewerContext>();
            if (context != null)
            {
                _ctrls.ModelSettings = context.ModelSettings;
            }
        }

        private void OnOverlaySettingsChange(object sender, EventArgs args)
        {
            var context = ContextAs<PreviewerContext>();
            if (context != null)
            {
                _ctrls.OverlaySettings = context.OverlaySettings;
            }
        }

        private void OnMiscChange(object sender, EventArgs args)
        {
            _view.Invalidate();
        }

        #region ContextMenu
        protected void ContextMenu_EditMaterial(object sender, EventArgs e)
        {
            if (ActiveMaterialContext == null) return;
            var i = sender as MenuItem;
            if (i != null)
            {
                var s = i.Tag as string;
                if (s != null)
                    ActiveMaterialContext.MaterialName = s;
            }
        }

        protected void OnViewerMouseClick(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
            {
                if (_isContextMenuPrimed)
                {
                    var context = ContextAs<PreviewerContext>();
                    if (context != null && context.MouseOver.HasMouseOver && ActiveMaterialContext != null)
                    {
                        ContextMenu cm = new ContextMenu();
                        var matName = context.MouseOver.MaterialName;
                        if (!string.IsNullOrEmpty(matName) && matName[0] != '<')
                        {
                            cm.MenuItems.Add(
                                new MenuItem("Pick &Material (" + context.MouseOver.MaterialName + ")", new EventHandler(ContextMenu_EditMaterial)) { Tag = context.MouseOver.FullMaterialName });

                            /*foreach (var t in ActiveMaterialContext.AssignableTechniqueConfigs)
                            {
                                cm.MenuItems.Add(
                                    new MenuItem("Assign Technique (" + t + ")",
                                    new EventHandler(ContextMenu_AssignTechnique))
                                    { Tag = new Tuple<string, string>(context.MouseOver.FullMaterialName, t) });
                            }*/
                        }

                        cm.Show(this, e.Location);
                    }
                }
                _isContextMenuPrimed = false;
            }
        }
        #endregion
    }
}
