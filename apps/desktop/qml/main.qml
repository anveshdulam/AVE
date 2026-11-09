import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 600
    title: qsTr("Autonomous Video Editor (AVE) — Native C++20 Shell")
    color: "#18181b" // Dark theme zinc-900

    // Top Header & Transport Toolbar
    header: ToolbarView {
        id: toolbar
        height: 48
    }

    // Main Work Area Splitter
    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        // Upper Zone: Viewer (Left) & Inspector (Right)
        SplitView {
            SplitView.fillWidth: true
            SplitView.preferredHeight: 460
            orientation: Qt.Horizontal

            // Viewer Shell
            Rectangle {
                SplitView.fillWidth: true
                color: "#09090b"
                border.color: "#27272a"
                border.width: 1

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 12

                    Rectangle {
                        width: 640
                        height: 360
                        color: "#000000"
                        border.color: "#3f3f46"
                        border.width: 2
                        radius: 4

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Vulkan Offscreen Render Graph Viewport\n(1920x1080 @ 24fps)")
                            color: "#71717a"
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: 14
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Hardware Accelerator: Intel(R) Arc(TM) Graphics (Vulkan 1.4)")
                        color: "#10b981"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }

            // Right: Inspector & AI Assistant Panel
            InspectorView {
                SplitView.preferredWidth: 380
                SplitView.minimumWidth: 300
            }
        }

        // Lower Zone: Multi-Track Timeline & Track Headers
        Rectangle {
            SplitView.fillWidth: true
            SplitView.preferredHeight: 380
            SplitView.minimumHeight: 200
            color: "#18181b"
            border.color: "#27272a"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                spacing: 0

                // Track Headers (Mute, Solo, Lock)
                TrackHeaderView {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 220
                }

                // Timeline Canvas (Ruler, Tracks, Clips, Playhead)
                TimelineCanvas {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }
    }
}
