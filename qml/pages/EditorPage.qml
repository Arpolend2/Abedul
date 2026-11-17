import QtQuick 2.5
import Sailfish.Silica 1.0
import ru.auroraos.amberpdf 1.0
import Abedul 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string projectId
    property string projectName
    property string compileRoot: ""
    property string currentFile: ""
    property bool initialized: false
    property bool dirty: false
    property string baseText: ""
    property bool baseKnown: false
    property string actionError: ""
    property string pendingCompileRoot: ""
    property bool deferOpen: false
    property bool started: false
    property bool lastFileRestored: false
    property bool compileHintShown: false
    property string zoomTarget: "editor"
    property string coverSnippet: ""
    property string coverSnippetFile: ""
    property bool coverHasEdits: false
    property var coverPdfSnapshot: null
    property string coverPdfError: ""
    property int coverPdfRevision: 0
    property var pendingSyncSource: null
    property string syncRequestFile: ""
    property string syncRequestText: ""
    property string pendingSnapshotFile: ""
    property string pendingSnapshotText: ""
    property string compiledSnapshotFile: ""
    property string compiledSnapshotText: ""
    property bool compileInProgress: overleafClient.pdfBusy
                                     || (pendingCompileRoot.length > 0 && overleafClient.sourceBusy)
    property bool openingSource: started && overleafClient.sourceBusy && !pendingCompileRoot.length
    property bool lightEditorText: editor.color.r * 0.299 + editor.color.g * 0.587
                                   + editor.color.b * 0.114 > 0.5
    property color editorBackground: Theme.colorScheme === Theme.LightOnDark ? "black" : "white"
    property LatexHighlighter highlighter: LatexHighlighter {
        textDocument: page.currentFile.match(/\.(tex|sty|cls|bib)$/i)
                      ? editor._editor.textDocument : null
        commandColor: page.lightEditorText ? "#82b1ff" : "#005cc5"
        commentColor: page.lightEditorText ? "#83c98b" : "#216e39"
        mathColor: page.lightEditorText ? "#ffbd72" : "#9a4500"
        argumentColor: page.lightEditorText ? "#d6a5ff" : "#6f42a1"
        bibMode: /\.bib$/i.test(page.currentFile)
    }
    backNavigation: !dirty && !overleafClient.sourceBusy

    function saveDraft() {
        if (dirty) {
            var saved = currentFile.length
                ? draftStore.saveFileWithBase(overleafClient.server, projectId, currentFile,
                                              editor.text, baseText, baseKnown)
                : draftStore.save(overleafClient.server, projectId, editor.text)
            if (saved)
                dirty = false
        }
    }

    function updateCoverSnippet() {
        var text = editor.text
        var position = Math.max(0, Math.min(editor._editor.cursorPosition, text.length))
        var line = text.substring(0, position).split("\n").length - 1
        var lines = text.split("\n")
        var start = Math.max(0, line - 2)
        coverSnippet = lines.slice(start, start + 5).join("\n")
        coverSnippetFile = currentFile
    }

    function scheduleCoverPdfCapture() {
        ++coverPdfRevision
        if (pdfLoader.item && pdfLoader.item.status === PdfDocument.Ready
                && Qt.application.state === Qt.ApplicationActive)
            coverPdfTimer.restart()
    }

    function captureCoverPdf() {
        var view = pdfLoader.item
        if (!view || view.status !== PdfDocument.Ready || view.width <= 0 || view.height <= 0)
            return
        var revision = coverPdfRevision
        var pdfPath = overleafClient.pdfPath
        var scale = 256 / Math.max(view.width, view.height)
        var targetWidth = Math.max(1, Math.round(view.width * scale))
        var targetHeight = Math.max(1, Math.round(view.height * scale))
        var started = view.grabToImage(function(result) {
            if (revision !== page.coverPdfRevision || pdfPath !== overleafClient.pdfPath
                    || view !== pdfLoader.item)
                return
            page.coverPdfSnapshot = result
            page.coverPdfError = ""
        }, Qt.size(targetWidth, targetHeight))
        if (!started)
            coverPdfError = qsTr("Could not capture the PDF preview.")
    }

    function activateCover() {
        if (started && typeof overleafClient.setCoverEditor === "function") {
            overleafClient.setCoverEditor(page)
            overleafClient.coverShowsProjects = false
        }
    }

    function saveAndCompile() {
        if (overleafClient.sourceBusy || overleafClient.pdfBusy)
            return
        actionError = ""
        if (!currentFile.length) {
            actionError = qsTr("Open a project file before saving.")
            return
        }
        if (!compileRoot.length) {
            actionError = qsTr("Open a .tex file first to select the main document.")
            return
        }
        saveDraft()
        if (dirty)
            return
        pendingCompileRoot = compileRoot
        overleafClient.saveProjectSource(projectId, currentFile, baseText, baseKnown, editor.text)
    }

    function adjustZoom(direction) {
        if (zoomTarget === "pdf") {
            if (pdfLoader.item)
                pdfLoader.item.zoom = Math.max(0.5, Math.min(6.0,
                                          pdfLoader.item.zoom * (direction > 0 ? 1.1 : 1 / 1.1)))
        } else {
            overleafClient.editorFontScale = Math.max(70, Math.min(160,
                                             overleafClient.editorFontScale + direction * 10))
        }
    }

    function openProjectFile(path) {
        saveDraft()
        if (dirty || overleafClient.sourceBusy)
            return false
        overleafClient.openProjectSource(projectId, path)
        return true
    }

    function syncAllowed() {
        if (!overleafClient.pdfPath.length || overleafClient.pdfBusy || overleafClient.sourceBusy) {
            actionError = qsTr("Compile the project and wait for the file to open before using SyncTeX.")
            return false
        }
        if (!currentFile.length || !baseKnown || editor.text !== baseText
                || (currentFile === compiledSnapshotFile && editor.text !== compiledSnapshotText)) {
            actionError = qsTr("Save and compile local changes before using SyncTeX.")
            return false
        }
        actionError = ""
        syncRequestFile = currentFile
        syncRequestText = editor.text
        return true
    }

    function normalizeSyncPath(path) {
        var parts = path.split("/")
        var normalized = []
        for (var i = 0; i < parts.length; ++i) {
            if (parts[i] === ".." || parts[i].indexOf("\\") !== -1)
                return ""
            if (parts[i].length && parts[i] !== ".")
                normalized.push(parts[i])
        }
        return normalized.length ? "/" + normalized.join("/") : ""
    }

    function moveToSource(position) {
        if (!baseKnown || editor.text !== baseText
                || (currentFile === compiledSnapshotFile && editor.text !== compiledSnapshotText)) {
            actionError = qsTr("The target file has local changes. Save and compile before using SyncTeX.")
            return
        }
        var lines = editor.text.split("\n")
        if (position.line < 1 || position.line > lines.length) {
            actionError = qsTr("The SyncTeX source line is not available in this file.")
            return
        }
        var offset = 0
        for (var i = 0; i < position.line - 1; ++i)
            offset += lines[i].length + 1
        var column = position.column >= 0 ? position.column : 0
        editor._editor.cursorPosition = offset + Math.min(column, lines[position.line - 1].length)
        editor._editor.forceActiveFocus()
        var rect = editor._editor.positionToRectangle(editor._editor.cursorPosition)
        var mapped = editor._editor.mapToItem(codePane.contentItem, rect.x, rect.y)
        codePane.contentY = Math.max(0, Math.min(codePane.contentHeight - codePane.height,
                                                mapped.y - codePane.height / 2))
    }

    function restoreLastFile() {
        if (lastFileRestored)
            return
        lastFileRestored = true
        var lastFile = draftStore.loadLastFile(overleafClient.server, projectId)
        if (lastFile.length)
            overleafClient.openProjectSource(projectId, lastFile)
    }

    function initializeEditor() {
        if (started)
            return
        started = true
        compileRoot = draftStore.loadCompileRoot(overleafClient.server, projectId)
        if (draftStore.error.length)
            return
        if (draftStore.load(overleafClient.server, projectId)) {
            editor.text = draftStore.text
            initialized = true
        } else {
            return
        }
        restoreLastFile()
    }
    Component.onCompleted: {
        if (!deferOpen || status === PageStatus.Active)
            initializeEditor()
        if (status === PageStatus.Active)
            activateCover()
    }
    onStatusChanged: {
        if (status === PageStatus.Active) {
            if (deferOpen)
                initializeEditor()
            activateCover()
        }
    }
    Component.onDestruction: {
        if (started) {
            if (typeof overleafClient.setCoverEditor === "function" && overleafClient.coverEditor === page)
                overleafClient.setCoverEditor(null)
            saveDraft()
            overleafClient.closeProjectPdf()
            overleafClient.closeProjectSource()
        }
    }

    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state !== Qt.ApplicationActive) {
                if (coverSnippetTimer.running) {
                    coverSnippetTimer.stop()
                    page.updateCoverSnippet()
                }
                page.saveDraft()
                if (coverPdfTimer.running) {
                    coverPdfTimer.stop()
                    page.captureCoverPdf()
                }
            } else {
                page.scheduleCoverPdfCapture()
            }
        }
    }
    Connections {
        target: overleafClient
        onSourceErrorChanged: {
            if (overleafClient.sourceError.length && !overleafClient.sourceBusy) {
                page.pendingCompileRoot = ""
                page.pendingSyncSource = null
            }
        }
        onPdfPathChanged: {
            ++page.coverPdfRevision
            page.coverPdfSnapshot = null
            page.coverPdfError = ""
            if (!overleafClient.pdfPath.length) {
                compileHintTimer.stop()
            } else {
                page.compiledSnapshotFile = page.pendingSnapshotFile
                page.compiledSnapshotText = page.pendingSnapshotText
                page.pendingSnapshotFile = ""
                page.pendingSnapshotText = ""
                if (!page.compileHintShown) {
                    page.compileHintShown = true
                    compileHintTimer.restart()
                }
            }
        }
        onSourceTextChanged: {
            if (!page.started || !overleafClient.sourcePath.length)
                return
            if (coverSnippetTimer.running) {
                coverSnippetTimer.stop()
                page.updateCoverSnippet()
            }
            if (!draftStore.loadFile(overleafClient.server, page.projectId, overleafClient.sourcePath))
                return
            page.initialized = false
            page.currentFile = overleafClient.sourcePath
            var useDraft = draftStore.exists
                           && (!draftStore.baseKnown || draftStore.text !== draftStore.baseText)
            editor.text = useDraft ? draftStore.text : overleafClient.sourceText
            page.baseKnown = useDraft ? draftStore.baseKnown : true
            page.baseText = useDraft ? draftStore.baseText : overleafClient.sourceText
            if (editor.text === overleafClient.sourceText) {
                page.baseKnown = true
                page.baseText = overleafClient.sourceText
            }
            page.actionError = ""
            if (/\.tex$/i.test(page.currentFile) && !page.pendingSyncSource) {
                page.compileRoot = page.currentFile
                if (!draftStore.saveCompileRoot(overleafClient.server, page.projectId, page.compileRoot))
                    page.actionError = draftStore.error
            }
            if (!draftStore.saveLastFile(overleafClient.server, page.projectId, page.currentFile))
                page.actionError = draftStore.error
            page.initialized = true
            if (!page.coverHasEdits)
                page.updateCoverSnippet()
            if (page.pendingSyncSource && page.pendingSyncSource.path === page.currentFile) {
                var location = page.pendingSyncSource
                page.pendingSyncSource = null
                page.moveToSource(location)
            }
        }
        onSourceSaved: {
            if (!page.started || path !== page.currentFile || !page.pendingCompileRoot.length)
                return
            page.baseText = text
            page.baseKnown = true
            if (!draftStore.saveFileWithBase(overleafClient.server, page.projectId, path, text, text, true)) {
                page.dirty = true
                page.pendingCompileRoot = ""
                return
            }
            var root = page.pendingCompileRoot
            page.pendingCompileRoot = ""
            page.pendingSnapshotFile = path
            page.pendingSnapshotText = text
            overleafClient.compileProjectPdf(page.projectId, root)
        }
        onSyncError: page.actionError = message
        onSyncPdfPositions: {
            if (page.syncRequestFile !== page.currentFile || page.syncRequestText !== editor.text) {
                page.actionError = qsTr("The source changed while SyncTeX was running.")
                return
            }
            if (pdfLoader.item && positions.length)
                pdfLoader.item.showSyncPosition(positions[0])
        }
        onSyncSourcePositions: {
            if (page.syncRequestFile !== page.currentFile || page.syncRequestText !== editor.text) {
                page.actionError = qsTr("The source changed while SyncTeX was running.")
                return
            }
            if (!positions.length)
                return
            var position = positions[0]
            var path = page.normalizeSyncPath(position.file)
            if (!path) {
                page.actionError = qsTr("The server returned an invalid source path.")
                return
            }
            position.path = path
            if (path === page.currentFile) {
                page.moveToSource(position)
            } else {
                page.pendingSyncSource = position
                if (!page.openProjectFile(path))
                    page.pendingSyncSource = null
            }
        }
    }

    PdfGeometry {
        id: pdfGeometry
        objectName: "pdfGeometry"
        path: overleafClient.pdfPath
    }

    OpenCachedPdf {
        id: externalPdf
    }

    DoubleTapMonitor {
        target: page.status === PageStatus.Active && page.started ? editor._editor : null
        onDoubleTapped: {
            if (!page.syncAllowed())
                return
            var index = editor._editor.positionAt(x, y)
            if (index < 0)
                return
            var before = editor.text.substring(0, index).split("\n")
            overleafClient.syncSourceToPdf(page.projectId, page.currentFile,
                                          before.length, before[before.length - 1].length)
        }
    }

    Shortcut {
        objectName: "saveAndCompileShortcut"
        sequence: StandardKey.Save
        enabled: page.status === PageStatus.Active
        autoRepeat: false
        onActivated: page.saveAndCompile()
    }
    Shortcut {
        sequence: StandardKey.ZoomIn
        enabled: page.status === PageStatus.Active
        onActivated: page.adjustZoom(1)
    }
    Shortcut {
        sequence: StandardKey.ZoomOut
        enabled: page.status === PageStatus.Active
        onActivated: page.adjustZoom(-1)
    }
    Connections {
        target: editor._editor
        onActiveFocusChanged: {
            if (editor._editor.activeFocus && !pdfHover.containsMouse)
                page.zoomTarget = "editor"
        }
    }

    Timer {
        id: saveTimer
        interval: 500
        onTriggered: page.saveDraft()
    }
    Timer {
        id: coverSnippetTimer
        interval: 0
        onTriggered: page.updateCoverSnippet()
    }
    Timer {
        id: coverPdfTimer
        interval: 600
        onTriggered: page.captureCoverPdf()
    }

    Timer {
        id: compileHintTimer
        objectName: "compileHintTimer"
        interval: 5000
    }

    WheelScroll {
        objectName: "editorPulleyWheel"
        parent: page
        anchors.fill: undefined
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.min(page.height, header.height
                         + (headerPane.pullDownMenu.active ? headerPane.pullDownMenu.height : 0))
        flickable: headerPane
    }

    SilicaFlickable {
        id: headerPane
        objectName: "editorHeader"
        anchors.fill: parent
        contentHeight: height
        clip: true
        PullDownMenu {
            background: Rectangle {
                width: parent.width
                height: parent.height
                color: page.editorBackground
            }
            MenuLabel {
                objectName: "editorProjectName"
                text: page.projectName
            }
            MenuItem {
                objectName: "editorSettings"
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                objectName: "openCachedPdf"
                text: qsTr("Open PDF in another app")
                enabled: overleafClient.pdfPath.length > 0 && !overleafClient.pdfBusy
                onClicked: externalPdf.open()
            }
            MenuItem {
                objectName: "downloadProjectArchive"
                text: qsTr("Download saved project ZIP")
                enabled: !overleafClient.archiveBusy
                onClicked: overleafClient.downloadProjectArchive(page.projectId)
            }
            MenuItem {
                objectName: "editorProjectFiles"
                text: qsTr("Project files")
                onClicked: pageStack.push(Qt.resolvedUrl("ProjectFilesPage.qml"), {
                    "projectId": page.projectId,
                    "projectName": page.projectName,
                    "editorPage": page
                })
            }
            MenuItem {
                objectName: "saveAndCompile"
                text: qsTr("Save and compile")
                enabled: !overleafClient.sourceBusy && !overleafClient.pdfBusy
                onClicked: page.saveAndCompile()
            }
        }
        Rectangle {
            id: header
            objectName: "editorHeaderBackground"
            width: headerPane.width
            height: Theme.itemSizeExtraSmall
            color: page.editorBackground
        }
        Item {
            id: panes
            objectName: "editorPanes"
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            property bool sideBySide: width > height

            SilicaFlickable {
                id: codePane
                objectName: "editorCodePane"
                x: panes.sideBySide && overleafClient.pdfOnLeft ? panes.width / 2 : 0
                width: panes.sideBySide ? panes.width / 2 : panes.width
                height: panes.sideBySide ? panes.height : panes.height / 2
                contentHeight: codeColumn.height
                clip: true
                Column {
                    id: codeColumn
                    width: parent.width
                    opacity: page.openingSource ? 0.3 : 1
                    Label {
                        objectName: "sourceStatus"
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        visible: page.currentFile.length > 0 && (!page.baseKnown || editor.text !== page.baseText)
                        text: qsTr("Local changes to %1 are not saved to Overleaf.").arg(page.currentFile)
                    }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        color: Theme.errorColor
                        visible: draftStore.error.length > 0 || overleafClient.sourceError.length > 0
                                 || page.actionError.length > 0 || externalPdf.error.length > 0
                                 || (overleafClient.archiveProject === page.projectId
                                     && overleafClient.archiveError.length > 0)
                        text: draftStore.error.length ? draftStore.error
                              : overleafClient.sourceError.length ? overleafClient.sourceError
                              : page.actionError.length ? page.actionError
                              : externalPdf.error.length ? externalPdf.error
                              : overleafClient.archiveProject === page.projectId ? overleafClient.archiveError : ""
                    }
                    Label {
                        objectName: "archiveStatus"
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        color: Theme.secondaryColor
                        visible: overleafClient.archiveProject === page.projectId
                                 && (overleafClient.archiveBusy || overleafClient.archivePath.length > 0)
                        text: overleafClient.archiveBusy ? qsTr("Downloading project ZIP…")
                              : qsTr("ZIP saved to %1").arg(overleafClient.archivePath)
                    }
                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: page.dirty && draftStore.error.length > 0
                        text: qsTr("Retry saving")
                        onClicked: page.saveDraft()
                    }
                    TextArea {
                        id: editor
                        objectName: "draftEditor"
                        width: parent.width
                        enabled: page.initialized && !overleafClient.sourceBusy
                        label: page.currentFile.length ? page.currentFile : qsTr("LaTeX draft")
                        placeholderText: qsTr("Write LaTeX here")
                        font.family: "monospace"
                        font.pixelSize: Math.round(Theme.fontSizeMedium * overleafClient.editorFontScale / 100)
                        color: Theme.colorScheme === Theme.LightOnDark ? "#dedede" : "#262626"
                        onTextChanged: {
                            if (page.initialized) {
                                page.dirty = true
                                page.coverHasEdits = true
                                coverSnippetTimer.restart()
                                saveTimer.restart()
                            }
                        }
                    }
                }
                VerticalScrollDecorator {}
                WheelScroll {
                    objectName: "editorCodeWheel"
                    parent: codePane
                    flickable: codePane
                    controlWheelZoom: true
                    trackPointer: true
                    onPointerEntered: page.zoomTarget = "editor"
                    onZoomRequested: {
                        page.zoomTarget = "editor"
                        page.adjustZoom(direction)
                    }
                }
                Rectangle {
                    objectName: "sourceLoadingIndicator"
                    parent: codePane
                    anchors.centerIn: parent
                    width: Math.min(parent.width, sourceLoadingContents.implicitWidth + 4 * Theme.paddingLarge)
                    height: sourceLoadingContents.implicitHeight + 4 * Theme.paddingLarge
                    radius: Theme.paddingMedium
                    color: page.editorBackground
                    visible: page.openingSource
                    z: 1
                    Column {
                        id: sourceLoadingContents
                        anchors.centerIn: parent
                        spacing: Theme.paddingMedium
                        BusyIndicator {
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: BusyIndicatorSize.Large
                            running: page.openingSource
                        }
                        Label {
                            text: qsTr("Opening file…")
                            color: Theme.highlightColor
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }

            Item {
                id: pdfPane
                objectName: "editorPdfPane"
                x: panes.sideBySide && !overleafClient.pdfOnLeft ? codePane.width : 0
                y: panes.sideBySide ? 0 : codePane.height
                width: panes.sideBySide ? panes.width - codePane.width : panes.width
                height: panes.sideBySide ? panes.height : panes.height - codePane.height
                Column {
                    id: pdfHeader
                    width: parent.width
                    Label {
                        objectName: "pdfCompileHint"
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeSmall
                        text: qsTr("Pull down from the top to save and compile again.")
                        opacity: compileHintTimer.running && overleafClient.pdfPath.length ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 250 } }
                    }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        color: Theme.errorColor
                        visible: overleafClient.pdfError.length > 0
                        text: overleafClient.pdfError
                    }
                }
                MouseArea {
                    id: pdfHover
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: true
                    onEntered: page.zoomTarget = "pdf"
                    Loader {
                        id: pdfLoader
                        objectName: "pdfLoader"
                        anchors.fill: parent
                        anchors.topMargin: pdfHeader.height
                        active: overleafClient.pdfPath.length > 0
                        clip: true
                        sourceComponent: SyncPdfView {
                            objectName: "pdfPreview"
                            documentProvider: pdfDocument
                            pdfPath: overleafClient.pdfPath
                            fitToPage: true
                            geometry: pdfGeometry
                            onContentXChanged: page.scheduleCoverPdfCapture()
                            onContentYChanged: page.scheduleCoverPdfCapture()
                            onZoomChanged: page.scheduleCoverPdfCapture()
                            onWidthChanged: page.scheduleCoverPdfCapture()
                            onHeightChanged: page.scheduleCoverPdfCapture()
                            onStatusChanged: page.scheduleCoverPdfCapture()
                            onContentChanged: page.scheduleCoverPdfCapture()
                            onClicked: page.zoomTarget = "pdf"
                            onSyncFailure: page.actionError = message
                            onSyncToSource: {
                                if (page.syncAllowed())
                                    overleafClient.syncPdfToSource(page.projectId, pageNumber, h, v)
                            }
                            PdfDocument { id: pdfDocument }
                        }
                    }
                    MouseArea {
                        objectName: "pdfZoomWheel"
                        anchors.fill: parent
                        z: 2
                        acceptedButtons: Qt.NoButton
                        onWheel: {
                            var delta = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y
                            if (!(wheel.modifiers & Qt.ControlModifier) || delta === 0 || !pdfLoader.item) {
                                wheel.accepted = false
                                return
                            }
                            page.zoomTarget = "pdf"
                            page.adjustZoom(delta > 0 ? 1 : -1)
                            wheel.accepted = true
                        }
                    }
                }
                Button {
                    objectName: "pdfCompileButton"
                    anchors.centerIn: parent
                    text: qsTr("Save and compile")
                    enabled: !overleafClient.sourceBusy && !overleafClient.pdfBusy
                    visible: overleafClient.pdfPath.length === 0 && !page.compileInProgress
                    onClicked: page.saveAndCompile()
                }
                Rectangle {
                    objectName: "compileProgress"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.horizontalPageMargin,
                                    progressContents.implicitWidth + 4 * Theme.paddingLarge)
                    height: progressContents.implicitHeight + 4 * Theme.paddingLarge
                    radius: Theme.paddingMedium
                    color: page.editorBackground
                    visible: page.compileInProgress
                    Column {
                        id: progressContents
                        anchors.centerIn: parent
                        spacing: Theme.paddingMedium
                        BusyIndicator {
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: BusyIndicatorSize.Large
                            running: page.compileInProgress
                        }
                        Label {
                            objectName: "compileProgressLabel"
                            text: overleafClient.pdfBusy ? qsTr("Compiling…") : qsTr("Saving…")
                            color: Theme.highlightColor
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
                Label {
                    id: pdfLoadError
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    color: Theme.errorColor
                    visible: pdfLoader.item !== null && pdfLoader.item.status === PdfDocument.Error
                    text: qsTr("Unable to display the PDF.")
                }
                }
            }
    }
}
