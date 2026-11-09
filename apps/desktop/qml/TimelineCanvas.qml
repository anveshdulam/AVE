import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    color: "#18181b"
    clip: true

    property real pixelsPerSecond: 100.0
    property real playheadPosition: 0.0

    // Timecode Ruler
    Rectangle {
        id: ruler
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 28
        color: "#18181b"
        border.color: "#27272a"
        border.width: 1

        Repeater {
            model: 20
            Rectangle {
                x: index * pixelsPerSecond
                y: 16
                width: 1
                height: 12
                color: "#71717a"

                Text {
                    x: 4
                    y: -14
                    text: "00:00:0" + index + ":00"
                    color: "#a1a1aa"
                    font.pixelSize: 10
                    font.family: "Consolas, monospace"
                }
            }
        }
    }

    // Tracks & Clips Canvas Area
    Flickable {
        id: flick
        anchors.top: ruler.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: 3000
        contentHeight: 3 * 68

        // Track Lanes
        Repeater {
            model: 3
            Rectangle {
                x: 0
                y: index * 68
                width: flick.contentWidth
                height: 64
                color: index % 2 === 0 ? "#1e1e24" : "#22222a"
                border.color: "#2e2e38"
                border.width: 1
            }
        }

        // Example Clips
        // Clip on V2 (Overlay PiP)
        Rectangle {
            x: 1.0 * pixelsPerSecond
            y: 0 * 68 + 4
            width: 3.0 * pixelsPerSecond
            height: 56
            radius: 4
            color: "#0284c7" // Sky-600
            border.color: "#38bdf8"
            border.width: 2

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "PiP Overlay.mp4 [3.0s]"
                color: "#ffffff"
                font.bold: true
                font.pixelSize: 12
            }
        }

        // Clip on V1 (Background Plate)
        Rectangle {
            x: 0.0 * pixelsPerSecond
            y: 1 * 68 + 4
            width: 8.0 * pixelsPerSecond
            height: 56
            radius: 4
            color: "#4f46e5" // Indigo-600
            border.color: "#818cf8"
            border.width: 2

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Background Plate.mp4 [8.0s]"
                color: "#ffffff"
                font.bold: true
                font.pixelSize: 12
            }
        }

        // Clip on A1 (Audio Track)
        Rectangle {
            x: 0.0 * pixelsPerSecond
            y: 2 * 68 + 4
            width: 8.0 * pixelsPerSecond
            height: 56
            radius: 4
            color: "#059669" // Emerald-600
            border.color: "#34d399"
            border.width: 2

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Master Audio 48kHz.wav [8.0s]"
                color: "#ffffff"
                font.bold: true
                font.pixelSize: 12
            }
        }

        // Playhead Needle
        Rectangle {
            x: playheadPosition - 1
            y: 0
            width: 2
            height: flick.contentHeight
            color: "#ef4444" // Red-500
            z: 10

            Rectangle {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: 10
                height: 12
                color: "#ef4444"
                radius: 2
            }
        }
    }
}
