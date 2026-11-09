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
                    color: "#10b981"
                }

                Text {
                    text: "Vulkan Compositor Active | 1920x1080 @ 23.98 fps"
                    color: "#10b981"
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

                    // Simulated Video Frame Content
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#0f172a" }
                            GradientStop { position: 0.5; color: "#1e1b4b" }
                            GradientStop { position: 1.0; color: "#090d16" }
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

                        // Center Text
                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                text: "AVE REAL-TIME VULKAN ENGINE"
                                color: "#f8fafc"
                                font.bold: true
                                font.pixelSize: 13
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Text {
                                text: "Hardware Staging & Compute Shaders Active"
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
                                text: root.timecode
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
                    ToolTip.visible: hovered
                    ToolTip.text: "Mark In (I)"
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
                }

                // Play / Pause Button
                Button {
                    text: root.isPlaying ? "⏸" : "▶"
                    highlighted: true
                    ToolTip.visible: hovered
                    ToolTip.text: root.isPlaying ? "Pause (Space)" : "Play (Space)"
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 26
                    onClicked: root.isPlaying = !root.isPlaying
                }

                // Step Next Frame
                Button {
                    text: "▶▶"
                    ToolTip.visible: hovered
                    ToolTip.text: "Step +1 Frame (Right Arrow)"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 24
                }

                // Mark Out Button
                Button {
                    text: "}"
                    ToolTip.visible: hovered
                    ToolTip.text: "Mark Out (O)"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 24
                }

                // Mini Scrubber Slider
                Slider {
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    value: root.playheadRatio
                    onMoved: root.playheadRatio = value
                }

                // Timecode
                Text {
                    text: root.timecode
                    color: "#38bdf8"
                    font.family: "Consolas, monospace"
                    font.bold: true
                    font.pixelSize: 12
                }
            }
        }
    }
}
