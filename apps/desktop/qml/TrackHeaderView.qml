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
        spacing: 0

        // Header Spacer aligned with Ruler
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#18181b"
            border.color: "#27272a"
            border.width: 1

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("TRACKS")
                color: "#71717a"
                font.pixelSize: 11
                font.bold: true
            }
        }

        // Track Lanes
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: [
                { name: "V2 (Overlay)", type: "Video", muted: false, solo: false },
                { name: "V1 (Main)",    type: "Video", muted: false, solo: false },
                { name: "A1 (Master)",  type: "Audio", muted: false, solo: false }
            ]

            delegate: Rectangle {
                width: ListView.view.width
                height: 64
                color: index % 2 === 0 ? "#202024" : "#242428"
                border.color: "#2f2f36"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        text: modelData.name
                        color: modelData.type === "Video" ? "#38bdf8" : "#34d399"
                        font.bold: true
                        font.pixelSize: 13
                        Layout.preferredWidth: 80
                    }

                    Button {
                        text: "M"
                        checkable: true
                        checked: modelData.muted
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                    }

                    Button {
                        text: "S"
                        checkable: true
                        checked: modelData.solo
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                    }

                    Button {
                        text: "🔒"
                        checkable: true
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                    }
                }
            }
        }
    }
}
