pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: recoveryRoot
    objectName: "recoveryDialog"
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, parent ? parent.width - 48 : 620)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0

    property var records: []
    property int selectedIndex: 0
    property string dialogMode: "startup" // "startup" | "open"
    property string fallbackOriginalPath: ""
    property bool busy: false
    property string errorMessage: ""

    readonly property var selectedRecord: (records && selectedIndex >= 0 && selectedIndex < records.length)
                                          ? records[selectedIndex]
                                          : null
    readonly property string selectedStatus: selectedRecord ? String(selectedRecord.status || "missing") : "missing"
    readonly property bool isSafeToRestore: selectedRecord ? Boolean(selectedRecord.isSafeToRestore) : false
    readonly property bool hasOriginalTarget: (selectedRecord && Boolean(selectedRecord.originalExists)) ||
                                              (dialogMode === "open" && fallbackOriginalPath !== "")
    readonly property bool canOpenOriginal: (dialogMode === "open" && hasOriginalTarget) ||
                                            ((selectedStatus === "stale" || selectedStatus === "corrupt") && hasOriginalTarget)

    signal restoreRequested(var record)
    signal discardRequested(var record)
    signal openOriginalRequested(var record)
    signal notNowRequested()

    function openWithRecords(recList, mode, fallbackOrigPath) {
        records = recList ? recList : []
        selectedIndex = 0
        dialogMode = mode || "startup"
        fallbackOriginalPath = fallbackOrigPath || ""
        errorMessage = ""
        busy = false
        open()
    }

    function focusPrimaryAction() {
        if (restoreButton.visible && restoreButton.enabled) {
            restoreButton.forceActiveFocus()
        } else if (openOriginalButton.visible && openOriginalButton.enabled) {
            openOriginalButton.forceActiveFocus()
        } else if (discardButton.visible && discardButton.enabled) {
            discardButton.forceActiveFocus()
        } else {
            notNowButton.forceActiveFocus()
        }
    }

    background: Rectangle {
        color: "#18181d"
        border.color: "#3f3f4e"
        border.width: 1
        radius: 6
    }

    onOpened: focusPrimaryAction()

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 46
            color: "#121216"
            radius: 6

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 6
                color: "#121216"
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: "#282832"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: recoveryRoot.selectedStatus === "corrupt"
                           ? "#f87171"
                           : (recoveryRoot.selectedStatus === "stale" ? "#fbbf24" : "#3b82f6")
                }

                Text {
                    text: recoveryRoot.dialogMode === "open"
                          ? qsTr("Project Recovery Available")
                          : qsTr("Recover Unsaved Projects")
                    color: "#f3f4f6"
                    font.bold: true
                    font.pixelSize: 13
                    Layout.fillWidth: true
                }

                Text {
                    visible: recoveryRoot.records && recoveryRoot.records.length > 1
                    text: qsTr("%1 sessions found").arg(recoveryRoot.records ? recoveryRoot.records.length : 0)
                    color: "#9ca3af"
                    font.pixelSize: 11
                }
            }
        }

        // Body
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 12

            // Multiple-session selector list
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(148, (recoveryRoot.records ? recoveryRoot.records.length : 0) * 44 + 8)
                visible: recoveryRoot.records && recoveryRoot.records.length > 1
                color: "#121216"
                border.color: "#282832"
                border.width: 1
                radius: 4

                ListView {
                    id: recordListView
                    objectName: "recoveryRecordList"
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    spacing: 4
                    model: recoveryRoot.records ? recoveryRoot.records : []

                    delegate: Rectangle {
                        id: recordDelegate
                        required property int index
                        required property var modelData

                        width: recordListView.width
                        height: 40
                        radius: 4
                        color: index === recoveryRoot.selectedIndex ? "#252938" : "#18181f"
                        border.color: index === recoveryRoot.selectedIndex ? "#3b82f6" : "#262630"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 8

                            Rectangle {
                                Layout.preferredWidth: 7
                                Layout.preferredHeight: 7
                                radius: 3.5
                                color: recordDelegate.modelData.status === "corrupt"
                                       ? "#f87171"
                                       : (recordDelegate.modelData.status === "stale" ? "#fbbf24" : "#3b82f6")
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Text {
                                    text: recordDelegate.modelData.projectName || qsTr("Untitled Project")
                                    color: "#f3f4f6"
                                    font.pixelSize: 11
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: recordDelegate.modelData.formattedTimestamp || ""
                                    color: "#9ca3af"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Text {
                                text: recordDelegate.modelData.statusLabel || ""
                                color: recordDelegate.modelData.status === "corrupt"
                                       ? "#fca5a5"
                                       : (recordDelegate.modelData.status === "stale" ? "#fcd34d" : "#93c5fd")
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                recoveryRoot.selectedIndex = recordDelegate.index
                                recoveryRoot.errorMessage = ""
                                recoveryRoot.focusPrimaryAction()
                            }
                        }
                    }
                }
            }

            // Selected Recovery Details Card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: detailsColumn.implicitHeight + 24
                visible: recoveryRoot.selectedRecord !== null
                color: "#121216"
                border.color: "#282832"
                border.width: 1
                radius: 4

                ColumnLayout {
                    id: detailsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            id: projectNameText
                            objectName: "recoveryProjectNameText"
                            text: recoveryRoot.selectedRecord ? String(recoveryRoot.selectedRecord.projectName || qsTr("Untitled Project")) : ""
                            color: "#f3f4f6"
                            font.pixelSize: 13
                            font.bold: true
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            radius: 3
                            color: recoveryRoot.selectedStatus === "corrupt"
                                   ? "#3a161a"
                                   : (recoveryRoot.selectedStatus === "stale" ? "#3a2e12" : "#152a45")
                            border.color: recoveryRoot.selectedStatus === "corrupt"
                                          ? "#f87171"
                                          : (recoveryRoot.selectedStatus === "stale" ? "#fbbf24" : "#3b82f6")
                            border.width: 1
                            implicitWidth: statusBadgeText.implicitWidth + 12
                            implicitHeight: statusBadgeText.implicitHeight + 6

                            Text {
                                id: statusBadgeText
                                objectName: "recoveryStatusText"
                                anchors.centerIn: parent
                                text: recoveryRoot.selectedRecord ? String(recoveryRoot.selectedRecord.statusLabel || "") : ""
                                color: recoveryRoot.selectedStatus === "corrupt"
                                       ? "#fca5a5"
                                       : (recoveryRoot.selectedStatus === "stale" ? "#fcd34d" : "#93c5fd")
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }

                    Text {
                        id: originalPathText
                        objectName: "recoveryOriginalPathText"
                        text: {
                            if (!recoveryRoot.selectedRecord) {
                                return ""
                            }
                            const orig = String(recoveryRoot.selectedRecord.originalProjectPath || "")
                            if (orig !== "") {
                                return qsTr("Original location: %1").arg(orig)
                            }
                            if (recoveryRoot.dialogMode === "open" && recoveryRoot.fallbackOriginalPath !== "") {
                                return qsTr("Target location: %1").arg(recoveryRoot.fallbackOriginalPath)
                            }
                            return qsTr("Original location: Unsaved session (untitled)")
                        }
                        color: "#9ca3af"
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text {
                            id: timestampText
                            objectName: "recoveryTimestampText"
                            text: recoveryRoot.selectedRecord
                                  ? qsTr("Autosaved: %1").arg(String(recoveryRoot.selectedRecord.formattedTimestamp || ""))
                                  : ""
                            color: "#9ca3af"
                            font.pixelSize: 11
                        }

                        Text {
                            id: safeStatusText
                            objectName: "recoverySafeStatusText"
                            text: recoveryRoot.isSafeToRestore
                                  ? qsTr("Safe to restore")
                                  : qsTr("Cannot auto-restore")
                            color: recoveryRoot.isSafeToRestore ? "#6ee7b7" : "#fca5a5"
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }

                    Text {
                        id: explanationText
                        objectName: "recoveryExplanationText"
                        Layout.topMargin: 4
                        Layout.fillWidth: true
                        text: recoveryRoot.selectedRecord ? String(recoveryRoot.selectedRecord.explanation || "") : ""
                        color: "#e5e7eb"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Inline Error Banner
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: recoveryErrorLabel.implicitHeight + 16
                visible: recoveryRoot.errorMessage !== ""
                color: "#2a1215"
                border.color: "#f87171"
                border.width: 1
                radius: 4

                Text {
                    id: recoveryErrorLabel
                    objectName: "recoveryErrorText"
                    anchors.fill: parent
                    anchors.margins: 8
                    text: recoveryRoot.errorMessage
                    color: "#fca5a5"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            // Action Buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                Button {
                    id: discardButton
                    objectName: "recoveryDiscardButton"
                    text: qsTr("Discard")
                    Accessible.name: qsTr("Discard Recovery Snapshot")
                    visible: recoveryRoot.selectedRecord !== null
                    enabled: !recoveryRoot.busy && recoveryRoot.selectedRecord !== null
                    Layout.preferredHeight: 30
                    Layout.minimumWidth: 84
                    onClicked: {
                        if (recoveryRoot.selectedRecord) {
                            recoveryRoot.discardRequested(recoveryRoot.selectedRecord)
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: notNowButton
                    objectName: "recoveryNotNowButton"
                    text: recoveryRoot.dialogMode === "open"
                          ? qsTr("Cancel")
                          : (recoveryRoot.selectedStatus === "valid" ? qsTr("Not Now") : qsTr("Close"))
                    Accessible.name: text
                    enabled: !recoveryRoot.busy
                    Layout.preferredHeight: 30
                    Layout.minimumWidth: 84
                    onClicked: recoveryRoot.notNowRequested()
                }

                Button {
                    id: openOriginalButton
                    objectName: "recoveryOpenOriginalButton"
                    text: qsTr("Open Original")
                    Accessible.name: qsTr("Open Original Project")
                    visible: recoveryRoot.canOpenOriginal
                    enabled: !recoveryRoot.busy && recoveryRoot.canOpenOriginal
                    highlighted: recoveryRoot.selectedStatus === "stale"
                    Layout.preferredHeight: 30
                    Layout.minimumWidth: 108
                    onClicked: {
                        if (recoveryRoot.selectedRecord) {
                            recoveryRoot.openOriginalRequested(recoveryRoot.selectedRecord)
                        }
                    }
                }

                Button {
                    id: restoreButton
                    objectName: "recoveryRestoreButton"
                    text: qsTr("Restore")
                    Accessible.name: qsTr("Restore Project")
                    visible: recoveryRoot.selectedRecord !== null &&
                             recoveryRoot.selectedStatus === "valid" &&
                             recoveryRoot.isSafeToRestore
                    enabled: !recoveryRoot.busy &&
                             recoveryRoot.selectedRecord !== null &&
                             recoveryRoot.selectedStatus === "valid" &&
                             recoveryRoot.isSafeToRestore
                    highlighted: true
                    Layout.preferredHeight: 30
                    Layout.minimumWidth: 88
                    onClicked: {
                        if (recoveryRoot.selectedRecord) {
                            recoveryRoot.restoreRequested(recoveryRoot.selectedRecord)
                        }
                    }
                }
            }
        }
    }

    Keys.onEscapePressed: {
        if (!busy) {
            notNowRequested()
        }
    }
}
