import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    color: "#1c1c20"
    border.color: "#27272a"
    border.width: 1

    property int activeTab: 0 // 0: Properties, 1: AI Assistant

    readonly property bool hasLiveInspector: (typeof inspectorService !== "undefined" && inspectorService !== null)
    readonly property bool hasLiveAi: (typeof aiStudioService !== "undefined" && aiStudioService !== null)
    readonly property bool hasClipSelection: hasLiveInspector && inspectorService.hasSelection

    property string transformError: ""
    property string lastAiJobKind: "" // "analyze" | "proposal" | ""
    property string pendingRefName: ""
    property string activeRefName: ""
    property bool statusErrorOverride: false

    readonly property bool hasNonUniformScale: hasClipSelection
        && Math.abs(inspectorService.scaleX - inspectorService.scaleY) >= 1e-3

    readonly property bool hasValidSimilarity: hasLiveAi
        && aiStudioService.hasReference
        && aiStudioService.structuralReferenceSimilarity !== undefined
        && aiStudioService.structuralReferenceSimilarity !== null
        && isFinite(Number(aiStudioService.structuralReferenceSimilarity))

    readonly property bool statusIsError: {
        if (!hasLiveAi || aiStudioService.isBusy) {
            return false
        }
        if (statusErrorOverride) {
            return true
        }
        var lower = aiStudioService.statusMessage.toLowerCase()
        return lower.indexOf("failed") >= 0
               || lower.indexOf("rejected") >= 0
               || lower.indexOf("unable") >= 0
               || lower.indexOf("cannot") >= 0
               || lower.indexOf("no valid") >= 0
               || lower.indexOf("enter a non-empty") >= 0
               || lower.indexOf("import media") >= 0
               || lower.indexOf("error") >= 0
               || lower.indexOf("conflict") >= 0
               || lower.indexOf("non-finite") >= 0
               || lower.indexOf("no decodable") >= 0
    }

    readonly property bool statusIsSuccess: {
        if (!hasLiveAi || aiStudioService.isBusy || statusIsError) {
            return false
        }
        var msg = aiStudioService.statusMessage
        return msg.indexOf("Applied AI proposal") === 0
               || msg.indexOf("Proposal generated") === 0
               || msg.indexOf("Reference fingerprint analyzed") === 0
    }

    function applyPosX(nextValue) {
        if (!hasClipSelection || !isFinite(nextValue)) {
            return
        }
        var prev = inspectorService.posX
        inspectorService.posX = nextValue
        if (Math.abs(nextValue - prev) >= 1e-4 && Math.abs(inspectorService.posX - nextValue) >= 1e-2) {
            transformError = qsTr("Pos X update rejected.")
        } else {
            transformError = ""
        }
    }

    function applyPosY(nextValue) {
        if (!hasClipSelection || !isFinite(nextValue)) {
            return
        }
        var prev = inspectorService.posY
        inspectorService.posY = nextValue
        if (Math.abs(nextValue - prev) >= 1e-4 && Math.abs(inspectorService.posY - nextValue) >= 1e-2) {
            transformError = qsTr("Pos Y update rejected.")
        } else {
            transformError = ""
        }
    }

    function applyUniformScale(nextValue) {
        if (!hasClipSelection || !isFinite(nextValue) || nextValue <= 0.0) {
            return
        }
        var prevX = inspectorService.scaleX
        var prevY = inspectorService.scaleY
        if (Math.abs(nextValue - prevX) >= 1e-4) {
            inspectorService.scaleX = nextValue
        }
        if (Math.abs(nextValue - prevY) >= 1e-4) {
            inspectorService.scaleY = nextValue
        }
        var xRejected = Math.abs(nextValue - prevX) >= 1e-4 && Math.abs(inspectorService.scaleX - nextValue) >= 1e-3
        var yRejected = Math.abs(nextValue - prevY) >= 1e-4 && Math.abs(inspectorService.scaleY - nextValue) >= 1e-3
        if (xRejected || yRejected) {
            transformError = qsTr("Scale update rejected.")
        } else {
            transformError = ""
        }
    }

    function applyOpacity(nextValue) {
        if (!hasClipSelection || !isFinite(nextValue) || nextValue < 0.0 || nextValue > 1.0) {
            return
        }
        var prev = inspectorService.opacity
        inspectorService.opacity = nextValue
        if (Math.abs(nextValue - prev) >= 1e-4 && Math.abs(inspectorService.opacity - nextValue) >= 1e-3) {
            transformError = qsTr("Opacity update rejected.")
        } else {
            transformError = ""
        }
    }

    function cancelActiveAiJob() {
        if (!hasLiveAi) {
            return
        }
        var kind = lastAiJobKind
        lastAiJobKind = ""
        statusErrorOverride = false
        if (kind === "analyze") {
            pendingRefName = ""
            aiStudioService.clearReference()
        } else {
            aiStudioService.discardProposal()
        }
    }

    Connections {
        target: root.hasLiveInspector ? inspectorService : null
        function onSelectionChanged() {
            root.transformError = ""
            posX.userDirty = false
            posY.userDirty = false
            scaleSlider.userDirty = false
            opSlider.userDirty = false
        }
    }

    Connections {
        target: root.hasLiveAi ? aiStudioService : null
        function onHasReferenceChanged() {
            if (aiStudioService.hasReference) {
                if (root.pendingRefName !== "") {
                    root.activeRefName = root.pendingRefName
                    root.pendingRefName = ""
                }
            } else {
                root.activeRefName = ""
            }
        }
        function onStatusMessageChanged() {
            if (aiStudioService.isBusy) {
                return
            }
            var msg = aiStudioService.statusMessage
            if (msg === "Reference cleared."
                    || msg === "Proposal discarded."
                    || msg === "Ready for natural language prompt.") {
                root.statusErrorOverride = false
                root.pendingRefName = ""
                root.lastAiJobKind = ""
                return
            }
            if (root.lastAiJobKind === "analyze") {
                if (!aiStudioService.hasReference) {
                    root.pendingRefName = ""
                    root.statusErrorOverride = true
                } else {
                    root.statusErrorOverride = false
                }
                root.lastAiJobKind = ""
                return
            }
            if (root.lastAiJobKind === "proposal") {
                if (!aiStudioService.hasValidProposal && msg.indexOf("Applied AI proposal") !== 0) {
                    root.statusErrorOverride = true
                } else {
                    root.statusErrorOverride = false
                }
                root.lastAiJobKind = ""
            }
        }
        function onProposalApplied() {
            root.statusErrorOverride = false
            root.lastAiJobKind = ""
        }
    }

    FileDialog {
        id: referenceMediaDialog
        title: qsTr("Select Reference Media for Structural Analysis")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.mxf *.wav *.mp3 *.flac *.aac *.m4a *.ogg)"),
            qsTr("All files (*)")
        ]
        onAccepted: {
            if (!root.hasLiveAi || !selectedFile) {
                return
            }
            var urlStr = selectedFile.toString()
            var slash = Math.max(urlStr.lastIndexOf("/"), urlStr.lastIndexOf("\\"))
            root.pendingRefName = slash >= 0 ? urlStr.slice(slash + 1) : urlStr
            root.lastAiJobKind = "analyze"
            root.statusErrorOverride = false
            aiStudioService.analyzeReferenceMedia(urlStr)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Studio Navigation Tab Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
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
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: !root.hasLiveInspector
                              ? qsTr("Inspector Service Offline")
                              : (root.hasClipSelection
                                 ? (inspectorService.clipName !== "" ? inspectorService.clipName : qsTr("Selected Clip"))
                                 : qsTr("No Clip Selected"))
                        color: root.hasClipSelection ? "#ffffff" : "#a1a1aa"
                        font.bold: true
                        font.pixelSize: 14
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: !root.hasLiveInspector
                              ? qsTr("Launch via native ave_desktop runtime to inspect clip transforms.")
                              : (root.hasClipSelection
                                 ? qsTr("Track: %1 | Duration: %2").arg(inspectorService.trackName).arg(inspectorService.durationTimecode)
                                 : qsTr("Select a clip on the timeline to inspect and edit spatial transforms."))
                        color: "#71717a"
                        font.pixelSize: 11
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2f2f36" }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: qsTr("Spatial Transforms (Vulkan GPU)")
                            color: root.hasClipSelection ? "#38bdf8" : "#52525b"
                            font.bold: true
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }
                        HoverHandler { id: transformHeaderHover }
                        ToolTip.visible: transformHeaderHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: qsTr("Exposes InspectorService posX, posY, scaleX/scaleY (uniform), opacity, and blendMode. Rotation and anchor reset to defaults via Reset Transforms.")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        opacity: root.hasClipSelection ? 1.0 : 0.45

                        Text { text: qsTr("Pos X:"); color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 42 }
                        Slider {
                            id: posX
                            property bool userDirty: false
                            property real pendingValue: 0.0
                            enabled: root.hasClipSelection
                            from: -500.0
                            to: 500.0
                            Layout.fillWidth: true
                            Binding {
                                target: posX
                                property: "value"
                                value: root.hasClipSelection ? inspectorService.posX : 0.0
                                when: !posX.pressed && !posX.userDirty
                            }
                            onMoved: {
                                pendingValue = value
                                if (pressed) {
                                    userDirty = true
                                } else {
                                    root.applyPosX(pendingValue)
                                }
                            }
                            onPressedChanged: {
                                if (!pressed && userDirty) {
                                    var commitVal = pendingValue
                                    userDirty = false
                                    root.applyPosX(commitVal)
                                }
                            }
                        }
                        Text {
                            text: root.hasClipSelection
                                  ? (Math.round((posX.pressed || posX.userDirty) ? posX.value : inspectorService.posX) + " px")
                                  : "--"
                            color: "#a1a1aa"
                            font.pixelSize: 11
                            Layout.preferredWidth: 48
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        opacity: root.hasClipSelection ? 1.0 : 0.45

                        Text { text: qsTr("Pos Y:"); color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 42 }
                        Slider {
                            id: posY
                            property bool userDirty: false
                            property real pendingValue: 0.0
                            enabled: root.hasClipSelection
                            from: -500.0
                            to: 500.0
                            Layout.fillWidth: true
                            Binding {
                                target: posY
                                property: "value"
                                value: root.hasClipSelection ? inspectorService.posY : 0.0
                                when: !posY.pressed && !posY.userDirty
                            }
                            onMoved: {
                                pendingValue = value
                                if (pressed) {
                                    userDirty = true
                                } else {
                                    root.applyPosY(pendingValue)
                                }
                            }
                            onPressedChanged: {
                                if (!pressed && userDirty) {
                                    var commitVal = pendingValue
                                    userDirty = false
                                    root.applyPosY(commitVal)
                                }
                            }
                        }
                        Text {
                            text: root.hasClipSelection
                                  ? (Math.round((posY.pressed || posY.userDirty) ? posY.value : inspectorService.posY) + " px")
                                  : "--"
                            color: "#a1a1aa"
                            font.pixelSize: 11
                            Layout.preferredWidth: 48
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        opacity: root.hasClipSelection ? 1.0 : 0.45

                        Text { text: qsTr("Scale:"); color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 42 }
                        Slider {
                            id: scaleSlider
                            property bool userDirty: false
                            property real pendingValue: 1.0
                            enabled: root.hasClipSelection
                            from: 0.1
                            to: 2.0
                            Layout.fillWidth: true
                            Binding {
                                target: scaleSlider
                                property: "value"
                                value: root.hasClipSelection ? inspectorService.scaleX : 1.0
                                when: !scaleSlider.pressed && !scaleSlider.userDirty
                            }
                            onMoved: {
                                pendingValue = value
                                if (pressed) {
                                    userDirty = true
                                } else {
                                    root.applyUniformScale(pendingValue)
                                }
                            }
                            onPressedChanged: {
                                if (!pressed && userDirty) {
                                    var commitVal = pendingValue
                                    userDirty = false
                                    root.applyUniformScale(commitVal)
                                }
                            }
                            HoverHandler { id: scaleHover }
                            ToolTip.visible: scaleHover.hovered
                            ToolTip.delay: 300
                            ToolTip.text: root.hasNonUniformScale
                                          ? qsTr("Uniform scale slider (applies both axes). Current clip: X=%1% Y=%2%")
                                            .arg(Math.round(inspectorService.scaleX * 100))
                                            .arg(Math.round(inspectorService.scaleY * 100))
                                          : qsTr("Uniform scale (updates scaleX and scaleY)")
                        }
                        Text {
                            text: root.hasClipSelection
                                  ? (Math.round(((scaleSlider.pressed || scaleSlider.userDirty) ? scaleSlider.value : inspectorService.scaleX) * 100)
                                     + (root.hasNonUniformScale ? "%*" : " %"))
                                  : "--"
                            color: root.hasNonUniformScale ? "#fbbf24" : "#a1a1aa"
                            font.pixelSize: 11
                            Layout.preferredWidth: 48
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        opacity: root.hasClipSelection ? 1.0 : 0.45

                        Text { text: qsTr("Opacity:"); color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 42 }
                        Slider {
                            id: opSlider
                            property bool userDirty: false
                            property real pendingValue: 1.0
                            enabled: root.hasClipSelection
                            from: 0.0
                            to: 1.0
                            Layout.fillWidth: true
                            Binding {
                                target: opSlider
                                property: "value"
                                value: root.hasClipSelection ? inspectorService.opacity : 1.0
                                when: !opSlider.pressed && !opSlider.userDirty
                            }
                            onMoved: {
                                pendingValue = value
                                if (pressed) {
                                    userDirty = true
                                } else {
                                    root.applyOpacity(pendingValue)
                                }
                            }
                            onPressedChanged: {
                                if (!pressed && userDirty) {
                                    var commitVal = pendingValue
                                    userDirty = false
                                    root.applyOpacity(commitVal)
                                }
                            }
                        }
                        Text {
                            text: root.hasClipSelection
                                  ? (Math.round(((opSlider.pressed || opSlider.userDirty) ? opSlider.value : inspectorService.opacity) * 100) + " %")
                                  : "--"
                            color: "#a1a1aa"
                            font.pixelSize: 11
                            Layout.preferredWidth: 48
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        opacity: root.hasClipSelection ? 1.0 : 0.45

                        Text { text: qsTr("Blend:"); color: "#d4d4d8"; font.pixelSize: 11; Layout.preferredWidth: 42 }
                        ComboBox {
                            id: blendCombo
                            enabled: root.hasClipSelection
                            model: ["Over", "Multiply", "Screen", "Add"]
                            Layout.fillWidth: true
                            readonly property int backendIndex: {
                                if (!root.hasClipSelection) {
                                    return 0
                                }
                                var idx = model.indexOf(inspectorService.blendMode)
                                return idx >= 0 ? idx : 0
                            }
                            Binding {
                                target: blendCombo
                                property: "currentIndex"
                                value: blendCombo.backendIndex
                                when: !blendCombo.pressed && !blendCombo.popup.visible
                            }
                            onActivated: (index) => {
                                if (!root.hasClipSelection) {
                                    return
                                }
                                var mode = textAt(index)
                                inspectorService.blendMode = mode
                                if (inspectorService.blendMode !== mode) {
                                    root.transformError = qsTr("Blend mode '%1' rejected.").arg(mode)
                                } else {
                                    root.transformError = ""
                                }
                            }
                        }
                    }

                    Text {
                        visible: root.transformError !== ""
                        text: root.transformError
                        color: "#f87171"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        visible: root.hasNonUniformScale
                        text: qsTr("* Clip has non-uniform scale (X: %1%, Y: %2%); dragging Scale applies uniform X=Y.")
                            .arg(Math.round(inspectorService.scaleX * 100))
                            .arg(Math.round(inspectorService.scaleY * 100))
                        color: "#fbbf24"
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Item { Layout.fillHeight: true }

                    Button {
                        text: qsTr("Reset Transforms")
                        enabled: root.hasClipSelection
                        Layout.fillWidth: true
                        onClicked: {
                            if (!root.hasClipSelection) {
                                return
                            }
                            inspectorService.resetTransforms()
                            var ok = Math.abs(inspectorService.posX) < 1e-3
                                     && Math.abs(inspectorService.posY) < 1e-3
                                     && Math.abs(inspectorService.scaleX - 1.0) < 1e-3
                                     && Math.abs(inspectorService.scaleY - 1.0) < 1e-3
                                     && Math.abs(inspectorService.opacity - 1.0) < 1e-3
                                     && inspectorService.blendMode === "Over"
                            root.transformError = ok ? "" : qsTr("Reset transforms rejected.")
                        }
                    }
                }
            }

            // TAB 1: AI PROMPT ASSISTANT & PROPOSAL REVIEW
            Item {
                anchors.fill: parent
                visible: root.activeTab === 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 9

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            text: qsTr("NATURAL LANGUAGE EDITING")
                            color: "#a78bfa"
                            font.bold: true
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }

                        Button {
                            text: qsTr("Analyze Ref...")
                            enabled: root.hasLiveAi && !aiStudioService.isBusy
                            Layout.preferredHeight: 22
                            font.pixelSize: 10
                            ToolTip.visible: hovered
                            ToolTip.delay: 300
                            ToolTip.text: qsTr("Analyze a local video or audio reference for structural fingerprint comparison")
                            onClicked: referenceMediaDialog.open()
                        }

                        Button {
                            text: qsTr("Clear")
                            visible: root.hasLiveAi && aiStudioService.hasReference
                            enabled: root.hasLiveAi && aiStudioService.hasReference && !aiStudioService.isBusy
                            Layout.preferredHeight: 22
                            font.pixelSize: 10
                            ToolTip.visible: hovered
                            ToolTip.delay: 300
                            ToolTip.text: qsTr("Clear loaded reference fingerprint")
                            onClicked: {
                                root.statusErrorOverride = false
                                root.lastAiJobKind = ""
                                root.pendingRefName = ""
                                aiStudioService.clearReference()
                            }
                        }
                    }

                    Text {
                        visible: root.hasLiveAi && aiStudioService.hasReference
                        text: root.activeRefName !== ""
                              ? qsTr("Reference: %1").arg(root.activeRefName)
                              : qsTr("Reference fingerprint active")
                        color: "#34d399"
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    // Prompt Input Field
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 62
                        clip: true
                        color: "#131316"
                        border.color: promptInput.activeFocus ? "#8b5cf6" : "#3f3f46"
                        border.width: 1
                        radius: 4

                        TextInput {
                            id: promptInput
                            anchors.fill: parent
                            anchors.margins: 8
                            enabled: root.hasLiveAi
                            color: "#f4f4f5"
                            font.pixelSize: 12
                            wrapMode: TextInput.Wrap
                            selectByMouse: true
                            Keys.onEscapePressed: promptInput.focus = false
                        }

                        Text {
                            anchors.fill: parent
                            anchors.margins: 8
                            visible: promptInput.text === ""
                            text: qsTr("Describe pacing, target duration, or cut style (e.g. fast 15s edit on beats)...")
                            color: "#52525b"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Quick Preset Chips (populate prompt text only)
                    RowLayout {
                        spacing: 6
                        Layout.fillWidth: true

                        Button {
                            text: "⚡ Fast Beats"
                            enabled: root.hasLiveAi && !aiStudioService.isBusy
                            Layout.fillWidth: true
                            onClicked: promptInput.text = "Cut dynamic music video synced to musical beats, pacing fast"
                        }
                        Button {
                            text: "🎬 Cinematic"
                            enabled: root.hasLiveAi && !aiStudioService.isBusy
                            Layout.fillWidth: true
                            onClicked: promptInput.text = "Create smooth cinematic sequence with dissolves, slow pacing"
                        }
                    }

                    // Generate Proposal / Busy Cancel Row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Button {
                            text: qsTr("⚡ Generate AI Edit Proposal")
                            highlighted: true
                            enabled: root.hasLiveAi && !aiStudioService.isBusy && promptInput.text.trim() !== ""
                            Layout.fillWidth: true
                            onClicked: {
                                if (!root.hasLiveAi) {
                                    return
                                }
                                promptInput.focus = false
                                root.lastAiJobKind = "proposal"
                                root.statusErrorOverride = false
                                aiStudioService.generateProposal(promptInput.text)
                            }
                        }

                        Button {
                            text: qsTr("Cancel")
                            visible: root.hasLiveAi && aiStudioService.isBusy
                            Layout.preferredWidth: 68
                            onClicked: root.cancelActiveAiJob()
                        }
                    }

                    // Worker Busy Progress Row (indeterminate only, never fabricated %)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.hasLiveAi && aiStudioService.isBusy

                        BusyIndicator {
                            running: root.hasLiveAi && aiStudioService.isBusy
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                        }

                        Text {
                            text: root.lastAiJobKind === "analyze"
                                  ? qsTr("Analyzing reference media...")
                                  : qsTr("Planning & validating dry-run proposal...")
                            color: "#c4b5fd"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2f2f36" }

                    // Proposal Review Card
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#151518"
                        border.color: (root.hasLiveAi && aiStudioService.isBusy)
                                      ? "#f59e0b"
                                      : ((root.hasLiveAi && aiStudioService.hasValidProposal) ? "#8b5cf6" : "#27272a")
                        border.width: 1
                        radius: 4

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            // Header + Live Similarity / Plan State Badge
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
                                    Layout.preferredWidth: Math.max(80, badgeText.implicitWidth + 14)
                                    Layout.preferredHeight: 20
                                    radius: 10
                                    color: {
                                        if (!root.hasLiveAi) {
                                            return "#27272a"
                                        }
                                        if (aiStudioService.isBusy) {
                                            return "#312e81"
                                        }
                                        if (root.hasValidSimilarity || aiStudioService.hasValidProposal) {
                                            return "#065f46"
                                        }
                                        return "#27272a"
                                    }

                                    Text {
                                        id: badgeText
                                        anchors.centerIn: parent
                                        text: {
                                            if (!root.hasLiveAi) {
                                                return qsTr("Offline")
                                            }
                                            if (aiStudioService.isBusy) {
                                                return qsTr("Working...")
                                            }
                                            if (root.hasValidSimilarity) {
                                                return (Number(aiStudioService.structuralReferenceSimilarity) * 100.0).toFixed(1) + "% Match"
                                            }
                                            if (aiStudioService.hasReference) {
                                                return qsTr("Ref N/A")
                                            }
                                            if (aiStudioService.hasValidProposal) {
                                                return qsTr("Plan Ready")
                                            }
                                            return qsTr("No Plan")
                                        }
                                        color: {
                                            if (!root.hasLiveAi) {
                                                return "#71717a"
                                            }
                                            if (aiStudioService.isBusy) {
                                                return "#c4b5fd"
                                            }
                                            if (root.hasValidSimilarity || aiStudioService.hasValidProposal) {
                                                return "#34d399"
                                            }
                                            if (aiStudioService.hasReference) {
                                                return "#d4d4d8"
                                            }
                                            return "#71717a"
                                        }
                                        font.bold: true
                                        font.pixelSize: 10
                                    }
                                }
                            }

                            // Dry-Run Inspection Summary
                            Text {
                                visible: root.hasLiveAi && aiStudioService.hasValidProposal
                                text: qsTr("Validated %1 operation(s) via dry-run inspection (1-step undo)")
                                    .arg(aiStudioService.proposedOperations.length)
                                color: "#a78bfa"
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }

                            // Operations Diff List
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: "#0d0d0f"
                                border.color: "#27272a"
                                border.width: 1

                                ListView {
                                    id: operationsList
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    clip: true
                                    spacing: 3
                                    model: {
                                        if (!root.hasLiveAi) {
                                            return [{ text: qsTr("AI Studio service offline."), valid: false }]
                                        }
                                        if (aiStudioService.proposedOperations.length > 0) {
                                            var ops = aiStudioService.proposedOperations
                                            var validFlag = aiStudioService.hasValidProposal
                                            var items = []
                                            for (var i = 0; i < ops.length; ++i) {
                                                items.push({ text: ops[i], valid: validFlag })
                                            }
                                            return items
                                        }
                                        if (aiStudioService.isBusy) {
                                            return [{ text: qsTr("Running local worker..."), valid: false }]
                                        }
                                        return [{ text: qsTr("Enter a prompt above and click 'Generate AI Edit Proposal'."), valid: false }]
                                    }

                                    delegate: Text {
                                        required property var modelData
                                        width: ListView.view ? ListView.view.width : 0
                                        text: modelData.text
                                        color: modelData.valid ? "#10b981" : "#71717a"
                                        font.family: "Consolas, monospace"
                                        font.pixelSize: 10
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }

                            // Commit / Discard Buttons (Atomic 1-step undo guarantee via ProposalBridge::commit)
                            RowLayout {
                                spacing: 6
                                Layout.fillWidth: true
                                visible: root.hasLiveAi && aiStudioService.hasValidProposal && !aiStudioService.isBusy

                                Button {
                                    text: qsTr("✔ Apply (1-Step Undo)")
                                    highlighted: true
                                    Layout.fillWidth: true
                                    onClicked: {
                                        if (!root.hasLiveAi) {
                                            return
                                        }
                                        promptInput.focus = false
                                        if (!aiStudioService.applyProposal()) {
                                            root.statusErrorOverride = true
                                        } else {
                                            root.statusErrorOverride = false
                                            root.lastAiJobKind = ""
                                        }
                                    }
                                }

                                Button {
                                    text: qsTr("✖ Discard")
                                    Layout.preferredWidth: 74
                                    onClicked: {
                                        if (!root.hasLiveAi) {
                                            return
                                        }
                                        promptInput.focus = false
                                        root.statusErrorOverride = false
                                        root.lastAiJobKind = ""
                                        aiStudioService.discardProposal()
                                    }
                                }
                            }

                            // Real Status & Validation Error Line
                            Text {
                                text: root.hasLiveAi
                                      ? aiStudioService.statusMessage
                                      : qsTr("AI Studio service offline.")
                                color: root.statusIsError ? "#f87171" : (root.statusIsSuccess ? "#34d399" : "#a1a1aa")
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
