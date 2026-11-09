import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#27272a"
    border.color: "#3f3f46"
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 16

        // Brand / Title
        Text {
            text: qsTr("AVE v0.1.0")
            color: "#f4f4f5"
            font.bold: true
            font.pixelSize: 14
        }

        Rectangle { width: 1; height: 24; color: "#3f3f46" }

        // Transport Controls
        RowLayout {
            spacing: 6

            Button {
                text: "◀◀"
                ToolTip.visible: hovered
                ToolTip.text: "Step -1 Frame"
            }
            Button {
                text: "▶ Play"
                highlighted: true
                ToolTip.visible: hovered
                ToolTip.text: "Start Real-Time Playback"
            }
            Button {
                text: "⏸ Pause"
                ToolTip.visible: hovered
                ToolTip.text: "Pause Transport"
            }
            Button {
                text: "▶▶"
                ToolTip.visible: hovered
                ToolTip.text: "Step +1 Frame"
            }
        }

        // SMPTE Timecode Display
        Rectangle {
            width: 130
            height: 32
            color: "#09090b"
            radius: 4
            border.color: "#3f3f46"

            Text {
                anchors.centerIn: parent
                text: "00:00:00:00"
                color: "#38bdf8"
                font.family: "Consolas, monospace"
                font.bold: true
                font.pixelSize: 15
            }
        }

        Rectangle { width: 1; height: 24; color: "#3f3f46" }

        // Edit Tools (Pointer, Blade, Trim)
        RowLayout {
            spacing: 4

            Button {
                text: "↖ Select"
                checkable: true
                checked: true
            }
            Button {
                text: "✂ Blade"
                checkable: true
            }
            Button {
                text: "⇥ Trim"
                checkable: true
            }
        }

        Rectangle { width: 1; height: 24; color: "#3f3f46" }

        // Snapping Toggle
        CheckBox {
            text: "Snap (10px)"
            checked: true
        }

        Item { Layout.fillWidth: true }

        // Undo / Redo buttons
        Button {
            text: "↶ Undo"
        }
        Button {
            text: "↷ Redo"
        }

        Rectangle { width: 1; height: 24; color: "#3f3f46" }

        // Zoom Slider
        RowLayout {
            spacing: 6
            Text { text: "Zoom"; color: "#a1a1aa"; font.pixelSize: 12 }
            Slider {
                from: 0.1
                to: 5.0
                value: 1.0
                Layout.preferredWidth: 100
            }
        }
    }
}
