# Orc GUI — Main Window

## Overview

The main window is the primary interface for orc, a LaserDisc and tape decoding
orchestration framework. It hosts a DAG (Directed Acyclic Graph) editor where
processing pipelines are built by connecting stage nodes. Each node represents
one processing step; connections between nodes define the data flow.

## File Menu

| Action | Shortcut | Description |
|--------|----------|-------------|
| New Project... | Ctrl+N | Create a new project, choosing the source type and video standard. |
| Quick Project... | Ctrl+Shift+Q | Create a pre-configured project with a standard pipeline for the chosen source. |
| Open Project... | Ctrl+O | Open an existing .orc project file from disk. |
| Save Project | Ctrl+S | Save the current project to its current file. |
| Save Project As... | Ctrl+Shift+S | Save the current project under a different file name. |
| Edit Project... | — | Edit top-level project settings such as video format, source type, media path, and signal units (10-bit, mV, or IRE). |
| Quit | Ctrl+Q | Exit the application. |

## View Menu

| Action | Shortcut | Description |
|--------|----------|-------------|
| Show Preview | Ctrl+Shift+P | Open the Preview Window. |
| Show Preview on Selection | — | When checked, selecting a preview-capable node automatically opens and updates the Preview Window. |
| Arrange DAG to Grid | Ctrl+G | Automatically arrange all nodes in an ordered grid layout. |

## Tools Menu

| Action | Description |
|--------|-------------|
| Plugin Manager... | Open the Plugin Manager to load, inspect, and manage runtime stage plugins. |
| Themes | Choose the UI theme: Auto (follow the operating system), Dark, or Light. Overrides the `--theme` command-line option and is remembered between runs. |

## DAG Editor

The central panel is a node-graph editor. Each node represents a processing
stage; edges carry data between stages from left (input ports) to right (output
ports).

### Adding Nodes

Right-click on the empty canvas to open the node creation menu. Node types are
grouped by category such as Source, Transform, Merger, and Sink.

### Connecting Nodes

Drag from an **output port** (right edge of a node) to an **input port** (left
edge of another node). A single output can feed multiple downstream nodes. A
node will not execute until all of its required inputs are connected.

### Selecting Nodes

Click a node to select it. Hold **Shift** and drag on the canvas to draw a
selection rectangle and select multiple nodes at once. If **Show Preview on
Selection** is enabled, selecting a preview-capable node will automatically
update the Preview Window to show that node's output.

### Moving and Arranging Nodes

Drag a node to reposition it on the canvas. Use **Arrange DAG to Grid**
(Ctrl+G) to tidy all nodes automatically.

### Node Context Menu

Right-click any node for:

| Action | Description |
|--------|-------------|
| Help... | Open the stage's built-in documentation covering its parameters, interactive tools, and behaviour. |
| Set as Preview Source | Route this node's output to the Preview Window without changing the selection. |
| Edit Parameters... | Open the stage parameter dialog to view and edit all configurable parameters. |
| Tools | Open an interactive stage tool, if the stage provides one. Stage tools can also set parameters directly. |
| Delete | Remove the node and all its connections from the DAG. |

## Status Bar

The status bar at the bottom shows informational messages such as plugin load
results, save confirmations, and operation status.
