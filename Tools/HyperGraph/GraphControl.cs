﻿#region License
// Copyright (c) 2009 Sander van Rossen, 2013 Oliver Salzburg
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#endregion

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Reflection;
using HyperGraph.Compatibility;

namespace HyperGraph
{
	public delegate bool AcceptElement(IElement element);

	public class GraphControl
	{
		public void Attach(Control ctrl)
		{
            // Hack --  SetStyle is protected, but we can access it by reflection
            //          It seems unclear why this was protected... Perhaps because it effects
            //          the way some virtual methods work? Anyway, it also effects our callbacks.
            //          So let's work-around this restruction.
			// ctrl.SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.Opaque | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.Selectable | ControlStyles.UserPaint, true);
            var type = ctrl.GetType();
            var method = type.GetMethod("SetStyle", BindingFlags.NonPublic | BindingFlags.Instance);
            if (method != null)
                method.Invoke(ctrl, new object[]{
                    ControlStyles.AllPaintingInWmPaint | ControlStyles.Opaque | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.Selectable | ControlStyles.UserPaint, true});

            ctrl.Paint += OnPaint;
            ctrl.MouseWheel += OnMouseWheel;
            ctrl.MouseDown += OnMouseDown;
            ctrl.MouseMove += OnMouseMove;
            ctrl.MouseUp += OnMouseUp;
            ctrl.DoubleClick += OnDoubleClick;
            ctrl.MouseClick += OnMouseClick;
            ctrl.KeyDown += OnKeyDown;
            ctrl.KeyUp += OnKeyUp;
            ctrl.DragEnter += OnDragEnter;
            ctrl.DragOver += OnDragOver;
            ctrl.DragLeave += OnDragLeave;
            ctrl.DragDrop += OnDragDrop;
            _invalidationChain = new WeakReference(ctrl);
		}

        public void Detach(Control ctrl)
        {
            ctrl.Paint -= OnPaint;
            ctrl.MouseWheel -= OnMouseWheel;
            ctrl.MouseDown -= OnMouseDown;
            ctrl.MouseMove -= OnMouseMove;
            ctrl.MouseUp -= OnMouseUp;
            ctrl.DoubleClick -= OnDoubleClick;
            ctrl.MouseClick -= OnMouseClick;
            ctrl.KeyDown -= OnKeyDown;
            ctrl.KeyUp -= OnKeyUp;
            ctrl.DragEnter -= OnDragEnter;
            ctrl.DragOver -= OnDragOver;
            ctrl.DragLeave -= OnDragLeave;
            ctrl.DragDrop -= OnDragDrop;
        }

        public event EventHandler<AcceptElementLocationEventArgs>   ShowElementMenu; 

		#region Grid
		public bool		ShowGrid					= true;
		public float	internalSmallGridStep		= 16.0f;
		[Description("The distance between the smallest grid lines"), Category("Appearance")] 
		public float	SmallGridStep
		{ 
			get 
			{
				return internalSmallGridStep; 
			}
			set
			{
				if (internalSmallGridStep == value)
					return;

				internalSmallGridStep = value;
                InvokeInvalidate();
			}
		}
		public float	internalLargeGridStep			= 16.0f * 8.0f;
		[Description("The distance between the largest grid lines"), Category("Appearance")] 
		public float	LargeGridStep
		{ 
			get 
			{ 
				return internalLargeGridStep; 
			}
			set
			{
				if (internalLargeGridStep == value)
					return;

				internalLargeGridStep = value;
                InvokeInvalidate();
			}
		}
		private Color	internalSmallStepGridColor	= Color.Gray;
		private Pen		SmallGridPen				= new Pen(Color.Gray);
		[Description("The color for the grid lines with the smallest gap between them"), Category("Appearance")] 
		public Color	SmallStepGridColor
		{
			get { return internalSmallStepGridColor; }
			set
			{
				if (internalSmallStepGridColor == value)
					return;

				internalSmallStepGridColor = value;
				SmallGridPen = new Pen(internalSmallStepGridColor);
                InvokeInvalidate();
			}
		}
		private Color	internalLargeStepGridColor	= Color.LightGray;
		private Pen		LargeGridPen				= new Pen(Color.LightGray);
		[Description("The color for the grid lines with the largest gap between them"), Category("Appearance")] 
		public Color	LargeStepGridColor
		{
			get { return internalLargeStepGridColor; }
			set
			{
				if (internalLargeStepGridColor == value)
					return;

				internalLargeStepGridColor = value;
				LargeGridPen = new Pen(internalLargeStepGridColor);
                InvokeInvalidate();
			}
		}
		#endregion

		#region DragElement
		IElement internalDragElement;
		IElement DragElement
		{
			get { return internalDragElement; }
			set
			{
				if (internalDragElement == value)
					return;
				if (internalDragElement != null)
					SetFlag(internalDragElement, RenderState.Dragging, false, false);
				internalDragElement = value;
				if (internalDragElement != null)
					SetFlag(internalDragElement, RenderState.Dragging, true, false);
			}
		}
		#endregion
		
		#region HoverElement
		IElement internalHoverElement;
		IElement HoverElement
		{
			get { return internalHoverElement; }
			set
			{
				if (internalHoverElement == value)
					return;
				if (internalHoverElement != null)
					SetFlag(internalHoverElement, RenderState.Hover, false, true);
				internalHoverElement = value;
				if (internalHoverElement != null)
					SetFlag(internalHoverElement, RenderState.Hover, true, true);
			}
		}
		#endregion
		
		#region SetFlag
		RenderState SetFlag(RenderState original, RenderState flag, bool value)
		{
			if (value)
				return original | flag;
			else
				return original & ~flag;
		}
		void SetFlag(IElement element, RenderState flag, bool value)
		{
			if (element == null)
				return;

			switch (element.ElementType)
			{
				case ElementType.NodeSelection:
				{
					var selection = element as NodeSelection;
					foreach (var node in selection.Nodes)
					{
						node.state = SetFlag(node.state, flag, value);
                        if (node.TitleItem != null)
						    SetFlag(node.TitleItem, flag, value);
					}
					break;
				}

				case ElementType.Node:
				{
					var node = element as Node;
					node.state = SetFlag(node.state, flag, value);
                    if (node.TitleItem != null)
                        SetFlag(node.TitleItem, flag, value);
					break;
				}

				case ElementType.Connector:
					var connector = element as NodeConnector;
					connector.state = SetFlag(connector.state, flag, value);
					break;

				case ElementType.Connection:
					var connection = element as NodeConnection;
					connection.state = SetFlag(connection.state, flag, value);
					break;

				case ElementType.NodeItem:
					var item = element as NodeItem;
					item.state = SetFlag(item.state, flag, value);
					break;
			}
		}
		void SetFlag(IElement element, RenderState flag, bool value, bool setConnections)
		{
			if (element == null)
				return;

			switch (element.ElementType)
			{
				case ElementType.NodeSelection:
				{
					var selection = element as NodeSelection;
					foreach (var node in selection.Nodes)
					{
						node.state = SetFlag(node.state, flag, value);
                        if (node.TitleItem != null)
                            SetFlag(node.TitleItem, flag, value);
					}
					break;
				}

				case ElementType.Node:
				{
					var node = element as Node;
					node.state = SetFlag(node.state, flag, value);
                    if (node.TitleItem != null)
                        SetFlag(node.TitleItem, flag, value);
					break;
				}

				case ElementType.Connector:
					var connector = element as NodeConnector;
					connector.state = SetFlag(connector.state, flag, value);
					break;

				case ElementType.Connection:
					var connection = element as NodeConnection;
					connection.state = SetFlag(connection.state, flag, value);
					if (setConnections)
					{
						//if (connection.From != null)
						//	connection.From.state = SetFlag(connection.From.state, flag, value);
						//if (connection.To != null)
						//	connection.To.state = SetFlag(connection.To.state, flag, value);
						//SetFlag(connection.From, flag, value, setConnections);
						//SetFlag(connection.To, flag, value, setConnections);
					}
					break;

				case ElementType.NodeItem:
					var item = element as NodeItem;
					item.state = SetFlag(item.state, flag, value);
					SetFlag(item.Node, flag, value, setConnections);
					break;
			}
		}
		#endregion
		
		#region ShowLabels
		bool internalShowLabels = false;
		[Description("Show labels on the lines that connect the graph nodes"), Category("Appearance")]
		public bool ShowLabels 
		{ 
			get 
			{
				return internalShowLabels;
			} 
			set
			{
				if (internalShowLabels == value)
					return;
				internalShowLabels = value;
				InvokeInvalidate();
			}
		}
		#endregion

        /// <summary>
		/// Should compatible connectors be highlighted when dragging a connection?
		/// </summary>
		[DisplayName( "Highlight Compatible Node Items" )]
		[Description( "Should compatible connectors be highlighted when dragging a connection?" )]
		[Category( "Behavior" )]
		public bool HighlightCompatible { get; set; }

        public IGraphModel Model 
        {
            set
            {
                if (_model != null)
                    _model.InvalidateViews -= InvalidateViewsHandler;
                _model = value;
                _model.InvalidateViews += InvalidateViewsHandler;
            }
            get { return _model; }
        }

        public void InvalidateViewsHandler(object sender, EventArgs args) { InvokeInvalidate(); }

        private WeakReference _invalidationChain;
        private void InvokeInvalidate() 
        {
            if (_invalidationChain == null) return;
            var t = _invalidationChain.Target as Control;
            if (t != null) t.Invalidate(); 
        }

        public IGraphSelection Selection
        {
            set {
                if (_selection != null)
                    _selection.SelectionChanged -= _selection_SelectionChanged;
                _selection = value;
                _selection.SelectionChanged += _selection_SelectionChanged;
            }
            get { return _selection; }
        }

        private void _selection_SelectionChanged(object sender, EventArgs e)
        {
            UpdateFocusStates();
        }

        private ISet<IElement> WorkingSelection()
        {
            var result = Selection.Selection;
            if (_unselectedNodes.Any() || _selectedNodes.Any())
            {
                result = new HashSet<IElement>(result);
                result.ExceptWith(_unselectedNodes);
                result.UnionWith(_selectedNodes);
            }
            return result;
        }

        public object Context { get; set; }

        private void UpdateFocusStates()
        {
            // only setting the "focus" state on the nodes
            var set = WorkingSelection();
            foreach (var n in Nodes)
                SetFlag(n, RenderState.Focus, set.Contains(n), false);
        }

        protected IGraphModel _model;
        protected IGraphSelection _selection;

		[Browsable(false), EditorBrowsable(EditorBrowsableState.Never)]
		private IEnumerable<Node> Nodes { get { return _model.Nodes; } }

		enum CommandMode
		{
			MarqueSelection,
			TranslateView,
			ScaleView,
			Edit
		}

		IElement				internalDragOverElement;
		bool					mouseMoved		= false;
		bool					dragging		= false;
		bool					abortDrag		= false;
		CommandMode				command			= CommandMode.Edit;
		MouseButtons			currentButtons;

		Point					lastLocation;
		PointF					snappedLocation;
		PointF					originalLocation;
		Point					originalMouseLocation;
		
        private List<Node> _selectedNodes = new List<Node>();
        private List<Node> _unselectedNodes = new List<Node>();

        #region Transformations
        protected PointF _translation = new PointF();
        protected float _zoom = 1.0f;
        public static readonly float MinZoom = 0.25f;
        public static readonly float MaxZoom = 5.00f;

        private readonly Matrix			transformation = new Matrix();
		private readonly Matrix			inverse_transformation = new Matrix();

        public Matrix Transform {  get { return transformation; } }

		protected void UpdateMatrices()
		{
			transformation.Reset();
			transformation.Translate(_translation.X, _translation.Y);
			transformation.Scale(_zoom, _zoom);

            inverse_transformation.Reset();
            inverse_transformation.Scale(1.0f / _zoom, 1.0f / _zoom);
			inverse_transformation.Translate(-_translation.X, -_translation.Y);
		}
		#endregion

		#region FindNodeItemAt
		static NodeItem FindNodeItemAt(Node node, PointF location)
		{
            Node.Column[] columns = new Node.Column[] { Node.Column.Input, Node.Column.Center, Node.Column.Output };
            foreach (var c in columns)
            {
                foreach (var item in node.ItemsForColumn(c))
                {
                    if (item.bounds.Contains(location))
                        return item;
                }
            }
			return null;
		}
		#endregion

		#region Picking
		static NodeConnector FindInputConnectorAt(Node node, PointF location)
		{
			foreach (var inputConnector in node.InputConnectors)
			{
				if (inputConnector.bounds.IsEmpty)
					continue;

				if (inputConnector.bounds.Contains(location))
					return inputConnector;
			}
			return null;
		}

        static NodeConnector FindOutputConnectorAt(Node node, PointF location)
		{
			foreach (var outputConnector in node.OutputConnectors)
			{
				if (outputConnector.bounds.IsEmpty)
					continue;

				if (outputConnector.bounds.Contains(location))
					return outputConnector;
			}
			return null;
		}

        public static IElement FindElementAt(IGraphModel model, PointF location, AcceptElement acceptElement = null)
		{
            foreach (var node in model.Nodes)
			{
				var inputConnector = FindInputConnectorAt(node, location);
				if (inputConnector != null && (acceptElement==null || acceptElement(inputConnector)))
					return inputConnector;

				var outputConnector = FindOutputConnectorAt(node, location);
				if (outputConnector != null && (acceptElement== null || acceptElement(outputConnector)))
					return outputConnector;

				if (node.bounds.Contains(location))
				{
                    if (!node.Collapsed)
                    {
                        var item = FindNodeItemAt(node, location);
                        if (item != null && (acceptElement == null || acceptElement(item)))
                            return item;
                    }
					if (acceptElement == null || acceptElement(node))
						return node;
				}
			}

            foreach (var subGraph in model.SubGraphs)
            {
                var inputConnector = FindInputConnectorAt(subGraph, location);
                if (inputConnector != null && (acceptElement == null || acceptElement(inputConnector)))
                    return inputConnector;

                var outputConnector = FindOutputConnectorAt(subGraph, location);
                if (outputConnector != null && (acceptElement == null || acceptElement(outputConnector)))
                    return outputConnector;
            }

            var skipConnections		= new HashSet<NodeConnection>();
			var foundConnections	= new List<NodeConnection>();
            foreach (var node in model.Nodes)
			{
				foreach (var connection in node.Connections)
				{
					if (skipConnections.Add(connection)) // if we can add it, we haven't checked it yet
					{
						if (connection.bounds.Contains(location))
							foundConnections.Insert(0, connection);
					}
				}
			}
            foreach (var subGraph in model.Nodes)
            {
                foreach (var connection in subGraph.Connections)
                {
                    if (skipConnections.Add(connection)) // if we can add it, we haven't checked it yet
                    {
                        if (connection.bounds.Contains(location))
                            foundConnections.Insert(0, connection);
                    }
                }
            }
            foreach (var connection in foundConnections)
			{
				if (connection.textBounds.Contains(location) && (acceptElement == null || acceptElement(connection)))
					return connection;
			}
			foreach (var connection in foundConnections)
			{
				using (var region = GraphRenderer.GetConnectionRegion(connection))
				{
					if (region.IsVisible(location) && (acceptElement == null || acceptElement(connection)))
						return connection;
				}
			}
            foreach (var subGraph in model.SubGraphs)
            {
                if (subGraph.bounds.Contains(location) && (acceptElement == null || acceptElement(subGraph)))
                    return subGraph;
            }

            return null;
		}

        public static IEnumerable<IElement> RectangleSelection(IGraphModel model, RectangleF rect, AcceptElement acceptElement = null)
        {
            return model.Nodes.Where(x => (rect.Contains(x.bounds) && (acceptElement == null || acceptElement(x))));
        }
		#endregion
		
		#region GetTransformedLocation
		PointF GetTransformedLocation()
		{
			var points = new PointF[] { snappedLocation };
			inverse_transformation.TransformPoints(points);
			var transformed_location = points[0];

			if (abortDrag)
			{
				transformed_location = originalLocation;
			}
			return transformed_location;
		}

        public PointF ClientToModel(PointF clientCoord)
        {
            var points = new PointF[] { snappedLocation };
            inverse_transformation.TransformPoints(points);
            return points[0];
        }
		#endregion

		#region GetMarqueRectangle
		RectangleF GetMarqueRectangle()
		{
			var transformed_location = GetTransformedLocation();
			var x1 = transformed_location.X;
			var y1 = transformed_location.Y;
			var x2 = originalLocation.X;
			var y2 = originalLocation.Y;
			var x = Math.Min(x1, x2);
			var y = Math.Min(y1, y2);
			var width = Math.Max(x1, x2) - x;
			var height = Math.Max(y1, y2) - y;
			return new RectangleF(x,y,width,height);
		}				
		#endregion

		#region OnPaint
		private void OnPaint(object sender, PaintEventArgs e)
		{
			if (e.Graphics == null)
				return;

			e.Graphics.PageUnit				= GraphicsUnit.Pixel;
			e.Graphics.CompositingQuality	= CompositingQuality.GammaCorrected;
			e.Graphics.TextRenderingHint	= TextRenderingHint.ClearTypeGridFit;
			e.Graphics.PixelOffsetMode		= PixelOffsetMode.HighQuality;
			e.Graphics.InterpolationMode	= InterpolationMode.HighQualityBicubic;

			UpdateMatrices();			
			e.Graphics.Transform			= transformation;

            OnDrawBackground((Control)sender, e);
			
			e.Graphics.SmoothingMode		= SmoothingMode.HighQuality;

			var transformed_location = GetTransformedLocation();
			if (command == CommandMode.MarqueSelection)
			{
				var marque_rectangle = GetMarqueRectangle();
				e.Graphics.FillRectangle(SystemBrushes.ActiveCaption, marque_rectangle);
				e.Graphics.DrawRectangle(Pens.DarkGray, marque_rectangle.X, marque_rectangle.Y, marque_rectangle.Width, marque_rectangle.Height);
			}

            if (_model == null || (!_model.Nodes.Any() && !_model.SubGraphs.Any()))
                return;

            GraphRenderer.PerformLayout(e.Graphics, _model);
            GraphRenderer.Render(e.Graphics, _model, ShowLabels, Context);
			
			if (command == CommandMode.Edit)
			{
				if (dragging)
				{
					if (DragElement != null)
					{
						RenderState renderState = RenderState.Dragging | RenderState.Hover;
						switch (DragElement.ElementType)
						{
							case ElementType.Connector:
								var connector = DragElement as NodeConnector;
                                renderState |= (connector.state & (RenderState.Incompatible | RenderState.Compatible | RenderState.Conversion));
                                GraphRenderer.RenderConnection(e.Graphics, connector, transformed_location.X, transformed_location.Y, renderState);
								break;
						}
					}
				}
			}
		}
		#endregion

		#region OnDrawBackground
		private void OnDrawBackground(Control ctrl, PaintEventArgs e)
		{
			e.Graphics.Clear(ctrl.BackColor);

			if (!ShowGrid)
				return;

			var points		= new PointF[]{
								new PointF(e.ClipRectangle.Left , e.ClipRectangle.Top),
								new PointF(e.ClipRectangle.Right, e.ClipRectangle.Bottom)
							};

			inverse_transformation.TransformPoints(points);

			var left			= points[0].X;
			var right			= points[1].X;
			var top				= points[0].Y;
			var bottom			= points[1].Y;
			var smallStepScaled	= SmallGridStep;
			
			var smallXOffset	= ((float)Math.Round(left / smallStepScaled) * smallStepScaled);
			var smallYOffset	= ((float)Math.Round(top  / smallStepScaled) * smallStepScaled);

			if (smallStepScaled > 3)
			{
				for (float x = smallXOffset; x < right; x += smallStepScaled)
					e.Graphics.DrawLine(SmallGridPen, x, top, x, bottom);

				for (float y = smallYOffset; y < bottom; y += smallStepScaled)
					e.Graphics.DrawLine(SmallGridPen, left, y, right, y);
			}

			var largeStepScaled = LargeGridStep;
			var largeXOffset	= ((float)Math.Round(left / largeStepScaled) * largeStepScaled);
			var largeYOffset	= ((float)Math.Round(top  / largeStepScaled) * largeStepScaled);

			if (largeStepScaled > 3)
			{
				for (float x = largeXOffset; x < right; x += largeStepScaled)
					e.Graphics.DrawLine(LargeGridPen, x, top, x, bottom);

				for (float y = largeYOffset; y < bottom; y += largeStepScaled)
					e.Graphics.DrawLine(LargeGridPen, left, y, right, y);
			}
		}
		#endregion



		#region OnMouseWheel
		private void OnMouseWheel(object sender, MouseEventArgs e)
		{
            float oldZoom = _zoom;
			_zoom *= (float)Math.Pow(2, e.Delta / 480.0f);
            if (_zoom < MinZoom) _zoom = MinZoom;
            if (_zoom > MaxZoom) _zoom = MaxZoom;

            var ctrl = sender as Control;
            if (ctrl != null)
            {
                // Adjust translation to so that we appear to zoom in and out
                // of the center of the control
                var fixedPt = new PointF(ctrl.Width / 2.0f, ctrl.Height / 2.0f);
                var t = new PointF((fixedPt.X - _translation.X) / oldZoom, (fixedPt.Y - _translation.Y) / oldZoom);
                var t2 = new PointF((fixedPt.X - _translation.X) / _zoom, (fixedPt.Y - _translation.Y) / _zoom);
                _translation.X += (t2.X - t.X) * _zoom;
                _translation.Y += (t2.Y - t.Y) * _zoom;
                UpdateMatrices();
                ctrl.Invalidate();
            }
		}
		#endregion

        NodeSelection BuildNodeSelection()
        {
            if (Selection == null) return null;

            var nodes = new List<Node>();
            foreach (var e in Selection.Selection)
            {
                var n = e as Node;
                if (n != null) nodes.Add(n);
            }
            return new NodeSelection(nodes);    
        }

		#region OnMouseDown
		private void OnMouseDown(object sender, MouseEventArgs e)
		{
			if (currentButtons != MouseButtons.None)
				return;

			currentButtons |= e.Button;

            _selectedNodes.Clear();
            _unselectedNodes.Clear();
			dragging	= true;
			abortDrag	= false;
			mouseMoved	= false;
			snappedLocation = lastLocation = e.Location;

            var ctrl = (Control)sender;
			
			var points = new PointF[] { e.Location };
			inverse_transformation.TransformPoints(points);
			var transformed_location = points[0];

			originalLocation = transformed_location;

			if (e.Button == MouseButtons.Left)
			{
				var element = FindElementAt(_model, transformed_location);
				if (element != null)
				{
					var selection = BuildNodeSelection();

					var element_node = element as Node;
					if (element_node != null)
					{
                        switch (Control.ModifierKeys)
						{
							case Keys.None:
							{
								if (selection != null &&
									selection.Nodes.Contains(element_node))
								{
									element = selection;
								}
								break;
							}

							case Keys.Shift:
							{
								if (selection != null && !selection.Nodes.Contains(element_node))
								{
									var nodes = selection.Nodes.ToList();
									nodes.Add(element_node);
									element = new NodeSelection(nodes);
								}
								break;
							}

							case Keys.Control:
							{
								if (selection != null)
								{
									if (selection.Nodes.Contains(element_node))
									{
										var nodes = selection.Nodes.ToList();
										nodes.Remove(element_node);
										element = new NodeSelection(nodes);
									}
                                    else
									{
										var nodes = selection.Nodes.ToList();
										nodes.Add(element_node);
										element = new NodeSelection(nodes);
									}
								}
								break;
							}

							case Keys.Alt:
							{
								if (selection != null && selection.Nodes.Contains(element_node))
								{
									var nodes = selection.Nodes.ToList();
									nodes.Remove(element_node);
									element = new NodeSelection(nodes);
								}
								break;
							}
						}
					}

                    if (element is NodeConnector)
                    {
                        // Should compatible connectors be highlighted?
                        if (HighlightCompatible && null != _model.CompatibilityStrategy)
                        {
                            NodeConnector draggingConnector = (NodeConnector)element;
                            foreach (Node graphNode in _model.Nodes.Concat(_model.SubGraphs))
                            {
                                IEnumerable<NodeConnector> connectors = IsInputConnector(draggingConnector) ? graphNode.OutputConnectors : graphNode.InputConnectors;
                                foreach (NodeConnector otherConnector in connectors)
                                {
                                    var connectionType = _model.CompatibilityStrategy.CanConnect(draggingConnector, otherConnector);
                                    if (connectionType == HyperGraph.Compatibility.ConnectionType.Compatible)
                                    {
                                        SetFlag(otherConnector, RenderState.Compatible, true);
                                    }
                                    else if (connectionType == HyperGraph.Compatibility.ConnectionType.Conversion)
                                    {
                                        SetFlag(otherConnector, RenderState.Conversion, true);
                                    }
                                    else
                                    {
                                        SetFlag(otherConnector, RenderState.Incompatible, true);
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        var item = element as NodeItem;
                        if (item != null)
                        {
                            if (!item.OnStartDrag(transformed_location, out originalLocation))
                            {
                                element = item.Node;
                                originalLocation = transformed_location;
                            }
                        }
                    }

                    if (Selection != null) 
                    {
                        if (element.ElementType == ElementType.NodeSelection) 
                        {
                            var sel = element as NodeSelection;
                            Selection.Update(sel.Nodes, Selection.Selection);
                        }
                        else if (element.ElementType == ElementType.Node)
                        {
                            Selection.SelectSingle(element);
                        }
                        else if (element.ElementType == ElementType.NodeItem)
                        {
                            Selection.SelectSingle((element as NodeItem).Node);
                        }
                        else if (element.ElementType == ElementType.Connector)
                        {
                            Selection.SelectSingle((element as NodeConnector).Node);
                        }
                    }

					DragElement = element;
                    _model.BringElementToFront(element);
					ctrl.Invalidate();
					command = CommandMode.Edit;
				} else
					command = CommandMode.MarqueSelection;
			} else
			{
				DragElement = null;
				command = CommandMode.TranslateView;
			}

			points = new PointF[] { originalLocation };
			transformation.TransformPoints(points);
			originalMouseLocation = ctrl.PointToScreen(new Point((int)points[0].X, (int)points[0].Y));
		}
        #endregion

        #region OnMouseMove
        private bool IsInputConnector(NodeConnector connector)
        {
            return connector.Node.InputConnectors.Contains(connector);
        }

        private void OnMouseMove(object sender, MouseEventArgs e)
		{
			if (DragElement == null &&
				command != CommandMode.MarqueSelection &&
				(currentButtons & MouseButtons.Right) != 0)
			{
				if (currentButtons == MouseButtons.Right)
					command = CommandMode.TranslateView;
				else
				if (currentButtons == (MouseButtons.Right | MouseButtons.Left))
					command = CommandMode.ScaleView;
			}

			Point currentLocation;
			PointF transformed_location;
			if (abortDrag)
			{
				transformed_location = originalLocation;

				var points = new PointF[] { originalLocation };
				transformation.TransformPoints(points);
				currentLocation = new Point((int)points[0].X, (int)points[0].Y);
			} else
			{
				currentLocation = e.Location;

				var points = new PointF[] { currentLocation };
				inverse_transformation.TransformPoints(points);
				transformed_location = points[0];
			}

			var deltaX = (lastLocation.X - currentLocation.X) / _zoom;
			var deltaY = (lastLocation.Y - currentLocation.Y) / _zoom;

            var ctrl = (Control)sender;

			bool needRedraw = false;
			switch (command)
			{
				case CommandMode.ScaleView:
					if (!mouseMoved)
					{
						if ((Math.Abs(deltaY) > 1))
							mouseMoved = true;
					}

					if (mouseMoved &&
						(Math.Abs(deltaY) > 0))
					{
						// _zoom *= (float)Math.Pow(2, deltaY / 100.0f);
                        // Cursor.Position = ctrl.PointToScreen(lastLocation);
						// snappedLocation = //lastLocation = 
						// 	currentLocation;
                        // ctrl.Invalidate();
					}
					return;
				case CommandMode.TranslateView:
				{
					if (!mouseMoved)
					{
						if ((Math.Abs(deltaX) > 1) ||
							(Math.Abs(deltaY) > 1))
							mouseMoved = true;
					}

					if (mouseMoved &&
						(Math.Abs(deltaX) > 0) ||
						(Math.Abs(deltaY) > 0))
					{
						_translation.X -= deltaX * _zoom;
						_translation.Y -= deltaY * _zoom;
						snappedLocation = lastLocation = currentLocation;
                        UpdateMatrices();
                        ctrl.Invalidate();
					}
					return;
				}
				case CommandMode.MarqueSelection:
					if (!mouseMoved)
					{
						if ((Math.Abs(deltaX) > 1) ||
							(Math.Abs(deltaY) > 1))
							mouseMoved = true;
					}

					if (mouseMoved &&
						(Math.Abs(deltaX) > 0) ||
						(Math.Abs(deltaY) > 0))
					{
						var marque_rectangle = GetMarqueRectangle();
						if (!abortDrag)
						{
                            var workingSelection = WorkingSelection();
                            foreach (var node in _model.Nodes)
							{
								if (marque_rectangle.Contains(node.bounds))
								{
									if (!workingSelection.Contains(node) && (Control.ModifierKeys != Keys.Alt))
									{
										_selectedNodes.Add(node);
									}
									if (workingSelection.Contains(node) && (Control.ModifierKeys == Keys.Alt))
									{
										_unselectedNodes.Add(node);
									}
								}
                                else
								{
									if (workingSelection.Contains(node) && (Control.ModifierKeys == Keys.None))
									{
										_unselectedNodes.Add(node);
									}
								}
							}
						}

						snappedLocation = lastLocation = currentLocation;
                        UpdateFocusStates();
                        ctrl.Invalidate();
					}
					return;

				default:
				case CommandMode.Edit:
					break;
			}

			if (dragging)
			{
				if (!mouseMoved)
				{
					if ((Math.Abs(deltaX) > 1) ||
						(Math.Abs(deltaY) > 1))
						mouseMoved = true;
				}

				if (mouseMoved &&
					(Math.Abs(deltaX) > 0) ||
					(Math.Abs(deltaY) > 0))
				{
					mouseMoved = true;
					if (DragElement != null)
					{
                        _model.BringElementToFront(DragElement);

						switch (DragElement.ElementType)
						{
							case ElementType.NodeSelection:		// drag nodes
							{
								var selection = DragElement as NodeSelection;
								foreach (var node in selection.Nodes)
								{
									node.Location = new Point(	(int)Math.Round(node.Location.X - deltaX),
																(int)Math.Round(node.Location.Y - deltaY));
								}

                                var subGraphKey = FindOverlappingSubGraph(selection.bounds);
                                foreach (var node in selection.Nodes)
                                {
                                    // Reassign subgraph, if we haven't got one assigned already
                                    if (node.SubGraphTag == null || node.SubGraphTag == pendingAssignmentSubgraphKey)
                                        node.SubGraphTag = (subGraphKey != null) ? pendingAssignmentSubgraphKey : null;
                                }

                                snappedLocation = lastLocation = currentLocation;
                                ctrl.Invalidate();
                                _model.InvokeMiscChange(false);
								return;
							}
							case ElementType.Node:				// drag single node
							{
								var node = DragElement as Node;
								node.Location	= new Point((int)Math.Round(node.Location.X - deltaX),
															(int)Math.Round(node.Location.Y - deltaY));

                                // Reassign subgraph, if we haven't got one assigned already
                                if (node.SubGraphTag == null || node.SubGraphTag == pendingAssignmentSubgraphKey)
                                {
                                    var subGraphKey = FindOverlappingSubGraph(node.bounds);
                                    node.SubGraphTag = (subGraphKey != null) ? pendingAssignmentSubgraphKey : null;
                                }

                                snappedLocation = lastLocation = currentLocation;
                                ctrl.Invalidate();
                                _model.InvokeMiscChange(false);
								return;
							}
							case ElementType.NodeItem:			// drag in node-item
							{
								var nodeItem = DragElement as NodeItem;
								needRedraw		= nodeItem.OnDrag(transformed_location);
								snappedLocation = lastLocation = currentLocation;
								break;
							}
							case ElementType.Connection:		// start dragging end of connection to new input connector
							{
                                _model.BringElementToFront(DragElement);
								var connection			= DragElement as NodeConnection;
								var outputConnector		= connection.From;
								// FocusElement			= outputConnector.Node;
                                if (_model.Disconnect(connection))
									DragElement	= outputConnector;
								else
									DragElement = null;

								goto case ElementType.Connector;
							}
							case ElementType.Connector:	// drag connection from input or output connector
							{
								snappedLocation = lastLocation = currentLocation;
								needRedraw = true;
								break;
							}
						}
					}
				}
			}

			NodeConnector destinationConnector = null;
			IElement draggingOverElement = null;

            SetFlag(DragElement, (RenderState.Compatible | RenderState.Incompatible | RenderState.Conversion), false);
			var element = FindElementAt(_model, transformed_location);
			if (element != null)
			{
				switch (element.ElementType)
				{
					default:
						if (DragElement != null)
							element = null;
						break;

					case ElementType.NodeItem:
					{	
						var item = element as NodeItem;
						if (DragElement != null)
						{
							element = item.Node;
							goto case ElementType.Node;
						}
						break;
					}
					case ElementType.Node:
					{
						var node = element as Node;
						if (DragElement != null && !_model.SubGraphs.Contains(node))
						{
							if (DragElement.ElementType == ElementType.Connector)
							{
								var dragConnector = DragElement as NodeConnector;
								if (dragConnector == null)
									break;

                                var connectors = IsInputConnector(dragConnector) ? node.OutputConnectors.ToList() : node.InputConnectors.ToList();
								if (connectors.Count == 1)
								{
									// Check if this connection would be allowed.
                                    if (_model.ConnectionIsAllowed(dragConnector, connectors[0]))
									{
										element = connectors[0];
										goto case ElementType.Connector;
									}
								}
								if (node != dragConnector.Node)
									draggingOverElement = node;
							}

							//element = null;
						}
						break;
					}
					case ElementType.Connector:
					{
						destinationConnector = element as NodeConnector;
						if (destinationConnector == null)
							break;
						
						if (DragElement != null && DragElement.ElementType == ElementType.Connector)
						{
							var dragConnector = DragElement as NodeConnector;
							if (dragConnector != null)
							{
								if (dragConnector.Node == destinationConnector.Node ||
									IsInputConnector(dragConnector) == IsInputConnector(destinationConnector))
								{
									element = null;
								} else
								{
                                    if (!_model.ConnectionIsAllowed(dragConnector, destinationConnector))
									{
										SetFlag(DragElement, RenderState.Incompatible, true);
									} else
									{
                                        SetFlag(DragElement, (destinationConnector.state & (RenderState.Compatible | RenderState.Incompatible | RenderState.Conversion)), true);
									}
								}
							}
						}
						draggingOverElement = destinationConnector.Node;
						break;
					}
				}
			}
		
			if (HoverElement != element)
			{
				HoverElement = element;
				needRedraw = true;
			}

			if (internalDragOverElement != draggingOverElement)
			{
				if (internalDragOverElement != null)
				{
					SetFlag(internalDragOverElement, RenderState.DraggedOver, false);
					var node = GetElementNode(internalDragOverElement);
					if (node != null)
                        GraphRenderer.PerformLayout(ctrl.CreateGraphics(), node);
					needRedraw = true;
				}

				internalDragOverElement = draggingOverElement;

				if (internalDragOverElement != null)
				{
					SetFlag(internalDragOverElement, RenderState.DraggedOver, true);
					var node = GetElementNode(internalDragOverElement);
					if (node != null)
                        GraphRenderer.PerformLayout(ctrl.CreateGraphics(), node);
					needRedraw = true;
				}
			}

			if (destinationConnector != null)
			{
				if (!destinationConnector.bounds.IsEmpty)
				{
                    var pre_points = new PointF[] { GraphRenderer.ConnectorInterfacePoint(destinationConnector, IsInputConnector(destinationConnector)) };
					transformation.TransformPoints(pre_points);
					snappedLocation = pre_points[0];
				}
			}
						

			if (needRedraw)
                ctrl.Invalidate();
		}
		#endregion

		#region GetElementNode
		private Node GetElementNode(IElement element)
		{
			if (element == null)
				return null;
			switch (element.ElementType)
			{
				default:
				case ElementType.Connection:		return null;
				case ElementType.Connector:	        return ((NodeConnector)element).Node;
				case ElementType.NodeItem:			return ((NodeItem)element).Node;
				case ElementType.Node:				return (Node)element;
			}
		}
		#endregion

        public sealed class NodeConnectorEventArgs : EventArgs
        {
            public NodeConnector Connector { get; set; }
            public Node Node { get; set; }
        }
        public event EventHandler<NodeConnectorEventArgs> ConnectorDoubleClick;

        private void Swap<T>(ref List<T> lhs, ref List<T> rhs)
        {
            List<T> t = lhs;
            lhs = rhs;
            rhs = t;
        }

		#region OnMouseUp
		private void OnMouseUp(object sender, MouseEventArgs e)
		{
			currentButtons &= ~e.Button;
			
			bool needRedraw = false;
			if (!dragging)
				return;

            var ctrl = (Control)sender;

			try
			{
				Point currentLocation;
				PointF transformed_location;
				if (abortDrag)
				{
					transformed_location = originalLocation;

					var points = new PointF[] { originalLocation };
					transformation.TransformPoints(points);
					currentLocation = new Point((int)points[0].X, (int)points[0].Y);
				} else
				{
					currentLocation = e.Location;

					var points = new PointF[] { currentLocation };
					inverse_transformation.TransformPoints(points);
					transformed_location = points[0];
				}

				switch (command)
				{
					case CommandMode.MarqueSelection:

                        List<Node> newUnselected = new List<Node>();
                        List<Node> newSelected = new List<Node>();
                        Swap(ref newUnselected, ref _unselectedNodes);
                        Swap(ref newSelected, ref _selectedNodes);

						if (!abortDrag && Selection != null)
						{
                            Selection.Update(newSelected, newUnselected);
						} 
                        else
                        {
                            UpdateFocusStates();
                        }
						ctrl.Invalidate();
						return;
					case CommandMode.ScaleView:
						return;
					case CommandMode.TranslateView:
						return;

					default:
					case CommandMode.Edit:
						break;
				}

				if (DragElement != null)
				{
					switch (DragElement.ElementType)
					{
						case ElementType.Connector:
						{
							var inputConnector	= (NodeConnector)DragElement;
							var outputConnector = HoverElement as NodeConnector;
                            if (outputConnector != null &&
                                outputConnector.Node != inputConnector.Node &&
                                (inputConnector.state & (RenderState.Compatible | RenderState.Conversion)) != 0)
                            {
                                var newConnection = _model.Connect(outputConnector, inputConnector);
                                if (Selection != null)
                                    Selection.SelectSingle(newConnection);
                            }
							needRedraw = true;
							return;
						}
                        case ElementType.NodeSelection:
                        {
                            var selection = DragElement as NodeSelection;
                            var subGraphKey = FindOverlappingSubGraph(selection.bounds);
                            foreach (var node in selection.Nodes)
                            {
                                // Reassign subgraph, if we haven't got one assigned already
                                if (node.SubGraphTag == null || node.SubGraphTag == pendingAssignmentSubgraphKey)
                                    node.SubGraphTag = subGraphKey;
                            }
                            needRedraw = true;
                            return;
                        }
                        case ElementType.Node:
                        {
                            var node = DragElement as Node;
                            // Complete subgraph assignment, if we haven't got a permanent assignment already
                            if (node.SubGraphTag == null || node.SubGraphTag == pendingAssignmentSubgraphKey)
                                node.SubGraphTag = FindOverlappingSubGraph(node.bounds);
                            needRedraw = true;
                            return;
                        }
                        default:
						case ElementType.Connection:
						case ElementType.NodeItem:
						{
							needRedraw = true;
							return;
						}
					}
				}

				// if (DragElement != null ||
				// 	FocusElement != null)
				// {
				// 	FocusElement = null;
				// 	needRedraw = true;
				// }
			}
			finally
			{
				if (HighlightCompatible)
				{
					// Remove all highlight flags
                    foreach (Node graphNode in _model.Nodes.Concat(_model.SubGraphs))
					{
						foreach (NodeConnector inputConnector in graphNode.InputConnectors)
                            SetFlag(inputConnector, RenderState.Compatible | RenderState.Incompatible | RenderState.Conversion, false);

						foreach (NodeConnector outputConnector in graphNode.OutputConnectors)
                            SetFlag(outputConnector, RenderState.Compatible | RenderState.Incompatible | RenderState.Conversion, false);
					}
				}

				if (DragElement != null)
				{
					var nodeItem = DragElement as NodeItem;
					if (nodeItem != null)
						nodeItem.OnEndDrag();
					DragElement = null;
					needRedraw = true;
				}

				dragging = false;
				command = CommandMode.Edit;
				_selectedNodes.Clear();
				_unselectedNodes.Clear();
				
				if (needRedraw)
					ctrl.Invalidate();
			}
		}
		#endregion

		#region OnDoubleClick
		bool ignoreDoubleClick = false;
		private void OnDoubleClick(object sender, EventArgs e)
		{
			if (mouseMoved || ignoreDoubleClick || 
				Control.ModifierKeys != Keys.None)
				return;

			var points = new Point[] { lastLocation };
			inverse_transformation.TransformPoints(points);
			var transformed_location = points[0];

			var element = FindElementAt(_model, transformed_location);
			if (element == null)
				return;

            var ctrl = (Control)sender;

			switch (element.ElementType)
			{
				case ElementType.Connection:
					((NodeConnection)element).DoDoubleClick();
					break;
				case ElementType.NodeItem:
					var item = element as NodeItem;
                    if (item.OnDoubleClick(ctrl))
					{
                        ctrl.Invalidate();
						return;
					}
					element = item.Node;
					goto case ElementType.Node;
				case ElementType.Node:
					var node = element as Node;
					node.Collapsed = !node.Collapsed;
					if (Selection!=null)
                        Selection.SelectSingle(node);
                    ctrl.Invalidate();
					break;

                case ElementType.Connector:
                    if (ConnectorDoubleClick != null)
                        ConnectorDoubleClick(this, new NodeConnectorEventArgs() { Connector = (NodeConnector)element, Node = ((NodeConnector)element).Node });
                    break;
			}
		}
		#endregion

		#region OnMouseClick
		private void OnMouseClick(object sender, MouseEventArgs e)
		{
			try
			{
				ignoreDoubleClick = false;
				if (mouseMoved)
					return;

				var points = new Point[] { lastLocation };
				inverse_transformation.TransformPoints(points);
				var transformed_location = points[0];

                var ctrl = (Control)sender;

				if (e.Button == MouseButtons.Right)
				{
					if (null != ShowElementMenu)
					{
						// See if we clicked on an element and give our owner the chance to show a menu
						var result = FindElementAt(_model, transformed_location, delegate(IElement el)
						{
							// Fire the event and see if someone cancels it.
                            var eventArgs = new AcceptElementLocationEventArgs(el, ctrl.PointToScreen(lastLocation));
							// Give our owner the chance to show a menu for this element ...
							ShowElementMenu(this, eventArgs);
							// If the owner declines (cancel == true) then we'll continue looking up the hierarchy ..
							return !eventArgs.Cancel;
						});
						// If we haven't found anything to click on we'll just return the event with a null pointer .. 
						//	allowing our owner to show a generic menu
						if (result == null)
						{
                            var eventArgs = new AcceptElementLocationEventArgs(null, ctrl.PointToScreen(lastLocation));
							ShowElementMenu(this, eventArgs);							
						}
						return;
					}
				}

				var element = FindElementAt(_model, transformed_location);
				if (element == null)
				{
					ignoreDoubleClick = true; // to avoid double-click from firing
					if (Control.ModifierKeys == Keys.None)
                        if (Selection!=null)
                            Selection.Update(null, Selection.Selection);
					return;
				}

				switch (element.ElementType)
				{
					case ElementType.NodeItem:
					{
						if (Control.ModifierKeys != Keys.None)
							return;

						var item = element as NodeItem;
                        if (item.OnClick(ctrl, e, transformation))
						{
							ignoreDoubleClick = true; // to avoid double-click from firing
                            ctrl.Invalidate();
							return;
						}
						break;
					}
				}
			}
			finally
			{
			}
		}
		#endregion

		#region OnKeyDown
		private void OnKeyDown(object sender, KeyEventArgs e)
		{
			if (e.KeyCode == Keys.Escape)
			{
				if (dragging)
				{
					abortDrag = true;
					if (command == CommandMode.Edit)
					{
						Cursor.Position = originalMouseLocation;
					} else
					if (command == CommandMode.MarqueSelection)
					{
                        _selectedNodes.Clear();
                        _unselectedNodes.Clear();
                        UpdateFocusStates();
						((Control)sender).Invalidate();
					}
					return;
				}
			}
		}
		#endregion

		#region OnKeyUp
		private void OnKeyUp(object sender, KeyEventArgs e)
		{
			if (e.KeyCode == Keys.Delete)
			{
                foreach(var q in Selection.Selection) 
                {
                    switch (q.ElementType)
                    {
                        case ElementType.Node: _model.RemoveNode(q as Node); break;
                        case ElementType.Connection: _model.Disconnect(q as NodeConnection); break;
                        case ElementType.NodeSelection:
                            {
                                var selection = q as NodeSelection;
                                foreach (var node in selection.Nodes)
                                    _model.RemoveNode(node);
                                break;
                            }
                    }
                }

                Selection.Update(null, Selection.Selection);
			}
		}
		#endregion


        private object FindOverlappingSubGraph(RectangleF rect)
        {
            foreach (var sg in _model.SubGraphs)
            {
                if (sg.bounds.IntersectsWith(rect))
                    return sg.SubGraphTag;
            }
            return null;
        }

		#region OnDragEnter
		Node dragNode = null;
        string pendingAssignmentSubgraphKey = "PendingAssignment";
		private void OnDragEnter(object sender, DragEventArgs drgevent)
		{
            if (dragNode != null)
            {
                _model.RemoveNode(dragNode);
                dragNode = null;
            }

			foreach (var name in drgevent.Data.GetFormats())
			{
				var node = drgevent.Data.GetData(name) as Node;
				if (node != null)
				{
                    if (_model.AddNode(node))
					{
						dragNode = node;

                        // Find subgraph to assign it to (based on boundary overlap)
                        if (dragNode.SubGraphTag == null || dragNode.SubGraphTag == pendingAssignmentSubgraphKey) {
                            var subGraphKey = FindOverlappingSubGraph(dragNode.bounds);
                            dragNode.SubGraphTag = (subGraphKey != null) ? pendingAssignmentSubgraphKey : null;
                        }

						drgevent.Effect = DragDropEffects.Copy;
					}
					return;
				}
			}
		}
		#endregion

		#region OnDragOver
		private void OnDragOver(object sender, DragEventArgs drgevent)
		{
			if (dragNode == null)
				return;

            var ctrl = (Control)sender;

			var location = (PointF)ctrl.PointToClient(new Point(drgevent.X, drgevent.Y));
			location.X -= ((dragNode.bounds.Right - dragNode.bounds.Left) / 2);
            if (dragNode.TitleItem != null)
			    location.Y -= ((dragNode.TitleItem.bounds.Bottom - dragNode.TitleItem.bounds.Top) / 2);
			
			var points = new PointF[] { location };
			inverse_transformation.TransformPoints(points);
			location = points[0];

			if (dragNode.Location != location)
			{
				dragNode.Location = location;

                // Reassign subgraph, if we haven't got one assigned already
                if (dragNode.SubGraphTag == null || dragNode.SubGraphTag == pendingAssignmentSubgraphKey)
                {
                    var subGraphKey = FindOverlappingSubGraph(dragNode.bounds);
                    dragNode.SubGraphTag = (subGraphKey != null) ? pendingAssignmentSubgraphKey : null;
                }

                ctrl.Invalidate();
			}
			
			drgevent.Effect = DragDropEffects.Copy;
		}
		#endregion

		#region OnDragLeave
		private void OnDragLeave(object sender, EventArgs e)
		{
			if (dragNode == null)
				return;
            _model.RemoveNode(dragNode);
			dragNode = null;
		}
		#endregion

		#region OnDragDrop
		private void OnDragDrop(object sender, DragEventArgs drgevent)
		{
            if (dragNode == null)
                return;

            // Complete subgraph assignment, if we haven't got a permanent assignment already
            if (dragNode.SubGraphTag == null || dragNode.SubGraphTag == pendingAssignmentSubgraphKey)
                dragNode.SubGraphTag = FindOverlappingSubGraph(dragNode.bounds);
            dragNode = null;
        }
		#endregion
	}
}
