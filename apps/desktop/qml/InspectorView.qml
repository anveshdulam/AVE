import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#202024"
    border.color: "#27272a"
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        Text {
            text: qsTr("INSPECTOR")
            color: "#a1a1aa"
            font.bold: true
            font.pixelSize: 11
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2f2f36" }

        // Selected clip title
        Text {
            text: "PiP Overlay.mp4"
            color: "#ffffff"
            font.bold: true
            font.pixelSize: 15
        }

        Text {
            text: "Track: V2 | Duration: 00:00:03:00"
            color: "#71717a"
            font.pixelSize: 12
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2f2f36" }

        Text {
            text: qsTr("Spatial Transforms (GPU)")
            color: "#38bdf8"
            font.bold: true
            font.pixelSize: 12
        }

        // Position X / Y
        RowLayout {
            spacing: 8
            Text { text: "Pos X:"; color: "#d4d4d8"; font.pixelSize: 12; Layout.preferredWidth: 40 }
            Slider { id: posX; from: -500; to: 500; value: 40; Layout.fillWidth: true }
            Text { text: Math.round(posX.value) + " px"; color: "#a1a1aa"; font.pixelSize: 12; Layout.preferredWidth: 45 }
        }

        RowLayout {
            spacing: 8
            Text { text: "Pos Y:"; color: "#d4d4d8"; font.pixelSize: 12; Layout.preferredWidth: 40 }
            Slider { id: posY; from: -500; to: 500; value: 30; Layout.fillWidth: true }
            Text { text: Math.round(posY.value) + " px"; color: "#a1a1aa"; font.pixelSize: 12; Layout.preferredWidth: 45 }
        }

        // Scale
        RowLayout {
            spacing: 8
            Text { text: "Scale:"; color: "#d4d4d8"; font.pixelSize: 12; Layout.preferredWidth: 40 }
            Slider { id: scaleSlider; from: 0.1; to: 2.0; value: 0.5; Layout.fillWidth: true }
            Text { text: Math.round(scaleSlider.value * 100) + " %"; color: "#a1a1aa"; font.pixelSize: 12; Layout.preferredWidth: 45 }
        }

        // Opacity
        RowLayout {
            spacing: 8
            Text { text: "Opacity:"; color: "#d4d4d8"; font.pixelSize: 12; Layout.preferredWidth: 40 }
            Slider { id: opSlider; from: 0.0; to: 1.0; value: 0.8; Layout.fillWidth: true }
            Text { text: Math.round(opSlider.value * 100) + " %"; color: "#a1a1aa"; font.pixelSize: 12; Layout.preferredWidth: 45 }
        }

        // Blend Mode
        RowLayout {
            spacing: 8
            Text { text: "Blend:"; color: "#d4d4d8"; font.pixelSize: 12; Layout.preferredWidth: 40 }
            ComboBox {
                model: ["Over", "Multiply", "Screen", "Add"]
                currentIndex: 0
                Layout.fillWidth: true
            }
        }

        Item { Layout.fillHeight: true }

        Button {
            text: qsTr("Reset Transforms")
            Layout.fillWidth: true
        }
    }
}
