﻿using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Linq;
using System.Text;

namespace HyperGraph
{
    public sealed class NodeEventArgs : EventArgs
    {
        public NodeEventArgs(Node node) { Node = node; }
        public Node Node { get; private set; }
    }

    public sealed class ElementEventArgs : EventArgs
    {
        public ElementEventArgs(IElement element) { Element = element; }
        public IElement Element { get; private set; }
    }

    public sealed class AcceptNodeEventArgs : CancelEventArgs
    {
        public AcceptNodeEventArgs(Node node) { Node = node; }
        public AcceptNodeEventArgs(Node node, bool cancel) : base(cancel) { Node = node; }
        public Node Node { get; private set; }
    }

    public sealed class AcceptElementLocationEventArgs : CancelEventArgs
    {
        public AcceptElementLocationEventArgs(IElement element, Point position) { Element = element; Position = position; }
        public AcceptElementLocationEventArgs(IElement element, Point position, bool cancel) : base(cancel) { Element = element; Position = position; }
        public IElement Element { get; private set; }
        public Point Position { get; private set; }
    }

    public sealed class NodeConnectionEventArgs : EventArgs
    {
        public NodeConnectionEventArgs(NodeConnection connection) { Connection = connection; From = connection.From; To = connection.To; }
        public NodeConnectionEventArgs(NodeConnector from, NodeConnector to, NodeConnection connection) { Connection = connection; From = from; To = to; }
        public NodeConnector From { get; set; }
        public NodeConnector To { get; set; }
        public NodeConnection Connection { get; private set; }
    }

    public sealed class AcceptNodeConnectionEventArgs : CancelEventArgs
    {
        public AcceptNodeConnectionEventArgs(NodeConnection connection) { Connection = connection; }
        public AcceptNodeConnectionEventArgs(NodeConnection connection, bool cancel) : base(cancel) { Connection = connection; }
        public NodeConnection Connection { get; private set; }
    }

    public sealed class MiscChangeEventArgs : EventArgs
    {
        public MiscChangeEventArgs(bool invalidateShaders) { InvalidateShaders = invalidateShaders;  }
        public bool InvalidateShaders { get; private set; }
    }

    public interface IGraphModel
    {
        IEnumerable<Node> Nodes { get; }
        IEnumerable<Node> SubGraphs { get; }

        void BringElementToFront(IElement element);
        bool AddNode(Node node);
        bool AddNodes(IEnumerable<Node> nodes);
        void RemoveNode(Node node);
        bool RemoveNodes(IEnumerable<Node> nodes);
        bool AddSubGraph(Node subGraph);
        void InvokeMiscChange(bool rebuildShaders);

        NodeConnection Connect(NodeConnector from, NodeConnector to, string name = "");
        bool Disconnect(NodeConnection connection);
        bool DisconnectAll(Node node);
        bool ConnectionIsAllowed(NodeConnector from, NodeConnector to);

        Compatibility.ICompatibilityStrategy CompatibilityStrategy { get; set; }

        uint GlobalRevisionIndex { get; }

        event EventHandler<AcceptNodeEventArgs>             NodeAdded;
        event EventHandler<AcceptNodeEventArgs>             NodeRemoving;
        event EventHandler<NodeEventArgs>                   NodeRemoved;
        event EventHandler<AcceptNodeConnectionEventArgs>   ConnectionAdding;
        event EventHandler<AcceptNodeConnectionEventArgs>   ConnectionAdded;
        event EventHandler<AcceptNodeConnectionEventArgs>   ConnectionRemoving;
        event EventHandler<NodeConnectionEventArgs>         ConnectionRemoved;
        event EventHandler<MiscChangeEventArgs>             MiscChange;
        event EventHandler<EventArgs>                       InvalidateViews;
    }

    public interface IGraphSelection
    {
        ISet<IElement> Selection { get; }

        void Update(IEnumerable<IElement> selectedItems, IEnumerable<IElement> deselectedItems);
        void SelectSingle(IElement newSelection);
        bool Contains(IElement element);

        event EventHandler SelectionChanged;
        event EventHandler SelectionChanging;
    }
}
