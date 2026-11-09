import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#1c1c20"
    border.color: "#27272a"
    border.width: 1

    property int activeTab: 0 // 0: Properties, 1: AI Assistant
    property bool proposalActive: false
    property string proposalStatus: "Ready for natural language prompt."

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Studio Navigation Tab Header
        Rectangle {
            Layout.fillWidth: true
            height: 38
            color: "#18181b"
            border.color: "#27272a"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.activeTab === 0 ? "#242429" : "transparent"
                    border.color: root.activeTab === 0 ? "#3b82f6" : "transparent"
                    border.width: root.activeTab === 0 ? 1 : 0

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("PROPERTIES")
                        color: root.activeTab === 0 ? "#ffffff" : "#a1a1aa"
                        font.bold: true
                        font.pixelSize: 11
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = 0
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.activeTab === 1 ? "#242429" : "transparent"
                    border.color: root.activeTab === 1 ? "#8b5cf6" : "transparent"
                    border.width: root.activeTab === 1 ? 1 : 0

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            text: "⚡"
                            font.pixelSize: 11
                        }
                        Text {
                            text: qsTr("AI ASSISTANT")
                            color: root.activeTab === 1 ? "#a78bfa" : "#a1a1aa"
                            font.bold: true
                            font.pixelSize: 11
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = 1
                    }
                }
            }
        }

        // Tab Content Area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // TAB 0: CLIP PROPERTIES & SPATIAL TRANSFORMS
            Item {
                anchors.fill: parent
                visible: root.activeTab === 0

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: "PiP Overlay.mp4"
                        color: "#ffffff"
                        font.bold: true
                        font.pixelSize: 14
                    }

                    Text {
                        text: "Track: V2 | Duration: 00:00:03:00"
                        color: "#71717a"
                        font.pixelSize: 11
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2f2f36" }

                    Text {
                        text: qsTr("Spatial Transforms (Vulkan GPU)")
                        color: "#38bdf8"
                        font.bold: true
                        font.pixelSize: 11
                    }

                    RowLayout {
                        spacing: 8
                        Text { text: "Pos X:"; color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 40 }
                        Slider { id: posX; from: -500; to: 500; value: 40; Layout.fillWidth: true }
                        Text { text: Math.round(posX.value) + " px"; color: "#a1a1aa"; font.pixelSize: 11; Layout.preferredWidth: 45 }
                    }

                    RowLayout {
                        spacing: 8
                        Text { text: "Pos Y:"; color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 40 }
                        Slider { id: posY; from: -500; to: 500; value: 30; Layout.fillWidth: true }
                        Text { text: Math.round(posY.value) + " px"; color: "#a1a1aa"; font.pixelSize: 11; Layout.preferredWidth: 45 }
                    }

                    RowLayout {
                        spacing: 8
                        Text { text: "Scale:"; color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 40 }
                        Slider { id: scaleSlider; from: 0.1; to: 2.0; value: 0.5; Layout.fillWidth: true }
                        Text { text: Math.round(scaleSlider.value * 100) + " %"; color: "#a1a1aa"; font.pixelSize: 11; Layout.preferredWidth: 45 }
                    }

                    RowLayout {
                        spacing: 8
                        Text { text: "Opacity:"; color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 40 }
                        Slider { id: opSlider; from: 0.0; to: 1.0; value: 0.8; Layout.fillWidth: true }
                        Text { text: Math.round(opSlider.value * 100) + " %"; color: "#a1a1aa"; font.pixelSize: 11; Layout.preferredWidth: 45 }
                    }

                    RowLayout {
                        spacing: 8
                        Text { text: "Blend:"; color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 40 }
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

            // TAB 1: AI PROMPT ASSISTANT & PROPOSAL REVIEW (Phase 8)
            Item {
                anchors.fill: parent
                visible: root.activeTab === 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: qsTr("NATURAL LANGUAGE EDITING")
                        color: "#a78bfa"
                        font.bold: true
                        font.pixelSize: 11
                    }

                    // Prompt Input Field
                    Rectangle {
                        Layout.fillWidth: true
                        height: 60
                        color: "#131316"
                        border.color: "#3f3f46"
                        border.width: 1
                        radius: 4

                        TextInput {
                            id: promptInput
                            anchors.fill: parent
                            anchors.margins: 8
                            color: "#f4f4f5"
                            font.pixelSize: 12
                            wrapMode: TextInput.Wrap
                            text: "Cut high energy music video on 120 BPM drum beats, target 15 seconds"
                            selectByMouse: true
                        }
                    }

                    // Quick Preset Chips
                    RowLayout {
                        spacing: 6
                        Layout.fillWidth: true

                        Button {
                            text: "⚡ Fast Beats"
                            Layout.fillWidth: true
                            onClicked: promptInput.text = "Cut dynamic music video synced to musical beats, pacing fast"
                        }
                        Button {
                            text: "🎬 Cinematic"
                            Layout.fillWidth: true
                            onClicked: promptInput.text = "Create smooth cinematic sequence with dissolves, slow pacing"
                        }
                    }

                    // Generate Proposal Action Button
                    Button {
                        text: qsTr("⚡ Generate AI Edit Proposal")
                        highlighted: true
                        Layout.fillWidth: true
                        onClicked: {
                            root.proposalActive = true
                            root.proposalStatus = "Proposal generated via Deterministic Planner (ADR-0018)."
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2f2f36" }

                    // Proposal Review Card
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#151518"
                        border.color: root.proposalActive ? "#8b5cf6" : "#27272a"
                        border.width: 1
                        radius: 4

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            // Header + Fidelity Badge
                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: qsTr("PROPOSAL REVIEW")
                                    color: "#e4e4e7"
                                    font.bold: true
                                    font.pixelSize: 11
                                }

                                Item { Layout.fillWidth: true }

                                Rectangle {
                                    width: 80
                                    height: 20
                                    radius: 10
                                    color: root.proposalActive ? "#065f46" : "#27272a"

                                    Text {
                                        anchors.centerIn: parent
                                        text: root.proposalActive ? "95.2% Match" : "No Plan"
                                        color: root.proposalActive ? "#34d399" : "#71717a"
                                        font.bold: true
                                        font.pixelSize: 10
                                    }
                                }
                            }

                            // Operations Diff List
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: "#0d0d0f"
                                border.color: "#27272a"
                                border.width: 1

                                ListView {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    clip: true
                                    model: root.proposalActive ? [
                                        "+ V1: SceneA_01.mp4 [00:00:00 -> 00:01:12]",
                                        "+ V1: SceneB_04.mp4 [00:01:12 -> 00:03:00]",
                                        "+ V1: SceneC_02.mp4 [00:03:00 -> 00:04:18]",
                                        "+ V1: SceneD_05.mp4 [00:04:18 -> 00:06:00]"
                                    ] : ["Type prompt above and click 'Generate AI Edit Proposal'"]

                                    delegate: Text {
                                        text: modelData
                                        color: root.proposalActive ? "#10b981" : "#71717a"
                                        font.family: "Consolas, monospace"
                                        font.pixelSize: 10
                                        height: 18
                                    }
                                }
                            }

                            // Commit / Discard Buttons (Atomic 1-step undo guarantee)
                            RowLayout {
                                spacing: 6
                                Layout.fillWidth: true
                                visible: root.proposalActive

                                Button {
                                    text: qsTr("✔ Apply (1-Step Undo)")
                                    highlighted: true
                                    Layout.fillWidth: true
                                    onClicked: {
                                        root.proposalStatus = "Committed 4 clips via CompositeCommand. Undo available (Ctrl+Z)."
                                    }
                                }

                                Button {
                                    text: qsTr("✖ Discard")
                                    Layout.preferredWidth: 70
                                    onClicked: {
                                        root.proposalActive = false
                                        root.proposalStatus = "Proposal rejected. Timeline untouched."
                                    }
                                }
                            }

                            // Status Line
                            Text {
                                text: root.proposalStatus
                                color: "#a1a1aa"
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
