import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#121215"
    border.color: "#282830"
    border.width: 1

    readonly property bool hasLiveService: (typeof playbackService !== "undefined" && playbackService !== null)
    readonly property real leftLevelDb: hasLiveService ? playbackService.leftLevelDb : -60.0
    readonly property real rightLevelDb: hasLiveService ? playbackService.rightLevelDb : -60.0
    readonly property real masterLevelDb: Math.max(leftLevelDb, rightLevelDb)

    HoverHandler {
        id: meterHover
    }

    ToolTip.visible: meterHover.hovered
    ToolTip.delay: 300
    ToolTip.text: root.hasLiveService
                  ? playbackService.audioOutputStatus
                  : qsTr("No live audio service connected")

    function dbToHeightRatio(db) {
        // Range: -60 dB (0.0) to 0 dB (1.0)
        if (!isFinite(db) || db <= -60.0) return 0.0
        if (db >= 0.0) return 1.0
        return (db + 60.0) / 60.0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        // Top Readout
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.masterLevelDb.toFixed(1) + " dB"
            color: root.masterLevelDb > -3.0 ? "#ef4444" : "#34d399"
            font.family: "Consolas, monospace"
            font.bold: true
            font.pixelSize: 9
        }

        // Meter Bars & Scale
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 3

            // Left Channel
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 8
                color: "#18181f"
                border.color: "#2a2a35"
                border.width: 1

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: parent.height * root.dbToHeightRatio(root.leftLevelDb)
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: root.leftLevelDb > -3.0 ? "#ef4444" : (root.leftLevelDb > -18.0 ? "#facc15" : "#10b981") }
                        GradientStop { position: 0.7; color: "#10b981" }
                        GradientStop { position: 1.0; color: "#047857" }
                    }
                }
            }

            // Right Channel
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 8
                color: "#18181f"
                border.color: "#2a2a35"
                border.width: 1

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: parent.height * root.dbToHeightRatio(root.rightLevelDb)
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: root.rightLevelDb > -3.0 ? "#ef4444" : (root.rightLevelDb > -18.0 ? "#facc15" : "#10b981") }
                        GradientStop { position: 0.7; color: "#10b981" }
                        GradientStop { position: 1.0; color: "#047857" }
                    }
                }
            }

            // dB Markings Column
            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                spacing: 0

                Repeater {
                    model: ["0", "-6", "-12", "-18", "-24", "-36", "-48", "-60"]
                    Text {
                        text: modelData
                        color: index === 0 ? "#ef4444" : (index <= 3 ? "#facc15" : "#6b7280")
                        font.family: "Consolas, monospace"
                        font.pixelSize: 8
                        Layout.alignment: Qt.AlignLeft
                    }
                }
            }
        }

        // Bottom Channel Labels
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "L   R"
            color: "#9ca3af"
            font.bold: true
            font.pixelSize: 9
        }
    }
}

