import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#16161a"
    border.color: "#282832"
    border.width: 1

    property int trackHeight: 64
    property real contentY: 0.0

    signal verticalScrollDelta(real deltaY)

    readonly property bool hasLiveTimeline: (typeof timelineModel !== "undefined" && timelineModel !== null)
    readonly property int effectiveTrackCount: hasLiveTimeline ? Math.max(0, timelineModel.trackCount) : 0

    onContentYChanged: syncListScroll()

    Connections {
        target: root.hasLiveTimeline ? timelineModel.tracksModel : null
        function onModelReset() { Qt.callLater(root.syncListScroll) }
        function onRowsInserted() { Qt.callLater(root.syncListScroll) }
        function onRowsRemoved() { Qt.callLater(root.syncListScroll) }
    }

    function syncListScroll() {
        var maxY = Math.max(0.0, trackListView.contentHeight - trackListView.height)
        var clamped = Math.max(0.0, Math.min(maxY, root.contentY))
        if (Math.abs(trackListView.contentY - clamped) > 0.1) {
            trackListView.contentY = clamped
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Track Header Top Bar (Aligned with Timeline Ruler)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
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
                    text: root.effectiveTrackCount === 1
                          ? qsTr("1 Track")
                          : qsTr("%1 Tracks").arg(root.effectiveTrackCount)
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
            spacing: 0
            interactive: false
            boundsBehavior: Flickable.StopAtBounds

            onContentHeightChanged: root.syncListScroll()
            onHeightChanged: root.syncListScroll()

            model: root.hasLiveTimeline ? timelineModel.tracksModel : []

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: (wheel) => {
                    var dy = wheel.pixelDelta.y !== 0 ? -wheel.pixelDelta.y : (-wheel.angleDelta.y * 0.5)
                    if (dy !== 0) {
                        wheel.accepted = true
                        root.verticalScrollDelta(dy)
                    }
                }
            }

            delegate: Rectangle {
                id: trackDelegate
                required property int index
                required property string trackId
                required property string trackName
                required property string trackType
                required property int trackIndex
                required property bool muted
                required property bool solo

                readonly property bool isVideo: trackType === "video"
                readonly property string accentColor: isVideo ? "#38bdf8" : "#10b981"

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
                        Layout.preferredWidth: 4
                        Layout.preferredHeight: 48
                        radius: 2
                        color: trackDelegate.accentColor
                        opacity: trackDelegate.muted ? 0.35 : 1.0
                    }

                    // Track Name & Type Subtitle
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: trackDelegate.trackName
                            color: trackDelegate.accentColor
                            opacity: trackDelegate.muted ? 0.55 : 1.0
                            font.bold: true
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: (trackDelegate.isVideo
                                   ? qsTr("Video Track %1").arg(trackDelegate.trackIndex + 1)
                                   : qsTr("Audio Track %1").arg(trackDelegate.trackIndex + 1))
                                  + (trackDelegate.muted ? qsTr(" • Muted") : "")
                            color: trackDelegate.muted ? "#f87171" : "#9ca3af"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        HoverHandler {
                            id: infoHover
                        }
                        ToolTip.visible: infoHover.hovered
                        ToolTip.delay: 450
                        ToolTip.text: trackDelegate.trackName
                                      + " (" + trackDelegate.trackType + ")"
                                      + (trackDelegate.muted ? qsTr(" [Muted]") : "")
                    }

                    // Track Action Buttons
                    RowLayout {
                        spacing: 4

                        // Lock Button (Deferred post-Phase 10)
                        Button {
                            text: "🔒"
                            enabled: false
                            checkable: false
                            checked: false
                            opacity: 0.45
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            HoverHandler {
                                id: lockHover
                            }
                            ToolTip.visible: lockHover.hovered
                            ToolTip.delay: 300
                            ToolTip.text: qsTr("Track lock — Deferred post-Phase 10")
                        }

                        // Hide / Mute Button (Live via TimelineModel::toggleTrackMute)
                        Button {
                            text: trackDelegate.isVideo ? "👁" : "M"
                            enabled: root.hasLiveTimeline
                            checkable: false
                            checked: trackDelegate.muted
                            highlighted: trackDelegate.muted
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            HoverHandler {
                                id: muteHover
                            }
                            ToolTip.visible: muteHover.hovered
                            ToolTip.delay: 300
                            ToolTip.text: trackDelegate.isVideo
                                          ? (trackDelegate.muted ? qsTr("Show Video Track") : qsTr("Hide Video Track"))
                                          : (trackDelegate.muted ? qsTr("Unmute Audio Track") : qsTr("Mute Audio Track"))
                            onClicked: {
                                if (root.hasLiveTimeline) {
                                    timelineModel.toggleTrackMute(trackDelegate.trackId)
                                }
                            }
                        }

                        // Solo Button (Audio only — Deferred post-Phase 10)
                        Button {
                            visible: !trackDelegate.isVideo
                            text: "S"
                            enabled: false
                            checkable: false
                            checked: trackDelegate.solo
                            opacity: 0.45
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            HoverHandler {
                                id: soloHover
                            }
                            ToolTip.visible: soloHover.hovered
                            ToolTip.delay: 300
                            ToolTip.text: qsTr("Track solo — Deferred post-Phase 10")
                        }
                    }
                }
            }
        }
    }
}
