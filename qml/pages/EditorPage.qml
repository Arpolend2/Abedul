import QtQuick 2.5
import Sailfish.Silica 1.0
import ru.omp.amberpdf 1.0
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
    Component.onCompleted: if (!deferOpen || status === PageStatus.Active) initializeEditor()
    onStatusChanged: if (deferOpen && status === PageStatus.Active) initializeEditor()
    Component.onDestruction: {
        if (started) {
            saveDraft()
            overleafClient.closeProjectPdf()
            overleafClient.closeProjectSource()
        }
    }

    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state !== Qt.ApplicationActive)
                page.saveDraft()
        }
    }
    Connections {
        target: overleafClient
        onSourceErrorChanged: {
            if (overleafClient.sourceError.length && !overleafClient.sourceBusy)
                page.pendingCompileRoot = ""
        }
        onPdfPathChanged: {
            if (!overleafClient.pdfPath.length) {
                compileHintTimer.stop()
            } else if (!page.compileHintShown) {
                page.compileHintShown = true
                compileHintTimer.restart()
            }
        }
        onSourceTextChanged: {
            if (!page.started || !overleafClient.sourcePath.length)
                return
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
            if (/\.tex$/i.test(page.currentFile)) {
                page.compileRoot = page.currentFile
                if (!draftStore.saveCompileRoot(overleafClient.server, page.projectId, page.compileRoot))
                    page.actionError = draftStore.error
            }
            if (!draftStore.saveLastFile(overleafClient.server, page.projectId, page.currentFile))
                page.actionError = draftStore.error
            page.initialized = true
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
            overleafClient.compileProjectPdf(page.projectId, root)
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
                text: qsTr("Project files")
                onClicked: pageStack.push(Qt.resolvedUrl("ProjectFilesPage.qml"), {
                    "projectId": page.projectId,
                    "projectName": page.projectName,
                    "editorPage": page
                })
            }
            MenuItem {
                objectName: "editorSettings"
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
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
                        visible: draftStore.error.length > 0 || overleafClient.sourceError.length > 0 || page.actionError.length > 0
                        text: draftStore.error.length ? draftStore.error
                              : overleafClient.sourceError.length ? overleafClient.sourceError : page.actionError
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
                        sourceComponent: PdfView {
                            objectName: "pdfPreview"
                            documentProvider: pdfDocument
                            pdfPath: overleafClient.pdfPath
                            fitToPage: true
                            onClicked: page.zoomTarget = "pdf"
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
