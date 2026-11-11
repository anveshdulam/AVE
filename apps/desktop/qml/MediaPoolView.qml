import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    color: "#141417"
    border.color: "#282830"
    border.width: 1

    signal assetSelected(string assetName, string duration, string resolution)

    readonly property bool hasLiveMediaPool: (typeof mediaPoolModel !== "undefined" && mediaPoolModel !== null)
    readonly property bool hasLiveTimeline: (typeof timelineModel !== "undefined" && timelineModel !== null)

    property string mediaFilter: "all" // "all" | "video" | "audio"
    property bool errorDismissed: false
    property string actionError: ""
    readonly property string modelImportError: (hasLiveMediaPool && !errorDismissed)
                                               ? mediaPoolModel.lastImportError
                                               : ""
    readonly property string activeError: actionError !== "" ? actionError : modelImportError

    function matchesFilter(mediaType) {
        if (mediaFilter === "all") return true
        if (mediaFilter === "video") return mediaType === "Video"
        if (mediaFilter === "audio") return mediaType === "Audio"
        return true
    }

    function fallbackBadgeLabel(mediaType) {
        if (mediaType === "Video") return "VIDEO"
        if (mediaType === "Audio") return "AUDIO"
        return "UNKNOWN"
    }

    function insertAsset(assetId) {
        if (!hasLiveTimeline || !assetId || assetId === "") {
            root.actionError = qsTr("Timeline service unavailable or invalid asset.")
            return
        }
        if (!timelineModel.insertClipFromAsset(assetId)) {
            root.actionError = qsTr("Could not insert clip onto timeline.")
        } else {
            root.actionError = ""
        }
    }

    Connections {
        target: root.hasLiveMediaPool ? mediaPoolModel : null
        function onLastImportErrorChanged() {
            root.errorDismissed = false
        }
        function onModelReset() {
            assetList.currentIndex = -1
        }
    }

    FileDialog {
        id: importFileDialog
        title: qsTr("Import Media Files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.mxf *.wav *.mp3 *.flac *.aac *.m4a *.ogg)"),
            qsTr("All files (*)")
        ]
        onAccepted: {
            if (root.hasLiveMediaPool && selectedFiles.length > 0) {
                root.actionError = ""
                mediaPoolModel.importMediaFiles(selectedFiles)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Panel Title Bar
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color: "#18181d"
            border.color: "#282830"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10

                Text {
                    text: qsTr("MEDIA POOL")
                    color: "#d1d5db"
                    font.bold: true
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: root.hasLiveMediaPool
                          ? (mediaPoolModel.assetCount
                             + (mediaPoolModel.assetCount === 1 ? qsTr(" Asset") : qsTr(" Assets"))
                             + (mediaPoolModel.isImporting ? qsTr(" | Importing") : qsTr(" | Master")))
                          : qsTr("Offline | Master")
                    color: (root.hasLiveMediaPool && mediaPoolModel.isImporting) ? "#60a5fa" : "#6b7280"
                    font.pixelSize: 10
                }
            }
        }

        // Filter & Import Strip
        Rectangle {
            Layout.fillWidth: true
            height: 26
            color: "#121215"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 6
                spacing: 12

                Repeater {
                    model: [
                        { key: "all", label: qsTr("All"), hint: qsTr("Show all imported media assets") },
                        { key: "video", label: qsTr("Video"), hint: qsTr("Filter to video assets") },
                        { key: "audio", label: qsTr("Audio"), hint: qsTr("Filter to audio assets") }
                    ]

                    Text {
                        text: modelData.label
                        color: root.mediaFilter === modelData.key ? "#93c5fd" : "#6b7280"
                        font.bold: root.mediaFilter === modelData.key
                        font.pixelSize: 11
                        ToolTip.visible: filterMouse.containsMouse
                        ToolTip.delay: 300
                        ToolTip.text: modelData.hint
                        MouseArea {
                            id: filterMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.mediaFilter = modelData.key
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("+ Import")
                    enabled: root.hasLiveMediaPool
                    font.pixelSize: 10
                    Layout.preferredHeight: 20
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Import video/audio files via native file dialog")
                    onClicked: importFileDialog.open()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#23232b" }

        // Asset List & Drag-and-Drop Zone
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: assetList
                anchors.fill: parent
                clip: true
                currentIndex: -1

                model: root.hasLiveMediaPool ? mediaPoolModel : []

                delegate: Rectangle {
                    readonly property bool rowVisible: root.matchesFilter(model.mediaType)
                    width: ListView.view.width
                    height: rowVisible ? 52 : 0
                    visible: rowVisible
                    enabled: rowVisible
                    clip: true
                    color: assetList.currentIndex === index ? "#262633" : (index % 2 === 0 ? "#16161a" : "#1a1a20")
                    border.color: !model.isOnline
                                  ? "#ef4444"
                                  : (assetList.currentIndex === index ? "#3b82f6" : "#222228")
                    border.width: 1

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            assetList.currentIndex = index
                            root.assetSelected(model.name, model.durationText, model.resolutionText)
                        }
                        onDoubleClicked: {
                            assetList.currentIndex = index
                            root.insertAsset(model.assetId)
                        }
                    }

                    ToolTip.visible: rowMouse.containsMouse
                    ToolTip.delay: 400
                    ToolTip.text: (model.isOnline ? "" : qsTr("[OFFLINE] "))
                                  + model.filePath
                                  + " | " + Number(model.durationSeconds).toFixed(2) + "s"
                                  + (model.codecText !== "" ? (" | " + model.codecText) : "")
                                  + (model.isOnline ? "" : qsTr(" | Relink via File > Relink Offline Media..."))

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 10

                        // Thumbnail / Honest Type Badge
                        Rectangle {
                            width: 48
                            height: 34
                            radius: 3
                            clip: true
                            color: !model.isOnline
                                   ? "#3f1518"
                                   : (model.mediaType === "Video" ? "#1e293b"
                                      : (model.mediaType === "Audio" ? "#064e3b" : "#27272a"))
                            border.color: !model.isOnline
                                          ? "#ef4444"
                                          : (model.mediaType === "Video" ? "#38bdf8"
                                             : (model.mediaType === "Audio" ? "#10b981" : "#71717a"))
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                visible: !thumbImage.visible
                                text: root.fallbackBadgeLabel(model.mediaType)
                                color: !model.isOnline
                                       ? "#fca5a5"
                                       : (model.mediaType === "Video" ? "#38bdf8"
                                          : (model.mediaType === "Audio" ? "#34d399" : "#a1a1aa"))
                                font.bold: true
                                font.pixelSize: 8
                            }

                            Image {
                                id: thumbImage
                                anchors.fill: parent
                                anchors.margins: 1
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                source: model.thumbnailDataUrl || ""
                                visible: model.thumbnailDataUrl !== "" && status === Image.Ready
                                opacity: model.isOnline ? 1.0 : 0.45
                            }
                        }

                        // Metadata Columns
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: model.name
                                    color: model.isOnline ? "#f3f4f6" : "#fca5a5"
                                    font.bold: true
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    visible: !model.isOnline
                                    color: "#450a0a"
                                    border.color: "#ef4444"
                                    border.width: 1
                                    radius: 2
                                    implicitWidth: offlineLabel.implicitWidth + 6
                                    implicitHeight: 14

                                    Text {
                                        id: offlineLabel
                                        anchors.centerIn: parent
                                        text: qsTr("OFFLINE")
                                        color: "#fca5a5"
                                        font.bold: true
                                        font.pixelSize: 8
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: model.durationText
                                    color: "#9ca3af"
                                    font.family: "Consolas, monospace"
                                    font.pixelSize: 10
                                }
                                Text {
                                    visible: text !== ""
                                    text: model.resolutionText
                                    color: "#6b7280"
                                    font.pixelSize: 10
                                }
                                Text {
                                    visible: text !== ""
                                    text: model.fpsText
                                    color: "#6b7280"
                                    font.pixelSize: 10
                                }
                                Text {
                                    visible: text !== ""
                                    text: model.codecText
                                    color: "#52525b"
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Button {
                            text: "+"
                            enabled: root.hasLiveTimeline
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 22
                            ToolTip.visible: hovered
                            ToolTip.delay: 300
                            ToolTip.text: qsTr("Insert asset onto timeline")
                            onClicked: {
                                assetList.currentIndex = index
                                root.insertAsset(model.assetId)
                            }
                        }
                    }
                }
            }

            // Honest Empty Pool / Empty Filter Hint
            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: !root.hasLiveMediaPool
                         || mediaPoolModel.assetCount === 0
                         || (root.mediaFilter !== "all" && assetList.contentHeight <= 0)
                text: !root.hasLiveMediaPool
                      ? qsTr("Media pool service offline")
                      : (mediaPoolModel.assetCount === 0
                         ? qsTr("No media imported.\nDrop files here or click + Import.")
                         : qsTr("No %1 assets in Media Pool.").arg(root.mediaFilter === "video" ? qsTr("video") : qsTr("audio")))
                color: "#6b7280"
                font.pixelSize: 11
            }

            // Drag & Drop Import Overlay
            DropArea {
                id: mediaDropArea
                anchors.fill: parent
                onDropped: (drop) => {
                    if (drop.hasUrls && root.hasLiveMediaPool) {
                        root.actionError = ""
                        mediaPoolModel.importMediaFiles(drop.urls)
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: mediaDropArea.containsDrag
                color: "#1e3a8a33"
                border.color: "#3b82f6"
                border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Drop media files to import")
                    color: "#93c5fd"
                    font.bold: true
                    font.pixelSize: 12
                }
            }
        }

        // Bottom Ingest / Import Status Strip
        Rectangle {
            Layout.fillWidth: true
            height: 24
            color: "#101013"
            border.color: "#282830"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 6
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 9
                    color: (root.hasLiveMediaPool && mediaPoolModel.isImporting)
                           ? "#60a5fa"
                           : (root.activeError !== "" ? "#f87171" : "#6b7280")
                    text: {
                        if (root.hasLiveMediaPool && mediaPoolModel.isImporting) {
                            var base = qsTr("Importing: %1 active, %2 pending")
                                .arg(mediaPoolModel.activeImportCount)
                                .arg(mediaPoolModel.pendingImportCount)
                            return root.activeError !== "" ? (base + " | " + root.activeError) : base
                        }
                        if (root.activeError !== "") {
                            return qsTr("Import error: ") + root.activeError
                        }
                        return qsTr("Drop media files to import (FFmpeg LGPL probe active)")
                    }

                    HoverHandler {
                        id: statusHover
                    }
                    ToolTip.visible: statusHover.hovered && (root.activeError !== "" || (root.hasLiveMediaPool && mediaPoolModel.isImporting))
                    ToolTip.delay: 300
                    ToolTip.text: root.activeError !== ""
                                  ? root.activeError
                                  : qsTr("Active: %1 | Pending: %2").arg(root.hasLiveMediaPool ? mediaPoolModel.activeImportCount : 0).arg(root.hasLiveMediaPool ? mediaPoolModel.pendingImportCount : 0)
                }

                Button {
                    text: qsTr("Cancel")
                    visible: root.hasLiveMediaPool && mediaPoolModel.isImporting
                    Layout.preferredHeight: 18
                    font.pixelSize: 9
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Cancel all active and pending imports")
                    onClicked: {
                        if (root.hasLiveMediaPool) {
                            mediaPoolModel.cancelAllImports()
                        }
                    }
                }

                Button {
                    text: "✕"
                    visible: (!root.hasLiveMediaPool || !mediaPoolModel.isImporting) && root.activeError !== ""
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    font.pixelSize: 9
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Dismiss error message")
                    onClicked: {
                        root.actionError = ""
                        root.errorDismissed = true
                    }
                }
            }
        }
    }
}


