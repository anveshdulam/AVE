import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#141417"
    border.color: "#282830"
    border.width: 1

    signal assetSelected(string assetName, string duration, string resolution)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Panel Title Bar
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color: "#18181d"
            border.color: "#282830"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10

                Text {
                    text: qsTr("MEDIA POOL")
                    color: "#d1d5db"
                    font.bold: true
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "4 Assets | Master Bin"
                    color: "#6b7280"
                    font.pixelSize: 10
                }
            }
        }

        // Bins / Folders Strip
        Rectangle {
            Layout.fillWidth: true
            height: 26
            color: "#121215"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                spacing: 12

                Text {
                    text: "📁 Master"
                    color: "#93c5fd"
                    font.bold: true
                    font.pixelSize: 11
                }
                Text {
                    text: "📁 B-Roll"
                    color: "#6b7280"
                    font.pixelSize: 11
                }
                Text {
                    text: "📁 Audio & Music"
                    color: "#6b7280"
                    font.pixelSize: 11
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#23232b" }

        // Asset List
        ListView {
            id: assetList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            currentIndex: 0

            model: [
                {
                    name: "A001_C001_WideShot.mp4",
                    type: "Video",
                    duration: "00:01:45:12",
                    resolution: "3840x2160 (4K)",
                    fps: "24.00 fps",
                    codec: "ProRes 422 HQ"
                },
                {
                    name: "A001_C002_CloseUp.mp4",
                    type: "Video",
                    duration: "00:00:54:06",
                    resolution: "3840x2160 (4K)",
                    fps: "24.00 fps",
                    codec: "ProRes 422 HQ"
                },
                {
                    name: "B_Roll_Cityscape.mov",
                    type: "Video",
                    duration: "00:00:32:18",
                    resolution: "1920x1080 (HD)",
                    fps: "60.00 fps",
                    codec: "H.264 / Rec.709"
                },
                {
                    name: "Synthwave_Theme_Master.wav",
                    type: "Audio",
                    duration: "00:03:12:00",
                    resolution: "48.0 kHz 24-bit",
                    fps: "Stereo L/R",
                    codec: "PCM Linear"
                }
            ]

            delegate: Rectangle {
                width: ListView.view.width
                height: 52
                color: assetList.currentIndex === index ? "#262633" : (index % 2 === 0 ? "#16161a" : "#1a1a20")
                border.color: assetList.currentIndex === index ? "#3b82f6" : "#222228"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 10

                    // Thumbnail / Type Badge
                    Rectangle {
                        width: 48
                        height: 34
                        radius: 3
                        color: modelData.type === "Video" ? "#1e293b" : "#064e3b"
                        border.color: modelData.type === "Video" ? "#38bdf8" : "#10b981"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData.type === "Video" ? "VIDEO" : "WAV"
                            color: modelData.type === "Video" ? "#38bdf8" : "#34d399"
                            font.bold: true
                            font.pixelSize: 9
                        }
                    }

                    // Metadata Columns
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: modelData.name
                            color: "#f3f4f6"
                            font.bold: true
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            spacing: 8
                            Text {
                                text: modelData.duration
                                color: "#9ca3af"
                                font.family: "Consolas, monospace"
                                font.pixelSize: 10
                            }
                            Text {
                                text: modelData.resolution
                                color: "#6b7280"
                                font.pixelSize: 10
                            }
                            Text {
                                text: modelData.fps
                                color: "#6b7280"
                                font.pixelSize: 10
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        assetList.currentIndex = index
                        root.assetSelected(modelData.name, modelData.duration, modelData.resolution)
                    }
                }
            }
        }

        // Bottom Ingest / Import Status Strip
        Rectangle {
            Layout.fillWidth: true
            height: 24
            color: "#101013"
            border.color: "#282830"
            border.width: 1

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Drop media files here to import (FFmpeg LGPL probe active)"
                color: "#6b7280"
                font.pixelSize: 9
            }
        }
    }
}
