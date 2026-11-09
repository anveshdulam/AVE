import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#16161a"
    border.color: "#282832"
    border.width: 1

    property int trackHeight: 64

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Track Header Top Bar (Aligned with Timeline Ruler)
        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: "#121215"
            border.color: "#282832"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Text {
                    text: qsTr("TRACKS")
                    color: "#9ca3af"
                    font.bold: true
                    font.pixelSize: 10
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "4 Total (2V / 2A)"
                    color: "#6b7280"
                    font.pixelSize: 9
                }
            }
        }

        // Track Header Strips
        ListView {
            id: trackListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            interactive: false

            model: [
                { id: "V2", name: "V2 (Overlay)", type: "video", muted: false, solo: false, locked: false, vol: 0 },
                { id: "V1", name: "V1 (Main Video)", type: "video", muted: false, solo: false, locked: false, vol: 0 },
                { id: "A1", name: "A1 (Dialogue/SFX)", type: "audio", muted: false, solo: false, locked: false, vol: 0 },
                { id: "A2", name: "A2 (Music Score)", type: "audio", muted: false, solo: false, locked: false, vol: -3 }
            ]

            delegate: Rectangle {
                width: trackListView.width
                height: root.trackHeight
                color: index % 2 === 0 ? "#17171c" : "#1a1a20"
                border.color: "#262630"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 6

                    // Track Color Tag Indicator
                    Rectangle {
                        width: 4
                        height: 48
                        radius: 2
                        color: modelData.type === "video" ? "#38bdf8" : "#10b981"
                    }

                    // Track ID & Name
                    ColumnLayout {
                        Layout.preferredWidth: 95
                        spacing: 2

                        Text {
                            text: modelData.id
                            color: modelData.type === "video" ? "#38bdf8" : "#10b981"
                            font.bold: true
                            font.pixelSize: 13
                        }

                        Text {
                            text: modelData.name
                            color: "#9ca3af"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    // Track Action Buttons
                    RowLayout {
                        spacing: 4

                        // Lock Button
                        Button {
                            text: "🔒"
                            checkable: true
                            checked: modelData.locked
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            ToolTip.visible: hovered
                            ToolTip.text: "Lock Track"
                        }

                        // Eye / Mute Button
                        Button {
                            text: modelData.type === "video" ? "👁" : "M"
                            checkable: true
                            checked: modelData.muted
                            highlighted: checked
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.type === "video" ? "Hide Video Track" : "Mute Audio Track"
                        }

                        // Solo Button (for Audio)
                        Button {
                            visible: modelData.type === "audio"
                            text: "S"
                            checkable: true
                            checked: modelData.solo
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            ToolTip.visible: hovered
                            ToolTip.text: "Solo Audio Track"
                        }
                    }
                }
            }
        }
    }
}
