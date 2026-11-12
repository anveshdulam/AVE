import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 650

    readonly property bool hasLiveTimeline: (typeof timelineModel !== "undefined" && timelineModel !== null)
    readonly property bool hasLivePlayback: (typeof playbackService !== "undefined" && playbackService !== null)
    readonly property bool hasLiveAutosave: (typeof autosaveService !== "undefined" && autosaveService !== null)

    property string pendingAction: ""
    property string pendingOpenUrl: ""
    property string pendingRecoveryOpenUrl: ""
    property bool awaitingOpenDialog: false
    property bool awaitingSaveAs: false
    property bool actionInFlight: false
    property bool closeApproved: false
    property bool handlingDialogDecision: false
    property bool handlingRecoveryDecision: false
    property bool suppressOpenReject: false
    property bool suppressSaveAsReject: false
    property string dialogErrorMessage: ""
    property string lastLifecycleErrorMessage: ""

    readonly property bool hasPendingLifecycleFlow: pendingAction !== "" ||
                                                    awaitingOpenDialog ||
                                                    awaitingSaveAs ||
                                                    actionInFlight ||
                                                    unsavedChangesDialog.visible ||
                                                    openProjectDialog.visible ||
                                                    saveProjectAsDialog.visible ||
                                                    recoveryDialog.visible

    Component.onCompleted: {
        if (root.hasLiveAutosave) {
            Qt.callLater(checkStartupRecovery)
        }
    }

    function checkStartupRecovery() {
        if (!root.hasLiveAutosave || root.hasPendingLifecycleFlow) {
            return
        }
        const records = autosaveService.discoverStartupRecovery()
        if (!records || records.length === 0) {
            return
        }
        pendingRecoveryOpenUrl = ""
        recoveryDialog.openWithRecords(records, "startup", "")
    }

    function performNormalOpen(openUrl) {
        if (!root.hasLiveTimeline || !openUrl || openUrl === "") {
            return false
        }
        if (timelineModel.openProject(openUrl)) {
            lastLifecycleErrorMessage = ""
            toolbar.showFeedback(qsTr("Project opened."), false)
            return true
        }
        const err = timelineModel.lastProjectError()
        showLifecycleError(err !== "" ? err : qsTr("Failed to open project file."))
        return false
    }

    function handleRecoveryRestore(record) {
        if (!root.hasLiveTimeline || !record) {
            return
        }
        if (String(record.status || "") !== "valid" || !record.isSafeToRestore) {
            recoveryDialog.errorMessage = qsTr("This recovery snapshot cannot be safely restored.")
            return
        }
        recoveryDialog.busy = true
        const expectedPath = root.pendingRecoveryOpenUrl !== ""
            ? root.pendingRecoveryOpenUrl
            : String(record.originalProjectPath || "")
        const ok = timelineModel.restoreRecoveredProject(String(record.recoveryFilePath || ""), expectedPath)
        recoveryDialog.busy = false
        if (ok) {
            root.handlingRecoveryDecision = true
            recoveryDialog.records = []
            recoveryDialog.close()
            root.handlingRecoveryDecision = false
            root.pendingRecoveryOpenUrl = ""
            lastLifecycleErrorMessage = ""
            toolbar.showFeedback(qsTr("Recovered project restored. Save when ready to update your project file."), false)
        } else {
            const err = timelineModel.lastProjectError()
            recoveryDialog.errorMessage = (err !== "")
                ? err
                : qsTr("Failed to restore recovery snapshot.")
            showLifecycleError(recoveryDialog.errorMessage)
        }
    }

    function handleRecoveryDiscard(record) {
        if (!root.hasLiveAutosave || !record) {
            return
        }
        recoveryDialog.busy = true
        const res = autosaveService.discardRecoveryRecord(String(record.recoveryFilePath || ""))
        recoveryDialog.busy = false
        if (!res || !res.ok) {
            recoveryDialog.errorMessage = (res && res.error && res.error !== "")
                ? res.error
                : qsTr("Could not discard the recovery file.")
            showLifecycleError(recoveryDialog.errorMessage)
            return
        }

        const mode = recoveryDialog.dialogMode
        const openTarget = root.pendingRecoveryOpenUrl !== ""
            ? root.pendingRecoveryOpenUrl
            : String(record.originalProjectPath || "")
        if (mode === "open") {
            root.handlingRecoveryDecision = true
            recoveryDialog.records = []
            recoveryDialog.close()
            root.handlingRecoveryDecision = false
            root.pendingRecoveryOpenUrl = ""
            toolbar.showFeedback(qsTr("Recovery snapshot discarded."), false)
            if (openTarget !== "") {
                root.performNormalOpen(openTarget)
            }
            return
        }

        const removedPath = String(record.recoveryFilePath || "")
        const nextList = []
        const curList = recoveryDialog.records ? recoveryDialog.records : []
        for (let i = 0; i < curList.length; ++i) {
            if (String(curList[i].recoveryFilePath || "") !== removedPath) {
                nextList.push(curList[i])
            }
        }
        if (nextList.length === 0) {
            root.handlingRecoveryDecision = true
            recoveryDialog.records = []
            recoveryDialog.close()
            root.handlingRecoveryDecision = false
            toolbar.showFeedback(qsTr("Recovery snapshot discarded."), false)
        } else {
            const nextIdx = Math.min(recoveryDialog.selectedIndex, nextList.length - 1)
            recoveryDialog.records = nextList
            recoveryDialog.selectedIndex = Math.max(0, nextIdx)
            recoveryDialog.errorMessage = ""
            recoveryDialog.focusPrimaryAction()
            toolbar.showFeedback(qsTr("Discarded selected recovery snapshot."), false)
        }
    }

    function handleRecoveryOpenOriginal(record) {
        const targetPath = (root.pendingRecoveryOpenUrl !== "")
            ? root.pendingRecoveryOpenUrl
            : (record ? String(record.originalProjectPath || "") : "")
        root.handlingRecoveryDecision = true
        recoveryDialog.close()
        root.handlingRecoveryDecision = false
        root.pendingRecoveryOpenUrl = ""
        if (targetPath !== "") {
            root.performNormalOpen(targetPath)
        } else {
            showLifecycleError(qsTr("The original project file location could not be found."))
        }
    }

    function handleRecoveryNotNow() {
        root.handlingRecoveryDecision = true
        recoveryDialog.close()
        root.handlingRecoveryDecision = false
        root.pendingRecoveryOpenUrl = ""
    }

    function resetLifecycleState() {
        pendingAction = ""
        pendingOpenUrl = ""
        awaitingOpenDialog = false
        awaitingSaveAs = false
        actionInFlight = false
        handlingDialogDecision = false
        dialogErrorMessage = ""
    }

    function showLifecycleError(message) {
        const text = (message && message !== "") ? message : qsTr("The project operation could not be completed.")
        lastLifecycleErrorMessage = text
        toolbar.showFeedback(text, true)
    }

    function completePendingOperation(action, openUrl) {
        if (!root.hasLiveTimeline) {
            return false
        }
        if (action === "new") {
            if (timelineModel.newProject()) {
                lastLifecycleErrorMessage = ""
                toolbar.showFeedback(qsTr("Created new project."), false)
                return true
            }
            const err = timelineModel.lastProjectError()
            showLifecycleError(err !== "" ? err : qsTr("Failed to create new project."))
            return false
        }
        if (action === "open") {
            if (root.hasLiveAutosave) {
                const rec = autosaveService.inspectProjectRecovery(openUrl)
                if (rec && rec.status && rec.status !== "missing") {
                    pendingRecoveryOpenUrl = openUrl
                    recoveryDialog.openWithRecords([rec], "open", openUrl)
                    return true
                }
            }
            return performNormalOpen(openUrl)
        }
        if (action === "close") {
            root.closeApproved = true
            Qt.callLater(root.close)
            return true
        }
        return false
    }

    function requestNewProject() {
        if (!root.hasLiveTimeline || root.hasPendingLifecycleFlow) {
            return
        }
        lastLifecycleErrorMessage = ""
        if (!timelineModel.isDirty) {
            completePendingOperation("new", "")
            return
        }
        pendingAction = "new"
        pendingOpenUrl = ""
        dialogErrorMessage = ""
        unsavedChangesDialog.open()
    }

    function requestOpenProject() {
        if (!root.hasLiveTimeline || root.hasPendingLifecycleFlow) {
            return
        }
        lastLifecycleErrorMessage = ""
        awaitingOpenDialog = true
        openProjectDialog.open()
    }

    function beginOpenProjectPath(urlStr) {
        if (openProjectDialog.visible) {
            suppressOpenReject = true
            openProjectDialog.close()
            suppressOpenReject = false
        }
        awaitingOpenDialog = false
        if (!root.hasLiveTimeline || !urlStr || urlStr === "") {
            return
        }
        if (pendingAction !== "" || awaitingSaveAs || actionInFlight || unsavedChangesDialog.visible || recoveryDialog.visible) {
            return
        }
        lastLifecycleErrorMessage = ""
        if (!timelineModel.isDirty) {
            completePendingOperation("open", urlStr)
            return
        }
        pendingAction = "open"
        pendingOpenUrl = urlStr
        dialogErrorMessage = ""
        unsavedChangesDialog.open()
    }

    function cancelOpenProjectDialog() {
        if (suppressOpenReject) {
            return
        }
        if (openProjectDialog.visible) {
            suppressOpenReject = true
            openProjectDialog.close()
            suppressOpenReject = false
        }
        awaitingOpenDialog = false
    }

    function requestSaveProject() {
        if (!root.hasLiveTimeline || root.hasPendingLifecycleFlow) {
            return
        }
        lastLifecycleErrorMessage = ""
        if (timelineModel.projectFilePath !== "") {
            actionInFlight = true
            const ok = timelineModel.saveProject()
            actionInFlight = false
            if (ok) {
                toolbar.showFeedback(qsTr("Project saved."), false)
            } else {
                const err = timelineModel.lastProjectError()
                showLifecycleError(err !== "" ? err : qsTr("Failed to save project."))
            }
            return
        }
        pendingAction = ""
        pendingOpenUrl = ""
        awaitingSaveAs = true
        saveProjectAsDialog.open()
    }

    function requestSaveProjectAs() {
        if (!root.hasLiveTimeline || root.hasPendingLifecycleFlow) {
            return
        }
        lastLifecycleErrorMessage = ""
        pendingAction = ""
        pendingOpenUrl = ""
        awaitingSaveAs = true
        saveProjectAsDialog.open()
    }

    function handleSaveProjectAsSelected(urlStr) {
        if (!root.hasLiveTimeline || actionInFlight) {
            return
        }
        let ok = false
        if (urlStr && urlStr !== "") {
            actionInFlight = true
            ok = timelineModel.saveProject(urlStr)
            actionInFlight = false
        } else {
            showLifecycleError(qsTr("No save location was selected for this project."))
        }

        if (saveProjectAsDialog.visible) {
            suppressSaveAsReject = true
            saveProjectAsDialog.close()
            suppressSaveAsReject = false
        }

        const action = pendingAction
        const openUrl = pendingOpenUrl
        awaitingSaveAs = false
        pendingAction = ""
        pendingOpenUrl = ""
        dialogErrorMessage = ""

        if (ok) {
            lastLifecycleErrorMessage = ""
            toolbar.showFeedback(qsTr("Project saved."), false)
            if (action !== "") {
                completePendingOperation(action, openUrl)
            }
        } else if (urlStr && urlStr !== "") {
            const err = timelineModel.lastProjectError()
            showLifecycleError(err !== "" ? err : qsTr("Failed to save project file."))
        }
    }

    function handleSaveProjectAsCancelled() {
        if (suppressSaveAsReject) {
            return
        }
        if (saveProjectAsDialog.visible) {
            suppressSaveAsReject = true
            saveProjectAsDialog.close()
            suppressSaveAsReject = false
        }
        awaitingSaveAs = false
        pendingAction = ""
        pendingOpenUrl = ""
        dialogErrorMessage = ""
        actionInFlight = false
        handlingDialogDecision = false
    }

    function triggerConfirmSave() {
        if (!root.hasLiveTimeline || actionInFlight || pendingAction === "") {
            return
        }
        if (timelineModel.projectFilePath !== "") {
            actionInFlight = true
            const ok = timelineModel.saveProject()
            actionInFlight = false
            if (ok) {
                handlingDialogDecision = true
                if (unsavedChangesDialog.visible) {
                    unsavedChangesDialog.close()
                }
                handlingDialogDecision = false
                const action = pendingAction
                const openUrl = pendingOpenUrl
                pendingAction = ""
                pendingOpenUrl = ""
                dialogErrorMessage = ""
                lastLifecycleErrorMessage = ""
                toolbar.showFeedback(qsTr("Project saved."), false)
                completePendingOperation(action, openUrl)
            } else {
                const err = timelineModel.lastProjectError()
                dialogErrorMessage = (err !== "")
                    ? err
                    : qsTr("Could not save the project file. Check folder permissions and available disk space.")
                showLifecycleError(dialogErrorMessage)
            }
            return
        }

        handlingDialogDecision = true
        if (unsavedChangesDialog.visible) {
            unsavedChangesDialog.close()
        }
        handlingDialogDecision = false
        awaitingSaveAs = true
        saveProjectAsDialog.open()
    }

    function triggerConfirmDontSave() {
        if (actionInFlight || pendingAction === "") {
            return
        }
        if (root.hasLiveTimeline && root.hasLiveAutosave) {
            const projId = timelineModel.currentProjectId()
            const projPath = timelineModel.projectFilePath
            autosaveService.discardActiveProjectChanges(projId, projPath)
        }
        handlingDialogDecision = true
        if (unsavedChangesDialog.visible) {
            unsavedChangesDialog.close()
        }
        handlingDialogDecision = false
        const action = pendingAction
        const openUrl = pendingOpenUrl
        pendingAction = ""
        pendingOpenUrl = ""
        dialogErrorMessage = ""
        lastLifecycleErrorMessage = ""
        completePendingOperation(action, openUrl)
    }

    function triggerConfirmCancel() {
        handlingDialogDecision = true
        if (unsavedChangesDialog.visible) {
            unsavedChangesDialog.close()
        }
        handlingDialogDecision = false
        resetLifecycleState()
        closeApproved = false
    }

    readonly property bool isTextEntryFocused: {
        var item = root.activeFocusItem
        if (!item) {
            return false
        }
        if (item instanceof TextInput) {
            return !item.readOnly
        }
        if (item instanceof TextEdit) {
            return !item.readOnly
        }
        return false
    }

    title: hasLiveTimeline
           ? (timelineModel.projectName + (timelineModel.isDirty ? " *" : "") + qsTr(" — Autonomous Video Editor (AVE)"))
           : qsTr("Autonomous Video Editor (AVE)")
    color: "#0f0f13"

    onClosing: (close) => {
        if (root.closeApproved) {
            root.closeApproved = false
            close.accepted = true
            return
        }
        if (!root.hasLiveTimeline || !timelineModel.isDirty) {
            close.accepted = true
            return
        }
        close.accepted = false
        if (root.hasPendingLifecycleFlow) {
            return
        }
        root.lastLifecycleErrorMessage = ""
        root.pendingAction = "close"
        root.pendingOpenUrl = ""
        root.dialogErrorMessage = ""
        unsavedChangesDialog.open()
    }

    FileDialog {
        id: openProjectDialog
        objectName: "openProjectDialog"
        title: qsTr("Open AVE Project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("AVE Projects (*.aveproj)"), qsTr("All files (*)")]
        onAccepted: root.beginOpenProjectPath(selectedFile ? selectedFile.toString() : "")
        onRejected: root.cancelOpenProjectDialog()
    }

    FileDialog {
        id: saveProjectAsDialog
        objectName: "saveProjectAsDialog"
        title: qsTr("Save AVE Project As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "aveproj"
        nameFilters: [qsTr("AVE Projects (*.aveproj)"), qsTr("All files (*)")]
        onAccepted: root.handleSaveProjectAsSelected(selectedFile ? selectedFile.toString() : "")
        onRejected: root.handleSaveProjectAsCancelled()
    }

    RecoveryDialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        onRestoreRequested: (record) => root.handleRecoveryRestore(record)
        onDiscardRequested: (record) => root.handleRecoveryDiscard(record)
        onOpenOriginalRequested: (record) => root.handleRecoveryOpenOriginal(record)
        onNotNowRequested: () => root.handleRecoveryNotNow()
        onClosed: {
            if (!root.handlingRecoveryDecision) {
                root.pendingRecoveryOpenUrl = ""
                errorMessage = ""
                busy = false
            }
        }
    }

    Dialog {
        id: unsavedChangesDialog
        objectName: "unsavedChangesDialog"
        anchors.centerIn: Overlay.overlay
        width: 440
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 0

        background: Rectangle {
            color: "#18181d"
            border.color: "#3f3f4e"
            border.width: 1
            radius: 6
        }

        onOpened: {
            saveButton.forceActiveFocus()
        }

        onClosed: {
            if (!root.handlingDialogDecision) {
                root.resetLifecycleState()
                root.closeApproved = false
            }
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 42
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
                        color: "#fbbf24"
                    }

                    Text {
                        text: qsTr("Unsaved Changes")
                        color: "#f3f4f6"
                        font.bold: true
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                }
            }

            // Body & Inline Error Banner
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 10

                Text {
                    text: qsTr("This project contains changes that haven't been saved.")
                    color: "#e5e7eb"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    text: root.hasLiveTimeline
                          ? qsTr("Project: %1").arg(timelineModel.projectName)
                          : ""
                    visible: text !== ""
                    color: "#9ca3af"
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: errorLabel.implicitHeight + 16
                    visible: root.dialogErrorMessage !== ""
                    color: "#2a1215"
                    border.color: "#f87171"
                    border.width: 1
                    radius: 4

                    Text {
                        id: errorLabel
                        objectName: "unsavedErrorText"
                        anchors.fill: parent
                        anchors.margins: 8
                        text: root.dialogErrorMessage
                        color: "#fca5a5"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                // Action Buttons
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    Button {
                        id: cancelButton
                        objectName: "unsavedCancelButton"
                        text: qsTr("Cancel")
                        Accessible.name: qsTr("Cancel")
                        enabled: !root.actionInFlight
                        Layout.preferredHeight: 30
                        Layout.minimumWidth: 82
                        KeyNavigation.left: dontSaveButton
                        KeyNavigation.right: dontSaveButton
                        KeyNavigation.tab: saveButton
                        Keys.onEscapePressed: root.triggerConfirmCancel()
                        onClicked: root.triggerConfirmCancel()
                    }

                    Button {
                        id: dontSaveButton
                        objectName: "unsavedDontSaveButton"
                        text: qsTr("Don't Save")
                        Accessible.name: qsTr("Don't Save")
                        enabled: !root.actionInFlight
                        Layout.preferredHeight: 30
                        Layout.minimumWidth: 96
                        KeyNavigation.left: saveButton
                        KeyNavigation.right: cancelButton
                        KeyNavigation.tab: cancelButton
                        Keys.onEscapePressed: root.triggerConfirmCancel()
                        onClicked: root.triggerConfirmDontSave()
                    }

                    Button {
                        id: saveButton
                        objectName: "unsavedSaveButton"
                        text: qsTr("Save")
                        Accessible.name: qsTr("Save")
                        highlighted: true
                        enabled: !root.actionInFlight
                        Layout.preferredHeight: 30
                        Layout.minimumWidth: 82
                        KeyNavigation.left: cancelButton
                        KeyNavigation.right: dontSaveButton
                        KeyNavigation.tab: dontSaveButton
                        Keys.onEscapePressed: root.triggerConfirmCancel()
                        Keys.onReturnPressed: root.triggerConfirmSave()
                        Keys.onEnterPressed: root.triggerConfirmSave()
                        onClicked: root.triggerConfirmSave()
                    }
                }
            }
        }

        Keys.onEscapePressed: root.triggerConfirmCancel()
    }

    // Standard Project Lifecycle Shortcuts
    Shortcut {
        sequence: StandardKey.New
        enabled: root.hasLiveTimeline && !root.hasPendingLifecycleFlow
        onActivated: root.requestNewProject()
    }

    Shortcut {
        sequence: StandardKey.Open
        enabled: root.hasLiveTimeline && !root.hasPendingLifecycleFlow
        onActivated: root.requestOpenProject()
    }

    Shortcut {
        sequence: StandardKey.Save
        enabled: root.hasLiveTimeline && !root.hasPendingLifecycleFlow
        onActivated: root.requestSaveProject()
    }

    Shortcut {
        sequences: [StandardKey.SaveAs, "Ctrl+Shift+S"]
        enabled: root.hasLiveTimeline && !root.hasPendingLifecycleFlow
        onActivated: root.requestSaveProjectAs()
    }

    // Global NLE Keyboard Shortcuts (disabled while typing inside editable text controls or modal lifecycle flow)
    Shortcut {
        sequence: "Space"
        enabled: root.hasLivePlayback && !root.isTextEntryFocused && !root.hasPendingLifecycleFlow
        onActivated: playbackService.togglePlay()
    }

    Shortcut {
        sequence: "Left"
        enabled: root.hasLivePlayback && !root.isTextEntryFocused && !root.hasPendingLifecycleFlow
        onActivated: playbackService.stepBackward(1)
    }

    Shortcut {
        sequence: "Right"
        enabled: root.hasLivePlayback && !root.isTextEntryFocused && !root.hasPendingLifecycleFlow
        onActivated: playbackService.stepForward(1)
    }

    Shortcut {
        sequence: "V"
        enabled: root.hasLiveTimeline && !root.isTextEntryFocused && !root.hasPendingLifecycleFlow
        onActivated: timelineModel.activeTool = 0
    }

    Shortcut {
        sequence: "C"
        enabled: root.hasLiveTimeline && !root.isTextEntryFocused && !root.hasPendingLifecycleFlow
        onActivated: timelineModel.activeTool = 1
    }

    Shortcut {
        sequences: [StandardKey.Delete, "Backspace"]
        enabled: root.hasLiveTimeline && timelineModel.selectedClipId !== "" && !root.isTextEntryFocused && !root.hasPendingLifecycleFlow
        onActivated: timelineModel.deleteSelected()
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: root.hasLiveTimeline && timelineModel.canUndo && !root.isTextEntryFocused && !root.hasPendingLifecycleFlow
        onActivated: timelineModel.undo()
    }

    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]
        enabled: root.hasLiveTimeline && timelineModel.canRedo && !root.isTextEntryFocused && !root.hasPendingLifecycleFlow
        onActivated: timelineModel.redo()
    }

    // Top Studio Navigation & Tool Toolbar
    header: ToolbarView {
        id: toolbar
        height: 60
        lifecycleBusy: root.hasPendingLifecycleFlow
        onOpenAiAssistantRequested: inspector.activeTab = 1
        onNewProjectRequested: root.requestNewProject()
        onOpenProjectRequested: root.requestOpenProject()
        onSaveProjectRequested: root.requestSaveProject()
        onSaveProjectAsRequested: root.requestSaveProjectAs()
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

                // Track Headers (Lock, Mute, Solo)
                TrackHeaderView {
                    id: trackHeader
                    SplitView.preferredWidth: 220
                    SplitView.minimumWidth: 180
                    SplitView.maximumWidth: 280
                    contentY: timelineCanvas.contentY
                    onVerticalScrollDelta: (deltaY) => timelineCanvas.scrollVerticalBy(deltaY)
                }

                // Timeline Canvas (SMPTE Ruler, Scrubber, Video/Audio Tracks with Waveforms)
                TimelineCanvas {
                    id: timelineCanvas
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 400
                }
            }
        }
    }
}
