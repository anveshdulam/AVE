import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#0f0f13"
    clip: true

    property int trackHeight: 64
    property int tracksRevision: 0

    readonly property bool hasLiveTimeline: (typeof timelineModel !== "undefined" && timelineModel !== null)
    readonly property bool hasLivePlayback: (typeof playbackService !== "undefined" && playbackService !== null)

    readonly property int effectiveTrackCount: hasLiveTimeline ? Math.max(0, timelineModel.trackCount) : 0
    readonly property double effectiveDurationSec: {
        var tlDur = hasLiveTimeline ? Math.max(0.0, timelineModel.durationSeconds) : 0.0
        var pbDur = hasLivePlayback ? Math.max(0.0, playbackService.durationSeconds) : 0.0
        return Math.max(tlDur, pbDur)
    }
    readonly property real effectiveZoomFactor: hasLiveTimeline
                                                ? Math.max(0.1, Math.min(20.0, timelineModel.zoomFactor))
                                                : 1.0
    readonly property real effectivePixelsPerSecond: 100.0 * effectiveZoomFactor
    readonly property double effectivePlayheadSec: (hasLivePlayback && isFinite(playbackService.playheadPosition))
                                                   ? Math.max(0.0, playbackService.playheadPosition)
                                                   : 0.0
    readonly property real effectivePlayheadX: effectivePlayheadSec * effectivePixelsPerSecond
    readonly property double effectiveFps: (hasLivePlayback && isFinite(playbackService.fps) && playbackService.fps > 0.0)
                                           ? playbackService.fps
                                           : 24.0
    readonly property int effectiveActiveTool: hasLiveTimeline ? timelineModel.activeTool : 0
    readonly property bool snappingEnabled: hasLiveTimeline ? timelineModel.snappingEnabled : false
    readonly property string effectiveSelectedClipId: hasLiveTimeline ? timelineModel.selectedClipId : ""

    // Exposed for vertical scroll synchronization with TrackHeaderView
    property alias contentY: flick.contentY

    signal playheadMoved(real positionX)

    onEffectivePixelsPerSecondChanged: {
        rulerCanvas.requestPaint()
        gridCanvas.requestPaint()
    }
    onEffectiveFpsChanged: rulerCanvas.requestPaint()
    onEffectiveTrackCountChanged: gridCanvas.requestPaint()

    Connections {
        target: root.hasLiveTimeline ? timelineModel.tracksModel : null
        function onModelReset() { root.tracksRevision++ }
        function onRowsInserted() { root.tracksRevision++ }
        function onRowsRemoved() { root.tracksRevision++ }
        function onDataChanged() { root.tracksRevision++ }
    }

    function pad2(val) {
        var n = Math.max(0, Math.floor(val))
        return n < 10 ? ("0" + n) : ("" + n)
    }

    // Matches ave::Timecode::from_rational Non-Drop-Frame HH:MM:SS:FF
    function formatNdfTimecode(seconds, fps) {
        var sec = Math.max(0.0, isFinite(seconds) ? seconds : 0.0)
        var safeFps = (isFinite(fps) && fps > 0.0) ? fps : 24.0
        var ceilFps = Math.max(1, Math.ceil(safeFps - 1e-6))
        var totalFrames = Math.floor(sec * safeFps + 1e-6)
        var ff = totalFrames % ceilFps
        var totalWholeSec = Math.floor(totalFrames / ceilFps)
        var ss = totalWholeSec % 60
        var totalMin = Math.floor(totalWholeSec / 60)
        var mm = totalMin % 60
        var hh = Math.floor(totalMin / 60)
        return pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss) + ":" + pad2(ff)
    }

    // Returns { stepSec, isFrameStep, minorCount } keeping major labels >= 92 px apart
    function computeRulerSpacing(pps, fps) {
        var safePps = Math.max(1.0, pps)
        var safeFps = (isFinite(fps) && fps > 0.0) ? fps : 24.0
        var ceilFps = Math.max(1, Math.ceil(safeFps - 1e-6))
        var minMajorPx = 92.0

        var frameSteps = [1, 2, 5, 10, 12, 15, 24, 30]
        for (var fi = 0; fi < frameSteps.length; ++fi) {
            var fc = frameSteps[fi]
            if (fc >= ceilFps) break
            var fSec = fc / safeFps
            if (fSec * safePps >= minMajorPx) {
                return { stepSec: fSec, minorCount: Math.max(1, Math.min(fc, 5)) }
            }
        }

        var secSteps = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 1800, 3600]
        for (var si = 0; si < secSteps.length; ++si) {
            var sc = secSteps[si]
            if (sc * safePps >= minMajorPx) {
                var minors = (sc === 5 || sc === 10 || sc === 15) ? 5 : 4
                return { stepSec: sc, minorCount: minors }
            }
        }

        return { stepSec: 3600, minorCount: 4 }
    }

    function trailingPaddingPixels(pps) {
        return Math.max(200.0, 10.0 * Math.min(Math.max(10.0, pps), 200.0))
    }

    function requiredContentWidth(pps) {
        var maxSec = Math.max(root.effectiveDurationSec, root.effectivePlayheadSec, 0.0)
        return Math.max(flick.width, maxSec * pps + trailingPaddingPixels(pps))
    }

    function laneInfoAtRow(row) {
        var rev = root.tracksRevision
        if (rev < 0 || row < 0 || row >= trackLaneRepeater.count) {
            return null
        }
        return trackLaneRepeater.itemAt(row)
    }

    function trackIdAtRow(row) {
        var lane = laneInfoAtRow(row)
        return lane ? lane.laneTrackId : ""
    }

    function isAudioTrackRow(row) {
        var lane = laneInfoAtRow(row)
        return lane ? (lane.laneTrackType === "audio") : false
    }

    function isTrackMutedRow(row) {
        var lane = laneInfoAtRow(row)
        return lane ? lane.laneMuted : false
    }

    function scrollVerticalBy(deltaY) {
        var maxY = Math.max(0.0, flick.contentHeight - flick.height)
        flick.contentY = Math.max(0.0, Math.min(maxY, flick.contentY + deltaY))
    }

    function clampScroll() {
        var maxX = Math.max(0.0, flick.contentWidth - flick.width)
        var maxY = Math.max(0.0, flick.contentHeight - flick.height)
        if (flick.contentX > maxX) flick.contentX = maxX
        if (flick.contentX < 0.0) flick.contentX = 0.0
        if (flick.contentY > maxY) flick.contentY = maxY
        if (flick.contentY < 0.0) flick.contentY = 0.0
    }

    function seekFromCanvasX(canvasX) {
        var clampedX = Math.max(0.0, isFinite(canvasX) ? canvasX : 0.0)
        var pps = Math.max(1.0, root.effectivePixelsPerSecond)
        var seconds = Math.max(0.0, clampedX / pps)
        if (root.hasLivePlayback) {
            playbackService.seekSeconds(seconds)
        }
        root.playheadMoved(seconds * pps)
        return seconds
    }

    function zoomAtViewportX(viewX, multiplier) {
        if (!root.hasLiveTimeline || !isFinite(multiplier) || multiplier <= 0.0) {
            return
        }
        var oldZoom = root.effectiveZoomFactor
        var newZoom = Math.max(0.1, Math.min(20.0, oldZoom * multiplier))
        if (Math.abs(newZoom - oldZoom) < 1e-6) {
            return
        }

        var oldPps = 100.0 * oldZoom
        var anchorX = Math.max(0.0, Math.min(flick.width, viewX))
        var pivotSec = Math.max(0.0, (flick.contentX + anchorX) / oldPps)

        timelineModel.zoomFactor = newZoom

        var appliedPps = root.effectivePixelsPerSecond
        var projectedWidth = requiredContentWidth(appliedPps)
        var maxX = Math.max(0.0, projectedWidth - flick.width)
        flick.contentX = Math.max(0.0, Math.min(maxX, pivotSec * appliedPps - anchorX))
    }

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
            Component.onCompleted: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (width <= 0 || height <= 0) return

                var pps = Math.max(1.0, root.effectivePixelsPerSecond)
                var spacing = root.computeRulerSpacing(pps, root.effectiveFps)
                var stepSec = spacing.stepSec
                var stepPx = stepSec * pps

                var scrollX = flick.contentX
                var startSec = Math.max(0.0, (scrollX - 96.0) / pps)
                var endSec = Math.max(0.0, (scrollX + width + 96.0) / pps)

                var firstTick = Math.max(0, Math.floor(startSec / stepSec))
                var lastTick = Math.min(firstTick + 512, Math.ceil(endSec / stepSec))

                ctx.strokeStyle = "#4b5563"
                ctx.fillStyle = "#9ca3af"
                ctx.font = "9px Consolas, monospace"
                ctx.lineWidth = 1

                for (var tick = firstTick; tick <= lastTick; ++tick) {
                    var tickSec = tick * stepSec
                    var x = tickSec * pps - scrollX

                    if (x >= -100 && x <= width + 100) {
                        ctx.beginPath()
                        ctx.moveTo(x, 14)
                        ctx.lineTo(x, 28)
                        ctx.stroke()

                        ctx.fillText(root.formatNdfTimecode(tickSec, root.effectiveFps), x + 3, 12)
                    }

                    var minorCount = spacing.minorCount
                    if (stepPx / minorCount >= 8.0) {
                        for (var sub = 1; sub < minorCount; ++sub) {
                            var subX = x + sub * (stepPx / minorCount)
                            if (subX >= 0 && subX <= width) {
                                ctx.beginPath()
                                ctx.moveTo(subX, 20)
                                ctx.lineTo(subX, 28)
                                ctx.stroke()
                            }
                        }
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                root.seekFromCanvasX(mouse.x + flick.contentX)
            }
            onPositionChanged: (mouse) => {
                if (pressed) {
                    root.seekFromCanvasX(mouse.x + flick.contentX)
                }
            }
            onWheel: (wheel) => {
                if (wheel.modifiers & (Qt.ControlModifier | Qt.AltModifier)) {
                    var delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                    if (delta !== 0) {
                        wheel.accepted = true
                        root.zoomAtViewportX(wheel.x, Math.pow(1.15, delta / 120.0))
                    }
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
        clip: true
        contentWidth: root.requiredContentWidth(root.effectivePixelsPerSecond)
        contentHeight: Math.max(height, root.effectiveTrackCount * root.trackHeight)
        boundsBehavior: Flickable.StopAtBounds

        onContentXChanged: {
            rulerCanvas.requestPaint()
            gridCanvas.requestPaint()
        }
        onContentYChanged: gridCanvas.requestPaint()
        onContentWidthChanged: root.clampScroll()
        onContentHeightChanged: {
            root.clampScroll()
            gridCanvas.requestPaint()
        }
        onWidthChanged: {
            root.clampScroll()
            rulerCanvas.requestPaint()
            gridCanvas.requestPaint()
        }
        onHeightChanged: {
            root.clampScroll()
            gridCanvas.requestPaint()
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        // Track Lanes Backgrounds
        Repeater {
            id: trackLaneRepeater
            model: root.hasLiveTimeline ? timelineModel.tracksModel : []

            Rectangle {
                required property int index
                required property string trackId
                required property string trackName
                required property string trackType
                required property int trackIndex
                required property bool muted
                required property bool solo

                readonly property string laneTrackId: trackId
                readonly property string laneTrackName: trackName
                readonly property string laneTrackType: trackType
                readonly property int laneTrackIndex: trackIndex
                readonly property bool laneMuted: muted

                x: 0
                y: index * root.trackHeight
                width: flick.contentWidth
                height: root.trackHeight
                z: 0
                color: index % 2 === 0 ? "#141418" : "#17171d"
                border.color: muted ? "#3f2e2e" : "#22222a"
                border.width: 1
            }
        }

        // Viewport-Windowed Vertical Gridlines (Bounded Memory at Any Timeline Length / Zoom)
        Canvas {
            id: gridCanvas
            x: flick.contentX
            y: flick.contentY
            width: Math.max(0, flick.width)
            height: Math.max(0, Math.min(flick.height, Math.max(0, root.effectiveTrackCount * root.trackHeight - flick.contentY)))
            z: 1
            Component.onCompleted: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (width <= 0 || height <= 0) return

                var pps = Math.max(1.0, root.effectivePixelsPerSecond)
                var spacing = root.computeRulerSpacing(pps, root.effectiveFps)
                var stepSec = spacing.stepSec
                var scrollX = flick.contentX

                var firstTick = Math.max(0, Math.floor(scrollX / (stepSec * pps)))
                var lastTick = Math.min(firstTick + 512, Math.ceil((scrollX + width) / (stepSec * pps)))

                ctx.strokeStyle = "#1b1b22"
                ctx.lineWidth = 1
                for (var tick = firstTick; tick <= lastTick; ++tick) {
                    var gx = tick * stepSec * pps - scrollX
                    if (gx >= 0 && gx <= width) {
                        ctx.beginPath()
                        ctx.moveTo(gx, 0)
                        ctx.lineTo(gx, height)
                        ctx.stroke()
                    }
                }
            }
        }

        // Background Interaction Area (Empty Deselect & Razor Split Along Timeline)
        MouseArea {
            id: backgroundMouse
            x: 0
            y: 0
            width: flick.contentWidth
            height: flick.contentHeight
            z: 2
            cursorShape: root.effectiveActiveTool === 1 ? Qt.SplitHCursor : Qt.ArrowCursor

            onClicked: (mouse) => {
                if (!root.hasLiveTimeline) return
                if (root.effectiveActiveTool === 1) {
                    var splitSec = root.seekFromCanvasX(mouse.x)
                    timelineModel.splitAtPlayhead(splitSec)
                } else {
                    timelineModel.selectedClipId = ""
                }
            }
        }

        // Real Timeline Clips
        Repeater {
            id: clipRepeater
            model: root.hasLiveTimeline ? timelineModel.clipsModel : []

            Rectangle {
                id: clipItem
                required property string clipId
                required property string trackId
                required property string assetId
                required property string clipName
                required property double timelineInSec
                required property double durationSec
                required property double sourceInSec
                required property int trackRow
                required property bool isSelected
                required property bool hasWaveform
                required property var waveformPeaks

                readonly property var laneItem: root.laneInfoAtRow(trackRow)
                readonly property bool isAudioTrack: laneItem ? (laneItem.laneTrackType === "audio") : false
                readonly property int trackTypeIndex: laneItem ? laneItem.laneTrackIndex : 0
                readonly property bool trackMuted: laneItem ? laneItem.laneMuted : false
                readonly property bool clipSelected: isSelected || (root.effectiveSelectedClipId !== "" && root.effectiveSelectedClipId === clipId)

                readonly property string fillColor: isAudioTrack
                                                    ? (trackTypeIndex % 2 === 0 ? "#064e3b" : "#134e4a")
                                                    : (trackTypeIndex > 0 ? "#1e3a5f" : "#1e293b")
                readonly property string headerColor: isAudioTrack
                                                      ? (trackTypeIndex % 2 === 0 ? "#047857" : "#0f766e")
                                                      : (trackTypeIndex > 0 ? "#2563eb" : "#1d4ed8")
                readonly property string normalBorderColor: isAudioTrack
                                                            ? (trackTypeIndex % 2 === 0 ? "#10b981" : "#14b8a6")
                                                            : (trackTypeIndex > 0 ? "#38bdf8" : "#60a5fa")
                readonly property string waveStrokeColor: isAudioTrack
                                                          ? (trackTypeIndex % 2 === 0 ? "#34d399" : "#2dd4bf")
                                                          : "#38bdf8"

                readonly property real modelX: Math.max(0.0, timelineInSec * root.effectivePixelsPerSecond)
                readonly property real modelY: Math.max(0, trackRow) * root.trackHeight + 3
                readonly property real modelWidth: Math.max(6.0, durationSec * root.effectivePixelsPerSecond)

                property bool isDragging: false
                property bool dragMoved: false
                property real pressCanvasX: 0.0
                property real pressCanvasY: 0.0
                property real dragOriginX: 0.0
                property real dragOriginY: 0.0
                property real previewX: modelX
                property real previewY: modelY

                property var peaksData: waveformPeaks
                property bool clipHasWaveform: hasWaveform
                property bool clipIsAudioTrack: isAudioTrack

                onPeaksDataChanged: waveformCanvas.requestPaint()
                onClipHasWaveformChanged: waveformCanvas.requestPaint()
                onClipIsAudioTrackChanged: waveformCanvas.requestPaint()
                onModelWidthChanged: waveformCanvas.requestPaint()

                x: isDragging ? previewX : modelX
                y: isDragging ? previewY : modelY
                width: modelWidth
                height: root.trackHeight - 6
                z: isDragging ? 50 : (clipSelected ? 20 : 10)
                radius: 4
                clip: true
                color: fillColor
                border.color: clipSelected ? "#facc15" : normalBorderColor
                border.width: clipSelected ? 2 : 1
                opacity: isDragging ? 0.85 : (trackMuted ? 0.60 : 1.0)

                // Clip Title Header Strip
                Rectangle {
                    id: clipHeader
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: clipItem.isAudioTrack ? 16 : 18
                    color: clipItem.clipSelected ? "#2563eb" : clipItem.headerColor
                    radius: 3

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 4

                        Text {
                            text: clipItem.clipName
                            color: "#ffffff"
                            font.bold: true
                            font.pixelSize: clipItem.isAudioTrack ? 9 : 10
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            visible: clipItem.width >= 100
                            text: Number(clipItem.durationSec).toFixed(2) + "s"
                            color: "#bfdbfe"
                            font.pixelSize: 9
                        }
                    }
                }

                // Viewport-Windowed Real Audio Waveform Rendering (<= 128 MediaCache Peaks or Baseline)
                Canvas {
                    id: waveformCanvas
                    readonly property real visLeft: Math.max(0.0, flick.contentX - clipItem.x)
                    readonly property real visRight: Math.min(clipItem.width, flick.contentX + flick.width - clipItem.x)

                    x: visLeft
                    y: clipHeader.height
                    width: Math.max(0.0, visRight - visLeft)
                    height: Math.max(0.0, clipItem.height - clipHeader.height)
                    visible: width > 0 && height > 2 && (clipItem.clipHasWaveform || clipItem.clipIsAudioTrack)

                    Component.onCompleted: if (visible) requestPaint()
                    onVisLeftChanged: if (visible) requestPaint()
                    onWidthChanged: if (visible) requestPaint()
                    onHeightChanged: if (visible) requestPaint()
                    onVisibleChanged: if (visible) requestPaint()

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        if (width <= 2 || height <= 2) return

                        var midY = height / 2.0
                        var peaks = clipItem.peaksData
                        var peakCount = (peaks && peaks.length !== undefined) ? peaks.length : 0

                        if (!clipItem.clipHasWaveform || peakCount <= 0) {
                            if (clipItem.clipIsAudioTrack) {
                                ctx.strokeStyle = clipItem.waveStrokeColor
                                ctx.globalAlpha = 0.35
                                ctx.lineWidth = 1
                                ctx.beginPath()
                                ctx.moveTo(2, midY)
                                ctx.lineTo(Math.max(2, width - 2), midY)
                                ctx.stroke()
                                ctx.globalAlpha = 1.0
                            }
                            return
                        }

                        ctx.strokeStyle = clipItem.waveStrokeColor
                        ctx.globalAlpha = 0.25
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(0, midY)
                        ctx.lineTo(width, midY)
                        ctx.stroke()

                        ctx.globalAlpha = 1.0
                        ctx.strokeStyle = clipItem.waveStrokeColor
                        ctx.lineWidth = 1

                        var fullW = Math.max(1.0, clipItem.width)
                        var maxBars = Math.min(128, peakCount)
                        var maxAmp = Math.max(1.0, midY - 3.0)

                        for (var wx = 2; wx < width - 1; wx += 2) {
                            var u0 = Math.max(0.0, Math.min(1.0, (visLeft + wx - 1.0) / fullW))
                            var u1 = Math.max(u0, Math.min(1.0, (visLeft + wx + 1.0) / fullW))
                            var idx0 = Math.min(maxBars - 1, Math.max(0, Math.floor(u0 * maxBars)))
                            var idx1 = Math.min(maxBars - 1, Math.max(idx0, Math.floor(u1 * maxBars)))

                            var peak = 0.0
                            for (var k = idx0; k <= idx1; ++k) {
                                var raw = Number(peaks[k])
                                if (isFinite(raw)) {
                                    var clamped = Math.max(0.0, Math.min(1.0, raw))
                                    if (clamped > peak) peak = clamped
                                }
                            }

                            var amp = peak * maxAmp
                            if (amp < 0.5) amp = 0.5
                            ctx.beginPath()
                            ctx.moveTo(wx, midY - amp)
                            ctx.lineTo(wx, midY + amp)
                            ctx.stroke()
                        }
                    }
                }

                HoverHandler {
                    id: clipHover
                }

                ToolTip.visible: clipHover.hovered && !clipItem.isDragging
                ToolTip.delay: 450
                ToolTip.text: clipItem.clipName
                              + " | In: " + root.formatNdfTimecode(clipItem.timelineInSec, root.effectiveFps)
                              + " | Dur: " + Number(clipItem.durationSec).toFixed(2) + "s"
                              + " | SrcIn: " + Number(clipItem.sourceInSec).toFixed(2) + "s"

                MouseArea {
                    id: clipMouse
                    anchors.fill: parent
                    preventStealing: root.effectiveActiveTool === 0
                    cursorShape: root.effectiveActiveTool === 1
                                 ? Qt.SplitHCursor
                                 : (clipItem.isDragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor)

                    onPressed: (mouse) => {
                        if (!root.hasLiveTimeline) return
                        if (root.effectiveActiveTool === 0) {
                            timelineModel.selectedClipId = clipItem.clipId
                            var pt = clipMouse.mapToItem(flick.contentItem, mouse.x, mouse.y)
                            clipItem.pressCanvasX = pt.x
                            clipItem.pressCanvasY = pt.y
                            clipItem.dragOriginX = clipItem.modelX
                            clipItem.dragOriginY = clipItem.modelY
                            clipItem.previewX = clipItem.modelX
                            clipItem.previewY = clipItem.modelY
                            clipItem.dragMoved = false
                            clipItem.isDragging = false
                        } else if (root.effectiveActiveTool !== 1) {
                            timelineModel.selectedClipId = clipItem.clipId
                        }
                    }

                    onPositionChanged: (mouse) => {
                        if (!pressed || root.effectiveActiveTool !== 0) {
                            return
                        }
                        var pt = clipMouse.mapToItem(flick.contentItem, mouse.x, mouse.y)
                        var dx = pt.x - clipItem.pressCanvasX
                        var dy = pt.y - clipItem.pressCanvasY

                        if (!clipItem.dragMoved && (Math.abs(dx) >= 4 || Math.abs(dy) >= 4)) {
                            clipItem.dragMoved = true
                            clipItem.isDragging = true
                        }

                        if (clipItem.dragMoved) {
                            clipItem.previewX = Math.max(0.0, clipItem.dragOriginX + dx)
                            var maxRow = Math.max(0, root.effectiveTrackCount - 1)
                            var centerY = clipItem.dragOriginY + dy + (clipItem.height * 0.5)
                            var targetRow = Math.max(0, Math.min(maxRow, Math.floor(centerY / root.trackHeight)))
                            clipItem.previewY = targetRow * root.trackHeight + 3
                        }
                    }

                    onReleased: {
                        if (root.effectiveActiveTool !== 0) {
                            return
                        }
                        var didDrag = clipItem.isDragging && clipItem.dragMoved
                        var targetSec = Math.max(0.0, clipItem.previewX / Math.max(1.0, root.effectivePixelsPerSecond))
                        var maxRow = Math.max(0, root.effectiveTrackCount - 1)
                        var targetRow = Math.max(0, Math.min(maxRow, Math.floor((clipItem.previewY + clipItem.height * 0.5) / root.trackHeight)))
                        var dstTrackId = root.trackIdAtRow(targetRow)

                        clipItem.isDragging = false
                        clipItem.dragMoved = false

                        if (didDrag && root.hasLiveTimeline && dstTrackId !== "") {
                            if (!timelineModel.moveSelectedClip(dstTrackId, targetSec)) {
                                clipItem.previewX = clipItem.modelX
                                clipItem.previewY = clipItem.modelY
                            }
                        } else {
                            clipItem.previewX = clipItem.modelX
                            clipItem.previewY = clipItem.modelY
                        }
                    }

                    onCanceled: {
                        clipItem.isDragging = false
                        clipItem.dragMoved = false
                        clipItem.previewX = clipItem.modelX
                        clipItem.previewY = clipItem.modelY
                    }

                    onClicked: (mouse) => {
                        if (!root.hasLiveTimeline) return
                        if (root.effectiveActiveTool === 1) {
                            var cid = clipItem.clipId
                            var pt = clipMouse.mapToItem(flick.contentItem, mouse.x, mouse.y)
                            var splitSec = root.seekFromCanvasX(pt.x)
                            timelineModel.selectedClipId = cid
                            timelineModel.splitAtPlayhead(splitSec)
                        }
                    }
                }
            }
        }

        // SMPTE Scrubbing Playhead
        Item {
            id: playheadItem
            x: root.effectivePlayheadX - 6
            y: 0
            width: 13
            height: flick.contentHeight
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
                    ctx.fillStyle = "#ef4444"
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

    // Viewport-Level Wheel Modifier Overlay (Zoom / Horizontal Pan) without Intercepting Clicks
    MouseArea {
        anchors.fill: flick
        acceptedButtons: Qt.NoButton
        z: 5
        onWheel: (wheel) => {
            if (wheel.modifiers & (Qt.ControlModifier | Qt.AltModifier)) {
                var zoomDelta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                if (zoomDelta !== 0) {
                    wheel.accepted = true
                    root.zoomAtViewportX(wheel.x, Math.pow(1.15, zoomDelta / 120.0))
                }
            } else if (wheel.modifiers & Qt.ShiftModifier) {
                var panDelta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                if (panDelta !== 0) {
                    wheel.accepted = true
                    var maxX = Math.max(0.0, flick.contentWidth - flick.width)
                    flick.contentX = Math.max(0.0, Math.min(maxX, flick.contentX - panDelta * 0.6))
                }
            } else {
                wheel.accepted = false
            }
        }
    }
}
