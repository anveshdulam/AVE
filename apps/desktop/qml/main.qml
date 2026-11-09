import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 650
    title: qsTr("Autonomous Video Editor (AVE) — Professional Studio")
    color: "#0f0f13"

    // Top Studio Navigation & Tool Toolbar
    header: ToolbarView {
        id: toolbar
        height: 60
    }

    // Master Workspace Vertical Splitter
    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        // UPPER ZONE: Media Pool (Left), Program Viewer (Center), Inspector/AI Studio (Right), Audio Meter (Far Right)
        SplitView {
            SplitView.fillWidth: true
            SplitView.preferredHeight: 480
            SplitView.minimumHeight: 280
            orientation: Qt.Horizontal

            // 1. Studio Media Pool & Asset Bins
            MediaPoolView {
                id: mediaPool
                SplitView.preferredWidth: 290
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: 400
            }

            // 2. Broadcast Program Monitor / Viewport
            ViewerView {
                id: programViewer
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420
            }

            // 3. Clip Properties Inspector & AI Prompt Studio
            InspectorView {
                id: inspector
                SplitView.preferredWidth: 350
                SplitView.minimumWidth: 280
                SplitView.maximumWidth: 450
            }

            // 4. Stereo Master Hardware Audio Meter
            AudioMeterView {
                id: audioMeter
                SplitView.preferredWidth: 48
                SplitView.minimumWidth: 44
                SplitView.maximumWidth: 56
            }
        }

        // LOWER ZONE: Multi-Track Timeline & Track Control Strips
        Rectangle {
            SplitView.fillWidth: true
            SplitView.preferredHeight: 340
            SplitView.minimumHeight: 180
            color: "#0f0f13"
            border.color: "#282832"
            border.width: 1

            SplitView {
                anchors.fill: parent
                orientation: Qt.Horizontal

                // Track Headers (Lock, Mute, Solo, Vol)
                TrackHeaderView {
                    SplitView.preferredWidth: 220
                    SplitView.minimumWidth: 180
                    SplitView.maximumWidth: 280
                }

                // Timeline Canvas (SMPTE Ruler, Scrubber, Video/Audio Tracks with Waveforms)
                TimelineCanvas {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 400
                    onPlayheadMoved: (posX) => {
                        // Keep timecode and viewer playhead ratio in sync
                        var ratio = posX / 1500.0
                        programViewer.playheadRatio = Math.min(1.0, Math.max(0.0, ratio))
                    }
                }
            }
        }
    }
}
