import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 650

    readonly property bool hasLiveTimeline: (typeof timelineModel !== "undefined" && timelineModel !== null)
    readonly property bool hasLivePlayback: (typeof playbackService !== "undefined" && playbackService !== null)
    readonly property bool isTextEntryFocused: {
        var item = root.activeFocusItem
        if (!item) {
            return false
        }
        if (item instanceof TextInput) {
            return !item.readOnly
        }
        if (item instanceof TextEdit) {
            return !item.readOnly
        }
        return false
    }

    title: hasLiveTimeline
           ? (timelineModel.projectName + (timelineModel.isDirty ? " *" : "") + qsTr(" — Autonomous Video Editor (AVE)"))
           : qsTr("Autonomous Video Editor (AVE)")
    color: "#0f0f13"

    // Global NLE Keyboard Shortcuts (disabled while typing inside editable text controls)
    Shortcut {
        sequence: "Space"
        enabled: root.hasLivePlayback && !root.isTextEntryFocused
        onActivated: playbackService.togglePlay()
    }

    Shortcut {
        sequence: "Left"
        enabled: root.hasLivePlayback && !root.isTextEntryFocused
        onActivated: playbackService.stepBackward(1)
    }

    Shortcut {
        sequence: "Right"
        enabled: root.hasLivePlayback && !root.isTextEntryFocused
        onActivated: playbackService.stepForward(1)
    }

    Shortcut {
        sequence: "V"
        enabled: root.hasLiveTimeline && !root.isTextEntryFocused
        onActivated: timelineModel.activeTool = 0
    }

    Shortcut {
        sequence: "C"
        enabled: root.hasLiveTimeline && !root.isTextEntryFocused
        onActivated: timelineModel.activeTool = 1
    }

    Shortcut {
        sequences: [StandardKey.Delete, "Backspace"]
        enabled: root.hasLiveTimeline && timelineModel.selectedClipId !== "" && !root.isTextEntryFocused
        onActivated: timelineModel.deleteSelected()
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: root.hasLiveTimeline && timelineModel.canUndo && !root.isTextEntryFocused
        onActivated: timelineModel.undo()
    }

    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]
        enabled: root.hasLiveTimeline && timelineModel.canRedo && !root.isTextEntryFocused
        onActivated: timelineModel.redo()
    }

    // Top Studio Navigation & Tool Toolbar
    header: ToolbarView {
        id: toolbar
        height: 60
        onOpenAiAssistantRequested: inspector.activeTab = 1
    }

    // Master Workspace Vertical Splitter
    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        // UPPER ZONE: Media Pool (Left), Program Viewer (Center), Inspector/AI Studio (Right), Audio Meter (Far Right)
        SplitView {
            SplitView.fillWidth: true
            SplitView.preferredHeight: 480
            SplitView.minimumHeight: 280
            orientation: Qt.Horizontal

            // 1. Studio Media Pool & Asset Bins
            MediaPoolView {
                id: mediaPool
                SplitView.preferredWidth: 290
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: 400
            }

            // 2. Broadcast Program Monitor / Viewport
            ViewerView {
                id: programViewer
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420
            }

            // 3. Clip Properties Inspector & AI Prompt Studio
            InspectorView {
                id: inspector
                SplitView.preferredWidth: 350
                SplitView.minimumWidth: 280
                SplitView.maximumWidth: 450
            }

            // 4. Stereo Master Hardware Audio Meter
            AudioMeterView {
                id: audioMeter
                SplitView.preferredWidth: 48
                SplitView.minimumWidth: 44
                SplitView.maximumWidth: 56
            }
        }

        // LOWER ZONE: Multi-Track Timeline & Track Control Strips
        Rectangle {
            SplitView.fillWidth: true
            SplitView.preferredHeight: 340
            SplitView.minimumHeight: 180
            color: "#0f0f13"
            border.color: "#282832"
            border.width: 1

            SplitView {
                anchors.fill: parent
                orientation: Qt.Horizontal

                // Track Headers (Lock, Mute, Solo)
                TrackHeaderView {
                    id: trackHeader
                    SplitView.preferredWidth: 220
                    SplitView.minimumWidth: 180
                    SplitView.maximumWidth: 280
                    contentY: timelineCanvas.contentY
                    onVerticalScrollDelta: (deltaY) => timelineCanvas.scrollVerticalBy(deltaY)
                }

                // Timeline Canvas (SMPTE Ruler, Scrubber, Video/Audio Tracks with Waveforms)
                TimelineCanvas {
                    id: timelineCanvas
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 400
                }
            }
        }
    }
}
