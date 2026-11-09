import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#0f0f13"
    clip: true

    property real pixelsPerSecond: 100.0
    property real playheadX: 180.0
    property int trackHeight: 64
    property bool snappingEnabled: true

    signal playheadMoved(real positionX)

    // Timecode Ruler
    Rectangle {
        id: ruler
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 28
        color: "#121215"
        border.color: "#282832"
        border.width: 1

        Canvas {
            id: rulerCanvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = "#4b5563"
                ctx.fillStyle = "#9ca3af"
                ctx.font = "9px Consolas, monospace"

                var totalSeconds = Math.ceil(width / root.pixelsPerSecond) + 5
                for (var s = 0; s < totalSeconds; s++) {
                    var x = s * root.pixelsPerSecond - flick.contentX

                    // Major second tick
                    ctx.beginPath()
                    ctx.moveTo(x, 14)
                    ctx.lineTo(x, 28)
                    ctx.stroke()

                    // Timecode Label
                    var secStr = s < 10 ? "0" + s : "" + s
                    ctx.fillText("01:00:" + secStr + ":00", x + 3, 12)

                    // Sub-frame minor ticks (4 per second)
                    for (var sub = 1; sub < 4; sub++) {
                        var subX = x + sub * (root.pixelsPerSecond / 4.0)
                        ctx.beginPath()
                        ctx.moveTo(subX, 20)
                        ctx.lineTo(subX, 28)
                        ctx.stroke()
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                root.playheadX = mouse.x + flick.contentX
                root.playheadMoved(root.playheadX)
            }
            onPositionChanged: (mouse) => {
                if (pressed) {
                    root.playheadX = Math.max(0, mouse.x + flick.contentX)
                    root.playheadMoved(root.playheadX)
                }
            }
        }
    }

    // Tracks & Clips Canvas Scroll Area
    Flickable {
        id: flick
        anchors.top: ruler.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: 3600
        contentHeight: 4 * root.trackHeight
        boundsBehavior: Flickable.StopAtBounds

        onContentXChanged: rulerCanvas.requestPaint()

        // Track Lanes Backgrounds
        Repeater {
            model: 4
            Rectangle {
                x: 0
                y: index * root.trackHeight
                width: flick.contentWidth
                height: root.trackHeight
                color: index % 2 === 0 ? "#141418" : "#17171d"
                border.color: "#22222a"
                border.width: 1

                // Subtle grid guidelines every second
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = "#1b1b22"
                        for (var gx = 0; gx < width; gx += root.pixelsPerSecond) {
                            ctx.beginPath()
                            ctx.moveTo(gx, 0)
                            ctx.lineTo(gx, height)
                            ctx.stroke()
                        }
                    }
                }
            }
        }

        // ==========================================
        // TRACK V2: Overlay Picture-in-Picture Clip
        // ==========================================
        Rectangle {
            x: 1.5 * root.pixelsPerSecond
            y: 0 * root.trackHeight + 3
            width: 3.5 * root.pixelsPerSecond
            height: root.trackHeight - 6
            radius: 4
            color: "#1e3a5f" // Deep studio navy
            border.color: "#38bdf8"
            border.width: 1

            // Clip Title Header Strip
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 18
                color: "#2563eb"
                radius: 3

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6

                    Text {
                        text: "A001_C002_CloseUp.mp4 [PiP 50%]"
                        color: "#ffffff"
                        font.bold: true
                        font.pixelSize: 10
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "03.5s"
                        color: "#bfdbfe"
                        font.pixelSize: 9
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: "Op: 80% | Scale: 50%"
                color: "#7dd3fc"
                font.pixelSize: 10
            }
        }

        // ==========================================
        // TRACK V1: Main Storyboard Sequence (3 Clips)
        // ==========================================
        // Clip 1
        Rectangle {
            x: 0.0 * root.pixelsPerSecond
            y: 1 * root.trackHeight + 3
            width: 4.2 * root.pixelsPerSecond
            height: root.trackHeight - 6
            radius: 4
            color: "#1e293b" // Studio Slate
            border.color: "#60a5fa"
            border.width: 1

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 18
                color: "#1d4ed8"
                radius: 3

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "A001_C001_WideShot.mp4"
                    color: "#ffffff"
                    font.bold: true
                    font.pixelSize: 10
                }
            }
        }

        // Clip 2
        Rectangle {
            x: 4.2 * root.pixelsPerSecond
            y: 1 * root.trackHeight + 3
            width: 3.8 * root.pixelsPerSecond
            height: root.trackHeight - 6
            radius: 4
            color: "#1e293b"
            border.color: "#60a5fa"
            border.width: 1

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 18
                color: "#1d4ed8"
                radius: 3

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "B_Roll_Cityscape.mov"
                    color: "#ffffff"
                    font.bold: true
                    font.pixelSize: 10
                }
            }
        }

        // Clip 3
        Rectangle {
            x: 8.0 * root.pixelsPerSecond
            y: 1 * root.trackHeight + 3
            width: 5.0 * root.pixelsPerSecond
            height: root.trackHeight - 6
            radius: 4
            color: "#1e293b"
            border.color: "#60a5fa"
            border.width: 1

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 18
                color: "#1d4ed8"
                radius: 3

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "A001_C003_NightPlate.mp4"
                    color: "#ffffff"
                    font.bold: true
                    font.pixelSize: 10
                }
            }
        }

        // ==========================================
        // TRACK A1: Dialogue / SFX with Waveform
        // ==========================================
        Rectangle {
            x: 0.0 * root.pixelsPerSecond
            y: 2 * root.trackHeight + 3
            width: 8.0 * root.pixelsPerSecond
            height: root.trackHeight - 6
            radius: 4
            color: "#064e3b" // Deep Emerald
            border.color: "#10b981"
            border.width: 1

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 16
                color: "#047857"
                radius: 3

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Dialogue_Clean_Track.wav [48kHz]"
                    color: "#ffffff"
                    font.bold: true
                    font.pixelSize: 9
                }
            }

            // Real Canvas Audio Waveform Rendering
            Canvas {
                anchors.fill: parent
                anchors.topMargin: 16
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = "#34d399"
                    ctx.lineWidth = 1

                    var midY = height / 2.0
                    for (var wx = 2; wx < width - 2; wx += 2) {
                        // Simulated speech formant envelopes
                        var amp = Math.sin(wx * 0.05) * Math.cos(wx * 0.02) * (midY - 4)
                        amp = Math.abs(amp) * (0.4 + 0.6 * Math.sin(wx * 0.3))
                        ctx.beginPath()
                        ctx.moveTo(wx, midY - amp)
                        ctx.lineTo(wx, midY + amp)
                        ctx.stroke()
                    }
                }
            }
        }

        // ==========================================
        // TRACK A2: Music Score Track with Waveform
        // ==========================================
        Rectangle {
            x: 0.0 * root.pixelsPerSecond
            y: 3 * root.trackHeight + 3
            width: 14.0 * root.pixelsPerSecond
            height: root.trackHeight - 6
            radius: 4
            color: "#134e4a" // Deep Teal
            border.color: "#14b8a6"
            border.width: 1

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 16
                color: "#0f766e"
                radius: 3

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Synthwave_Theme_Master.wav [120 BPM - Beat-Grid]"
                    color: "#ffffff"
                    font.bold: true
                    font.pixelSize: 9
                }
            }

            // High-Density Music Beat Waveform Rendering
            Canvas {
                anchors.fill: parent
                anchors.topMargin: 16
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = "#2dd4bf"
                    ctx.lineWidth = 1

                    var midY = height / 2.0
                    for (var mx = 2; mx < width - 2; mx += 2) {
                        // Rhythmic 120 BPM beat peaks
                        var beatSync = (mx % 50 === 0) ? (midY - 2) : (Math.random() * (midY - 8))
                        ctx.beginPath()
                        ctx.moveTo(mx, midY - beatSync)
                        ctx.lineTo(mx, midY + beatSync)
                        ctx.stroke()
                    }
                }
            }
        }

        // ==========================================
        // SMPTE SCRUBBING PLAYHEAD
        // ==========================================
        Item {
            x: root.playheadX - 6
            y: 0
            width: 13
            height: 4 * root.trackHeight
            z: 100

            // Playhead Triangular Head
            Canvas {
                width: 13
                height: 12
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = "#ef4444" // Studio Red
                    ctx.beginPath()
                    ctx.moveTo(0, 0)
                    ctx.lineTo(12, 0)
                    ctx.lineTo(6, 12)
                    ctx.closePath()
                    ctx.fill()
                }
            }

            // Playhead Vertical Needle Line
            Rectangle {
                width: 1.5
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#ef4444"
            }
        }
    }
}
