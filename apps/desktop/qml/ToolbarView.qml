import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#121215"
    border.color: "#282832"
    border.width: 1

    property int activeWorkspace: 1 // 0: Media, 1: Edit, 2: Fusion, 3: Color, 4: Fairlight, 5: Deliver
    property int activeTool: 0 // 0: Select, 1: Blade, 2: Ripple, 3: Slip
    property bool snapping: true

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
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 16

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

                // Studio Menus
                RowLayout {
                    spacing: 14
                    Repeater {
                        model: ["File", "Edit", "Timeline", "Clip", "Effects", "AI Assistant", "View", "Help"]
                        Text {
                            text: modelData
                            color: "#9ca3af"
                            font.pixelSize: 11
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Hardware Status Badge
                RowLayout {
                    spacing: 8
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 3.5
                        color: "#10b981"
                    }
                    Text {
                        text: "Intel(R) Arc(TM) Graphics | Vulkan 1.4 Compute | 24.00 FPS"
                        color: "#10b981"
                        font.bold: true
                        font.pixelSize: 10
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
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 16

                // Workspace Switcher Tabs (DaVinci Resolve Style)
                RowLayout {
                    spacing: 2
                    Repeater {
                        model: ["MEDIA", "EDIT", "FUSION/FX", "COLOR", "AUDIO", "DELIVER"]
                        Rectangle {
                            width: 70
                            height: 26
                            radius: 3
                            color: root.activeWorkspace === index ? "#2563eb" : "#1a1a20"
                            border.color: root.activeWorkspace === index ? "#3b82f6" : "#2a2a35"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: root.activeWorkspace === index ? "#ffffff" : "#9ca3af"
                                font.bold: true
                                font.pixelSize: 9
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.activeWorkspace = index
                            }
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
                        checked: root.activeTool === 0
                        highlighted: checked
                        Layout.preferredHeight: 26
                        onClicked: root.activeTool = 0
                    }
                    Button {
                        text: "✂ Blade (C)"
                        checkable: true
                        checked: root.activeTool === 1
                        highlighted: checked
                        Layout.preferredHeight: 26
                        onClicked: root.activeTool = 1
                    }
                    Button {
                        text: "⇥ Ripple (B)"
                        checkable: true
                        checked: root.activeTool === 2
                        highlighted: checked
                        Layout.preferredHeight: 26
                        onClicked: root.activeTool = 2
                    }
                    Button {
                        text: "↔ Slip (Y)"
                        checkable: true
                        checked: root.activeTool === 3
                        highlighted: checked
                        Layout.preferredHeight: 26
                        onClicked: root.activeTool = 3
                    }
                }

                Rectangle { width: 1; height: 20; color: "#2e2e38" }

                // Snapping Toggle
                Button {
                    text: root.snapping ? "🧲 Snap: ON" : "Snap: OFF"
                    checkable: true
                    checked: root.snapping
                    Layout.preferredHeight: 26
                    onClicked: root.snapping = !root.snapping
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
                        text: "01:00:14:18"
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
