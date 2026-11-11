import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#0a0a0c"
    border.color: "#282830"
    border.width: 1

    property bool isPlaying: false
    property string timecode: "01:00:14:18"
    property real playheadRatio: 0.28

    readonly property bool hasLiveService: (typeof playbackService !== "undefined" && playbackService !== null)
    readonly property bool hasTimelineDuration: hasLiveService && playbackService.durationSeconds > 0.0
    readonly property bool effectivePlaying: hasLiveService ? playbackService.isPlaying : root.isPlaying
    readonly property string effectiveTimecode: hasLiveService ? playbackService.timecode : root.timecode
    readonly property real effectivePlayheadRatio: hasLiveService
                                                   ? (playbackService.durationSeconds > 0.0
                                                      ? Math.min(1.0, Math.max(0.0, playbackService.playheadPosition / playbackService.durationSeconds))
                                                      : 0.0)
                                                   : root.playheadRatio

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Monitor Header
        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: "#141418"
            border.color: "#24242c"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10

                Text {
                    text: qsTr("PROGRAM MONITOR (Rec.709)")
                    color: "#e5e7eb"
                    font.bold: true
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 7
                    height: 7
                    radius: 3.5
                    color: (root.hasLiveService && !playbackService.hasGpuAcceleration) ? "#f59e0b" : "#10b981"
                }

                Text {
                    text: root.hasLiveService
                          ? ((playbackService.hasGpuAcceleration
                              ? (playbackService.gpuDeviceName + " | Vulkan Compositor ")
                              : "CPU Reference Compositor ")
                             + (playbackService.compositingEnabled ? "Active" : "Bypassed")
                             + " | " + Number(playbackService.fps).toFixed(2) + " fps")
                          : "Vulkan Compositor Active | 1920x1080 @ 23.98 fps"
                    color: (root.hasLiveService && !playbackService.hasGpuAcceleration) ? "#f59e0b" : "#10b981"
                    font.bold: true
                    font.pixelSize: 10
                }
            }
        }

        // Viewport 16:9 Display Area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#050507"

            // 16:9 Monitor Frame
            Item {
                id: monitorFrame
                width: Math.min(parent.width - 24, (parent.height - 24) * (16.0 / 9.0))
                height: width * (9.0 / 16.0)
                anchors.centerIn: parent

                Rectangle {
                    anchors.fill: parent
                    color: "#000000"
                    border.color: "#33333e"
                    border.width: 1

                    // Backdrop & Live Async Vulkan Readback Frame
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#0f172a" }
                            GradientStop { position: 0.5; color: "#1e1b4b" }
                            GradientStop { position: 1.0; color: "#090d16" }
                        }

                        Image {
                            id: liveFrameImage
                            anchors.fill: parent
                            asynchronous: true
                            cache: false
                            retainWhileLoading: true
                            fillMode: Image.PreserveAspectFit
                            source: (root.hasLiveService && playbackService.currentFrameSeq > 0)
                                    ? ("image://aveframe/" + playbackService.currentFrameSeq)
                                    : ""
                        }

                        // Safe Action Guide (90%)
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width * 0.90
                            height: parent.height * 0.90
                            color: "transparent"
                            border.color: "#2563eb"
                            border.width: 1
                            opacity: 0.35
                        }

                        // Safe Title Guide (80%)
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width * 0.80
                            height: parent.height * 0.80
                            color: "transparent"
                            border.color: "#38bdf8"
                            border.width: 1
                            opacity: 0.25
                        }

                        // Center Reticle
                        Rectangle {
                            anchors.centerIn: parent
                            width: 16
                            height: 1
                            color: "#ffffff"
                            opacity: 0.3
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 1
                            height: 16
                            color: "#ffffff"
                            opacity: 0.3
                        }

                        // Center Text (shown only before first live frame becomes Ready)
                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 4
                            visible: liveFrameImage.status !== Image.Ready

                            Text {
                                text: !root.hasLiveService
                                      ? qsTr("AVE VIDEO MONITOR (STANDBY)")
                                      : (playbackService.hasGpuAcceleration
                                         ? qsTr("AVE VULKAN PREVIEW (%1)").arg(playbackService.compositingEnabled ? qsTr("READY") : qsTr("BYPASSED"))
                                         : qsTr("AVE CPU REFERENCE PREVIEW (%1)").arg(playbackService.compositingEnabled ? qsTr("READY") : qsTr("BYPASSED")))
                                color: "#f8fafc"
                                font.bold: true
                                font.pixelSize: 13
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Text {
                                text: !root.hasLiveService
                                      ? qsTr("Playback service offline")
                                      : (playbackService.durationSeconds <= 0.0
                                         ? qsTr("Import media and place a clip on the timeline, or press Play")
                                         : (playbackService.hasGpuAcceleration
                                            ? (playbackService.gpuDeviceName
                                               + (playbackService.compositingEnabled
                                                  ? qsTr(" | Press Play or Scrub to preview")
                                                  : qsTr(" | Compositing bypassed — press Play or Scrub")))
                                            : (playbackService.compositingEnabled
                                               ? qsTr("CPU Reference Compositor | Press Play or Scrub to preview")
                                               : qsTr("CPU Reference Compositor | Compositing bypassed"))))
                                color: "#94a3b8"
                                font.pixelSize: 11
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }

                        // Timecode Overlay in Monitor
                        Rectangle {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 10
                            width: 110
                            height: 22
                            color: "#cc000000"
                            radius: 3
                            border.color: "#3b82f6"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: root.effectiveTimecode
                                color: "#60a5fa"
                                font.family: "Consolas, monospace"
                                font.bold: true
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }

        // Sub-Monitor Mini Scrubber & Transport Bar
        Rectangle {
            Layout.fillWidth: true
            height: 38
            color: "#141418"
            border.color: "#24242c"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                // Mark In Button
                Button {
                    text: "{"
                    enabled: false
                    HoverHandler {
                        id: markInHover
                    }
                    ToolTip.visible: markInHover.hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Mark In (I) — Deferred post-Phase 10")
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 24
                }

                // Step Prev Frame
                Button {
                    text: "◀◀"
                    ToolTip.visible: hovered
                    ToolTip.text: "Step -1 Frame (Left Arrow)"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 24
                    onClicked: {
                        if (root.hasLiveService) {
                            playbackService.stepBackward(1)
                        }
                    }
                }

                // Play / Pause Button
                Button {
                    text: root.effectivePlaying ? "⏸" : "▶"
                    highlighted: true
                    ToolTip.visible: hovered
                    ToolTip.text: root.effectivePlaying ? "Pause (Space)" : "Play (Space)"
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 26
                    onClicked: {
                        if (root.hasLiveService) {
                            playbackService.togglePlay()
                        } else {
                            root.isPlaying = !root.isPlaying
                        }
                    }
                }

                // Step Next Frame
                Button {
                    text: "▶▶"
                    ToolTip.visible: hovered
                    ToolTip.text: "Step +1 Frame (Right Arrow)"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 24
                    onClicked: {
                        if (root.hasLiveService) {
                            playbackService.stepForward(1)
                        }
                    }
                }

                // Mark Out Button
                Button {
                    text: "}"
                    enabled: false
                    HoverHandler {
                        id: markOutHover
                    }
                    ToolTip.visible: markOutHover.hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Mark Out (O) — Deferred post-Phase 10")
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 24
                }

                // Mini Scrubber Slider
                Slider {
                    id: scrubberSlider
                    Layout.fillWidth: true
                    enabled: !root.hasLiveService || root.hasTimelineDuration
                    from: 0.0
                    to: 1.0
                    Binding {
                        target: scrubberSlider
                        property: "value"
                        value: root.effectivePlayheadRatio
                        when: !scrubberSlider.pressed
                    }
                    onMoved: {
                        if (root.hasLiveService) {
                            if (playbackService.durationSeconds > 0.0) {
                                playbackService.seekSeconds(value * playbackService.durationSeconds)
                            }
                        } else {
                            root.playheadRatio = value
                        }
                    }
                }

                // Timecode
                Text {
                    text: root.effectiveTimecode
                    color: "#38bdf8"
                    font.family: "Consolas, monospace"
                    font.bold: true
                    font.pixelSize: 12
                }
            }
        }
    }
}

