import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    color: "#121215"
    border.color: "#282832"
    border.width: 1

    signal openAiAssistantRequested()

    readonly property bool hasLivePlayback: (typeof playbackService !== "undefined" && playbackService !== null)
    readonly property bool hasLiveTimeline: (typeof timelineModel !== "undefined" && timelineModel !== null)
    readonly property bool hasLiveMediaPool: (typeof mediaPoolModel !== "undefined" && mediaPoolModel !== null)

    readonly property int effectiveActiveTool: hasLiveTimeline ? timelineModel.activeTool : 0
    readonly property bool effectiveSnapping: hasLiveTimeline ? timelineModel.snappingEnabled : false

    property string feedbackMessage: ""
    property bool feedbackIsError: false

    function showFeedback(message, isError) {
        feedbackMessage = message
        feedbackIsError = isError
        feedbackTimer.restart()
    }

    Timer {
        id: feedbackTimer
        interval: 6000
        onTriggered: root.feedbackMessage = ""
    }

    Connections {
        target: root.hasLiveMediaPool ? mediaPoolModel : null
        function onLastImportErrorChanged() {
            if (mediaPoolModel.lastImportError !== "") {
                root.showFeedback(mediaPoolModel.lastImportError, true)
            }
        }
    }

    FileDialog {
        id: openProjectDialog
        title: qsTr("Open AVE Project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("AVE Projects (*.aveproj)"), qsTr("All files (*)")]
        onAccepted: {
            if (!root.hasLiveTimeline || !selectedFile) {
                return
            }
            if (timelineModel.openProject(selectedFile.toString())) {
                root.showFeedback(qsTr("Project opened."), false)
            } else {
                root.showFeedback(qsTr("Failed to open project file."), true)
            }
        }
    }

    FileDialog {
        id: saveProjectAsDialog
        title: qsTr("Save AVE Project As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "aveproj"
        nameFilters: [qsTr("AVE Projects (*.aveproj)"), qsTr("All files (*)")]
        onAccepted: {
            if (!root.hasLiveTimeline || !selectedFile) {
                return
            }
            if (timelineModel.saveProject(selectedFile.toString())) {
                root.showFeedback(qsTr("Project saved."), false)
            } else {
                root.showFeedback(qsTr("Failed to save project file."), true)
            }
        }
    }

    FileDialog {
        id: importMediaDialog
        title: qsTr("Import Media Files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.mxf *.wav *.mp3 *.flac *.aac *.m4a *.ogg)"),
            qsTr("All files (*)")
        ]
        onAccepted: {
            if (root.hasLiveMediaPool && selectedFiles.length > 0) {
                mediaPoolModel.importMediaFiles(selectedFiles)
            }
        }
    }

    FileDialog {
        id: exportOtioDialog
        title: qsTr("Export OpenTimelineIO")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "otio"
        nameFilters: [qsTr("OpenTimelineIO (*.otio)"), qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            if (!root.hasLiveTimeline || !selectedFile) {
                return
            }
            if (timelineModel.exportOtio(selectedFile.toString())) {
                root.showFeedback(qsTr("OTIO timeline exported."), false)
            } else {
                root.showFeedback(qsTr("Failed to export OTIO timeline."), true)
            }
        }
    }

    FolderDialog {
        id: relinkFolderDialog
        title: qsTr("Select Directory to Relink Offline Media")
        onAccepted: {
            if (!root.hasLiveTimeline || !selectedFolder) {
                return
            }
            const relinked = timelineModel.relinkOfflineMedia(selectedFolder.toString())
            if (relinked > 0) {
                root.showFeedback(qsTr("Relinked %1 offline asset(s).").arg(relinked), false)
            } else {
                root.showFeedback(qsTr("No offline media relinked in selected folder."), true)
            }
        }
    }

    Popup {
        id: aboutPopup
        anchors.centerIn: Overlay.overlay
        width: 380
        modal: true
        focus: true
        padding: 16
        background: Rectangle {
            color: "#18181d"
            border.color: "#3b82f6"
            border.width: 1
            radius: 4
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: qsTr("Autonomous Video Editor (AVE) v0.1.0")
                color: "#f3f4f6"
                font.bold: true
                font.pixelSize: 13
            }
            Text {
                text: qsTr("Windows-first native C++20 NLE powered by Qt 6.8.3 (LGPLv3), FFmpeg (LGPLv3 shared), Vulkan 1.4, and OpenTimelineIO interchange.")
                color: "#9ca3af"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Close")
                Layout.alignment: Qt.AlignRight
                onClicked: aboutPopup.close()
            }
        }
    }

    Menu {
        id: fileMenu
        MenuItem {
            text: qsTr("New Project")
            enabled: root.hasLiveTimeline
            onTriggered: {
                if (timelineModel.newProject()) {
                    root.showFeedback(qsTr("Created new project."), false)
                } else {
                    root.showFeedback(qsTr("Failed to create new project."), true)
                }
            }
        }
        MenuItem {
            text: qsTr("Open Project...")
            enabled: root.hasLiveTimeline
            onTriggered: openProjectDialog.open()
        }
        MenuItem {
            text: qsTr("Save Project")
            enabled: root.hasLiveTimeline
            onTriggered: {
                if (timelineModel.projectFilePath !== "") {
                    if (timelineModel.saveProject()) {
                        root.showFeedback(qsTr("Project saved."), false)
                    } else {
                        root.showFeedback(qsTr("Failed to save project."), true)
                    }
                } else {
                    saveProjectAsDialog.open()
                }
            }
        }
        MenuItem {
            text: qsTr("Save Project As...")
            enabled: root.hasLiveTimeline
            onTriggered: saveProjectAsDialog.open()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Import Media...")
            enabled: root.hasLiveMediaPool
            onTriggered: importMediaDialog.open()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Export OTIO...")
            enabled: root.hasLiveTimeline
            onTriggered: exportOtioDialog.open()
        }
        MenuItem {
            text: qsTr("Relink Offline Media...")
            enabled: root.hasLiveTimeline
            onTriggered: relinkFolderDialog.open()
        }
    }

    Menu {
        id: editMenu
        MenuItem {
            text: qsTr("Undo")
            enabled: root.hasLiveTimeline && timelineModel.canUndo
            onTriggered: {
                if (!timelineModel.undo()) {
                    root.showFeedback(qsTr("Undo failed."), true)
                }
            }
        }
        MenuItem {
            text: qsTr("Redo")
            enabled: root.hasLiveTimeline && timelineModel.canRedo
            onTriggered: {
                if (!timelineModel.redo()) {
                    root.showFeedback(qsTr("Redo failed."), true)
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Delete Selected Clip")
            enabled: root.hasLiveTimeline && timelineModel.selectedClipId !== ""
            onTriggered: {
                if (!timelineModel.deleteSelected()) {
                    root.showFeedback(qsTr("Could not delete selected clip."), true)
                }
            }
        }
    }

    Menu {
        id: timelineMenu
        MenuItem {
            text: qsTr("Split at Playhead")
            enabled: root.hasLiveTimeline && timelineModel.durationSeconds > 0.0 && root.hasLivePlayback
            onTriggered: {
                if (!timelineModel.splitAtPlayhead(playbackService.playheadPosition)) {
                    root.showFeedback(qsTr("No clip under playhead to split."), true)
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            text: root.effectiveSnapping ? qsTr("Disable Snapping") : qsTr("Enable Snapping")
            enabled: root.hasLiveTimeline
            checkable: true
            checked: root.effectiveSnapping
            onTriggered: timelineModel.snappingEnabled = !timelineModel.snappingEnabled
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Zoom In")
            enabled: root.hasLiveTimeline && timelineModel.zoomFactor < 20.0
            onTriggered: timelineModel.zoomFactor = Math.min(20.0, timelineModel.zoomFactor * 1.25)
        }
        MenuItem {
            text: qsTr("Zoom Out")
            enabled: root.hasLiveTimeline && timelineModel.zoomFactor > 0.1
            onTriggered: timelineModel.zoomFactor = Math.max(0.1, timelineModel.zoomFactor / 1.25)
        }
        MenuItem {
            text: qsTr("Reset Zoom (100%)")
            enabled: root.hasLiveTimeline
            onTriggered: timelineModel.zoomFactor = 1.0
        }
    }

    Menu {
        id: clipMenu
        MenuItem {
            text: qsTr("Split Selected / Playhead")
            enabled: root.hasLiveTimeline && timelineModel.durationSeconds > 0.0 && root.hasLivePlayback
            onTriggered: {
                if (!timelineModel.splitAtPlayhead(playbackService.playheadPosition)) {
                    root.showFeedback(qsTr("No clip under playhead to split."), true)
                }
            }
        }
        MenuItem {
            text: qsTr("Delete Selected Clip")
            enabled: root.hasLiveTimeline && timelineModel.selectedClipId !== ""
            onTriggered: {
                if (!timelineModel.deleteSelected()) {
                    root.showFeedback(qsTr("Could not delete selected clip."), true)
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Speed / Retime — Deferred post-Phase 10")
            enabled: false
        }
    }

    Menu {
        id: viewMenu
        MenuItem {
            text: qsTr("Compositing Enabled")
            enabled: root.hasLivePlayback
            checkable: true
            checked: root.hasLivePlayback && playbackService.compositingEnabled
            onTriggered: playbackService.compositingEnabled = !playbackService.compositingEnabled
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Zoom In")
            enabled: root.hasLiveTimeline && timelineModel.zoomFactor < 20.0
            onTriggered: timelineModel.zoomFactor = Math.min(20.0, timelineModel.zoomFactor * 1.25)
        }
        MenuItem {
            text: qsTr("Zoom Out")
            enabled: root.hasLiveTimeline && timelineModel.zoomFactor > 0.1
            onTriggered: timelineModel.zoomFactor = Math.max(0.1, timelineModel.zoomFactor / 1.25)
        }
        MenuItem {
            text: qsTr("Reset Zoom (100%)")
            enabled: root.hasLiveTimeline
            onTriggered: timelineModel.zoomFactor = 1.0
        }
    }

    Menu {
        id: helpMenu
        MenuItem {
            text: qsTr("About AVE Studio...")
            onTriggered: aboutPopup.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top System / Menu Bar
        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: "#0e0e11"
            border.color: "#202026"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 10

                // App Title
                RowLayout {
                    spacing: 6
                    Rectangle {
                        width: 14
                        height: 14
                        radius: 3
                        color: "#3b82f6"
                        Text {
                            anchors.centerIn: parent
                            text: "A"
                            color: "#ffffff"
                            font.bold: true
                            font.pixelSize: 10
                        }
                    }
                    Text {
                        text: "AVE STUDIO PRO v0.1.0"
                        color: "#f3f4f6"
                        font.bold: true
                        font.pixelSize: 12
                    }
                }

                // Project Name & Dirty Indicator
                Text {
                    id: projectStatusLabel
                    Layout.maximumWidth: 145
                    elide: Text.ElideRight
                    text: root.hasLiveTimeline
                          ? ("| " + timelineModel.projectName + (timelineModel.isDirty ? " *" : ""))
                          : qsTr("| No Project")
                    color: (root.hasLiveTimeline && timelineModel.isDirty) ? "#fbbf24" : "#9ca3af"
                    font.bold: root.hasLiveTimeline && timelineModel.isDirty
                    font.pixelSize: 11

                    HoverHandler {
                        id: projectHover
                    }
                    ToolTip.visible: projectHover.hovered
                    ToolTip.delay: 300
                    ToolTip.text: root.hasLiveTimeline
                                  ? (timelineModel.projectFilePath !== ""
                                     ? timelineModel.projectFilePath
                                     : qsTr("Unsaved project%1").arg(timelineModel.isDirty ? qsTr(" (modified)") : ""))
                                  : qsTr("Timeline service offline")
                }

                // Studio Menus
                RowLayout {
                    spacing: 10

                    Repeater {
                        model: [
                            { key: "file", label: qsTr("File"), enabled: true, menuObj: fileMenu, tip: qsTr("Project, Media Import, OTIO Export & Relink") },
                            { key: "edit", label: qsTr("Edit"), enabled: true, menuObj: editMenu, tip: qsTr("Undo, Redo & Clip Deletion") },
                            { key: "timeline", label: qsTr("Timeline"), enabled: true, menuObj: timelineMenu, tip: qsTr("Split, Snapping & Timeline Zoom") },
                            { key: "clip", label: qsTr("Clip"), enabled: true, menuObj: clipMenu, tip: qsTr("Clip Operations") },
                            { key: "effects", label: qsTr("Effects"), enabled: false, menuObj: null, tip: qsTr("Effects / OpenFX — Deferred post-Phase 10") },
                            { key: "ai", label: qsTr("AI Assistant"), enabled: true, menuObj: null, tip: qsTr("Open AI Assistant Tab") },
                            { key: "view", label: qsTr("View"), enabled: true, menuObj: viewMenu, tip: qsTr("Compositor & Timeline View Options") },
                            { key: "help", label: qsTr("Help"), enabled: true, menuObj: helpMenu, tip: qsTr("About Autonomous Video Editor") }
                        ]

                        Text {
                            id: menuLabel
                            text: modelData.label
                            color: !modelData.enabled
                                   ? "#52525b"
                                   : (menuHover.hovered ? "#f3f4f6" : (modelData.key === "ai" ? "#a78bfa" : "#9ca3af"))
                            font.pixelSize: 11

                            HoverHandler {
                                id: menuHover
                            }
                            ToolTip.visible: menuHover.hovered
                            ToolTip.delay: 300
                            ToolTip.text: modelData.tip

                            MouseArea {
                                anchors.fill: parent
                                enabled: modelData.enabled
                                cursorShape: modelData.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    if (modelData.key === "ai") {
                                        root.openAiAssistantRequested()
                                    } else if (modelData.menuObj) {
                                        modelData.menuObj.popup(menuLabel, 0, menuLabel.height + 4)
                                    }
                                }
                            }
                        }
                    }
                }

                // Action / Error Feedback Message
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    visible: root.feedbackMessage !== ""
                    text: root.feedbackMessage
                    color: root.feedbackIsError ? "#f87171" : "#34d399"
                    font.pixelSize: 10

                    HoverHandler {
                        id: feedbackHover
                    }
                    ToolTip.visible: feedbackHover.hovered && root.feedbackMessage !== ""
                    ToolTip.delay: 300
                    ToolTip.text: root.feedbackMessage
                }

                Item {
                    Layout.fillWidth: true
                    visible: root.feedbackMessage === ""
                }

                // Hardware Status Badge
                RowLayout {
                    spacing: 6
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 3.5
                        color: (!root.hasLivePlayback || !playbackService.hasGpuAcceleration || !playbackService.compositingEnabled)
                               ? "#f59e0b"
                               : "#10b981"
                    }
                    Text {
                        id: hardwareStatusText
                        Layout.maximumWidth: 330
                        elide: Text.ElideRight
                        text: root.hasLivePlayback
                              ? ((playbackService.hasGpuAcceleration
                                  ? (playbackService.gpuDeviceName + " | Vulkan ")
                                  : "CPU Reference | ")
                                 + (playbackService.compositingEnabled ? "Compositor Active" : "Compositor Bypassed")
                                 + " | " + Number(playbackService.fps).toFixed(2) + " FPS")
                              : qsTr("Playback Service Offline")
                        color: (!root.hasLivePlayback || !playbackService.hasGpuAcceleration || !playbackService.compositingEnabled)
                               ? "#f59e0b"
                               : "#10b981"
                        font.bold: true
                        font.pixelSize: 10

                        HoverHandler {
                            id: hardwareHover
                        }
                        ToolTip.visible: hardwareHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: root.hasLivePlayback
                                      ? (hardwareStatusText.text + "\n" + playbackService.audioOutputStatus)
                                      : qsTr("Playback service offline")
                    }
                }
            }
        }

        // Lower Workspace Switcher & Timeline Toolbar
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#16161a"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                // Workspace Switcher Tabs (DaVinci Resolve Style)
                RowLayout {
                    spacing: 2
                    Repeater {
                        model: [
                            { label: "MEDIA", enabled: false, active: false, tip: qsTr("Media Pool is integrated in the Edit workspace") },
                            { label: "EDIT", enabled: true, active: true, tip: qsTr("Edit Workspace (Active)") },
                            { label: "FUSION/FX", enabled: false, active: false, tip: qsTr("Fusion / OpenFX — Deferred post-Phase 10") },
                            { label: "COLOR", enabled: false, active: false, tip: qsTr("Color Grading & OCIO — Deferred post-Phase 10") },
                            { label: "AUDIO", enabled: false, active: false, tip: qsTr("Audio Mixer & Meters are integrated in the Edit workspace") },
                            { label: "DELIVER", enabled: false, active: false, tip: qsTr("Render / Export Pipeline — Deferred post-Phase 10") }
                        ]
                        Rectangle {
                            width: 58
                            height: 26
                            radius: 3
                            color: modelData.active ? "#2563eb" : "#1a1a20"
                            border.color: modelData.active ? "#3b82f6" : "#2a2a35"
                            border.width: 1
                            opacity: modelData.active ? 1.0 : 0.48

                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: modelData.active ? "#ffffff" : "#9ca3af"
                                font.bold: true
                                font.pixelSize: 9
                            }

                            HoverHandler {
                                id: tabHover
                            }
                            ToolTip.visible: tabHover.hovered
                            ToolTip.delay: 300
                            ToolTip.text: modelData.tip
                        }
                    }
                }

                Rectangle { width: 1; height: 20; color: "#2e2e38" }

                // Editing Tools Palette
                RowLayout {
                    spacing: 4

                    Button {
                        text: "↖ Select (V)"
                        checkable: true
                        checked: root.effectiveActiveTool === 0
                        highlighted: checked
                        enabled: root.hasLiveTimeline
                        Layout.preferredHeight: 26
                        onClicked: {
                            if (root.hasLiveTimeline) {
                                timelineModel.activeTool = 0
                            }
                        }
                    }
                    Button {
                        text: "✂ Blade (C)"
                        checkable: true
                        checked: root.effectiveActiveTool === 1
                        highlighted: checked
                        enabled: root.hasLiveTimeline
                        Layout.preferredHeight: 26
                        onClicked: {
                            if (root.hasLiveTimeline) {
                                timelineModel.activeTool = 1
                            }
                        }
                    }
                    Button {
                        text: "⇥ Trim (T)"
                        checkable: true
                        checked: root.effectiveActiveTool === 2
                        enabled: false
                        opacity: 0.55
                        Layout.preferredHeight: 26
                        HoverHandler {
                            id: trimHover
                        }
                        ToolTip.visible: trimHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Trim / Ripple Edit (Tool 2) — Deferred post-Phase 10")
                    }
                    Button {
                        text: "↔ Slip (Y)"
                        checkable: true
                        checked: root.effectiveActiveTool === 3
                        enabled: false
                        opacity: 0.55
                        Layout.preferredHeight: 26
                        HoverHandler {
                            id: slipHover
                        }
                        ToolTip.visible: slipHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Slip Tool (Tool 3) — Deferred post-Phase 10")
                    }
                }

                Rectangle { width: 1; height: 20; color: "#2e2e38" }

                // Undo / Redo
                RowLayout {
                    spacing: 4

                    Button {
                        text: "↶"
                        enabled: root.hasLiveTimeline && timelineModel.canUndo
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        HoverHandler {
                            id: undoHover
                        }
                        ToolTip.visible: undoHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Undo (Ctrl+Z)")
                        onClicked: {
                            if (root.hasLiveTimeline && !timelineModel.undo()) {
                                root.showFeedback(qsTr("Undo failed."), true)
                            }
                        }
                    }
                    Button {
                        text: "↷"
                        enabled: root.hasLiveTimeline && timelineModel.canRedo
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        HoverHandler {
                            id: redoHover
                        }
                        ToolTip.visible: redoHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Redo (Ctrl+Y)")
                        onClicked: {
                            if (root.hasLiveTimeline && !timelineModel.redo()) {
                                root.showFeedback(qsTr("Redo failed."), true)
                            }
                        }
                    }
                }

                // Snapping Toggle
                Button {
                    text: root.effectiveSnapping ? "🧲 Snap: ON" : "Snap: OFF"
                    checkable: true
                    checked: root.effectiveSnapping
                    enabled: root.hasLiveTimeline
                    Layout.preferredHeight: 26
                    onClicked: {
                        if (root.hasLiveTimeline) {
                            timelineModel.snappingEnabled = !timelineModel.snappingEnabled
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Glowing SMPTE Master Timecode
                Rectangle {
                    width: 125
                    height: 26
                    color: "#09090b"
                    radius: 3
                    border.color: "#3b82f6"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: root.hasLivePlayback ? playbackService.timecode : "00:00:00:00"
                        color: "#38bdf8"
                        font.family: "Consolas, monospace"
                        font.bold: true
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
}
