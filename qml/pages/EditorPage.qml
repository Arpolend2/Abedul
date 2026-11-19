import QtQuick 2.5
import Sailfish.Silica 1.0
import ru.auroraos.amberpdf 1.0
import Abedul 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string projectId
    property string projectName
    readonly property bool offline: overleafClient.offline === true
    property string compileRoot: ""
    property string currentFile: ""
    property bool largeScreen: Screen.sizeCategory >= Screen.Large
    property var attachedPdfPage: null
    property bool hasStandaloneDraft: false
    property bool showingCachedFile: false
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
    property bool searchVisible: false
    property string searchMessage: ""
    property string zoomTarget: "editor"
    property string coverSnippet: ""
    property string coverSnippetFile: ""
    property bool coverHasEdits: false
    readonly property var latexSuggestion: {
        var input = editor._editor
        if (page.offline || !page.initialized || !editor.enabled || input.inputMethodComposing
                || input.selectionStart !== input.selectionEnd
                || (currentFile.length && !/\.(tex|sty|cls|bib)$/i.test(currentFile)))
            return null
        var text = input.text
        var cursor = input.cursorPosition
        var start = text.lastIndexOf("\n", cursor - 1) + 1
        var line = text.substring(start, cursor)
        var escaped = false
        for (var i = 0; i < line.length; ++i) {
            if (line.charAt(i) === "%" && !escaped)
                return null
            escaped = line.charAt(i) === "\\" ? !escaped : false
        }
        var snippetMatch = line.match(/^\s*\\([a-zA-Z]+)$/)
        if (snippetMatch && (cursor === text.length || text.charAt(cursor) === "\n")) {
            var snippets = {
                eqn: "eqnarray", eqnarray: "eqnarray",
                equ: "equation", equation: "equation",
                tab: "tabular", tabular: "tabular",
                fig: "figure", figure: "figure", table: "table",
                align: "align", gather: "gather", itemize: "itemize",
                enumerate: "enumerate", center: "center", quote: "quote",
                verbatim: "verbatim"
            }
            if (snippets.hasOwnProperty(snippetMatch[1])) {
                var name = snippets[snippetMatch[1]]
                return { completion: "", display: " -> \\begin{" + name + "}...",
                         snippet: true, environment: name, prefixLength: snippetMatch[1].length + 1 }
            }
        }
        var environment = line.match(/\\(?:begin|end)\{([a-zA-Z*]*)$/)
        if (environment) {
            var environmentStart = line.length - environment[0].length
            var previousSlashes = 0
            for (i = environmentStart - 1; i >= 0 && line.charAt(i) === "\\"; --i)
                ++previousSlashes
            if (previousSlashes % 2)
                return null
            var closingBrace = text.charAt(cursor) === "}"
            if (/[a-zA-Z*]/.test(text.charAt(cursor)) && cursor < text.length)
                return null
            var environments = [
                "eqnarray", "equation", "align", "gather", "multline",
                "document", "figure", "table", "tabular", "itemize",
                "enumerate", "description", "abstract", "verbatim",
                "array", "cases", "center", "flushleft", "flushright",
                "matrix", "pmatrix", "bmatrix", "vmatrix", "theorem",
                "proof", "quote", "quotation", "minipage", "tikzpicture"
            ]
            for (i = 0; i < environments.length; ++i) {
                if (environments[i].indexOf(environment[1]) === 0) {
                    var remaining = environments[i].substring(environment[1].length)
                    var completion = remaining + (closingBrace ? "" : "}")
                    if (completion.length)
                        return { completion: completion, display: completion, snippet: false }
                    return null
                }
            }
            return null
        }
        var match = line.match(/\\[a-zA-Z]*$/)
        if (!match)
            return null
        var commandStart = line.length - match[0].length
        var precedingSlashes = 0
        for (i = commandStart - 1; i >= 0 && line.charAt(i) === "\\"; --i)
            ++precedingSlashes
        if (precedingSlashes % 2)
            return null
        if (cursor < text.length && /[a-zA-Z]/.test(text.charAt(cursor)))
            return null
        var commands = [
            "begin{}", "end{}", "section{}", "subsection{}", "subsubsection{}",
            "chapter{}", "paragraph{}", "documentclass{}", "usepackage{}",
            "textbf{}", "textit{}", "emph{}", "cite{}", "ref{}", "label{}",
            "includegraphics{}", "frac{}{}", "sqrt{}", "item", "centering",
            "caption{}", "footnote{}", "newline", "alpha", "beta", "gamma", "delta",
            "epsilon", "varepsilon", "zeta", "eta", "theta", "vartheta", "iota",
            "kappa", "lambda", "mu", "nu", "xi", "pi", "varpi", "rho", "varrho",
            "sigma", "varsigma", "tau", "upsilon", "phi", "varphi", "chi", "psi",
            "omega", "Gamma", "Delta", "Theta", "Lambda", "Xi", "Pi", "Sigma",
            "Upsilon", "Phi", "Psi", "Omega", "sum", "prod", "int", "iint", "oint",
            "lim", "sin", "cos", "tan", "log", "ln", "exp", "min", "max", "sup",
            "inf", "infty", "partial", "nabla", "in", "notin", "subset", "subseteq",
            "supset", "supseteq", "cup", "cap", "times", "cdot", "pm", "mp", "div",
            "leq", "geq", "neq", "approx", "equiv", "sim", "propto",
            "rightarrow", "leftarrow", "leftrightarrow", "Rightarrow", "Leftarrow",
            "Leftrightarrow", "mapsto", "forall", "exists", "neg", "land", "lor",
            "mathbb{}", "mathbf{}", "mathcal{}", "mathrm{}", "mathit{}", "mathsf{}",
            "text{}", "texttt{}", "underline{}", "overline{}", "overbrace{}",
            "underbrace{}", "vec{}", "hat{}", "bar{}", "dot{}", "ddot{}", "tilde{}",
            "left", "right", "big", "Big", "bigg", "Bigg", "quad", "qquad",
            "hspace{}", "vspace{}", "dots", "ldots", "cdots", "vdots", "ddots",
            "limits", "displaystyle", "textstyle", "operatorname{}",
            "newcommand{}{}", "renewcommand{}{}", "providecommand{}{}",
            "newenvironment{}{}{}", "input{}", "include{}", "bibliography{}",
            "bibliographystyle{}", "pageref{}", "url{}", "href{}{}", "title{}",
            "author{}", "date{}", "maketitle", "tableofcontents", "appendix",
            "part{}", "subparagraph{}", "clearpage", "newpage",
            "hline", "cline{}", "multicolumn{}{}{}", "textwidth", "linewidth",
            "raggedright", "raggedleft", "today", "LaTeX", "TeX"
        ]
        var prefix = match[0].substring(1)
        for (i = 0; i < commands.length; ++i) {
            if (commands[i].split("{")[0] === prefix) {
                var exactSuffix = text.charAt(cursor) === "{" ? "" : commands[i].substring(prefix.length)
                return exactSuffix.length
                        ? { completion: exactSuffix, display: exactSuffix, snippet: false } : null
            }
        }
        for (i = 0; i < commands.length; ++i) {
            var name = commands[i].split("{")[0]
            if (name.indexOf(prefix) !== 0)
                continue
            var candidate = commands[i]
            if (text.charAt(cursor) === "{")
                candidate = name
            if (candidate.length > prefix.length) {
                var suffix = candidate.substring(prefix.length)
                return { completion: suffix, display: suffix, snippet: false }
            }
        }
        return null
    }
    readonly property string latexCompletion: latexSuggestion ? latexSuggestion.completion : ""
    readonly property bool completionAvailable: editor._editor.activeFocus && !!latexSuggestion
    property bool completionTabHintActive: false
    property bool completionWasAvailable: false
    onCompletionAvailableChanged: completionHintUpdate.restart()
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
    forwardNavigation: !panes.sideBySide && !!attachedPdfPage && !overleafClient.sourceBusy

    function updatePdfNavigation() {
        if (typeof pageStack === "undefined")
            return
        if (pageStack.busy) {
            pdfNavigationTimer.restart()
            return
        }
        if (attachedPdfPage && pageStack.currentPage === attachedPdfPage && page.largeScreen
                && attachedPdfPage.width > attachedPdfPage.height) {
            pageStack.navigateBack(PageStackAction.Immediate)
            pdfNavigationTimer.restart()
            return
        }
        if (page.status !== PageStatus.Active || pageStack.currentPage !== page)
            return
        if (panes.sideBySide && attachedPdfPage) {
            var oldPage = attachedPdfPage
            attachedPdfPage = null
            pageStack.popAttached(oldPage, PageStackAction.Immediate)
        } else if (!panes.sideBySide && !attachedPdfPage) {
            attachedPdfPage = pageStack.pushAttached(pdfPageComponent)
        }
    }

    Timer {
        id: pdfNavigationTimer
        interval: 100
        onTriggered: page.updatePdfNavigation()
    }

    Component {
        id: pdfPageComponent
        Page {
            id: separatePdfPage
            objectName: "separatePdfPage"
            allowedOrientations: Orientation.All
            property alias contentHost: pdfPageHost
            onStatusChanged: if (status === PageStatus.Active) pdfNavigationTimer.restart()
            onWidthChanged: if (status === PageStatus.Active) pdfNavigationTimer.restart()
            onHeightChanged: if (status === PageStatus.Active) pdfNavigationTimer.restart()
            Component.onDestruction: if (page.attachedPdfPage === separatePdfPage) page.attachedPdfPage = null
            Shortcut {
                sequence: StandardKey.Save
                enabled: separatePdfPage.status === PageStatus.Active
                autoRepeat: false
                onActivated: page.saveAndCompile()
            }
            SilicaFlickable {
                id: pdfPageFlick
                anchors.fill: parent
                contentHeight: height
                PullDownMenu {
                    MenuItem {
                        text: qsTr("Open PDF in another app")
                        enabled: overleafClient.pdfPath.length > 0 && !overleafClient.pdfBusy
                        onClicked: externalPdf.open()
                    }
                    MenuItem {
                        text: qsTr("Save and compile")
                        enabled: !page.offline && !overleafClient.sourceBusy && !overleafClient.pdfBusy
                        onClicked: page.saveAndCompile()
                    }
                }
                Item {
                    id: pdfPageHost
                    width: parent.width
                    height: parent.height
                }
            }
        }
    }

    function saveDraft() {
        if (offline)
            return
        if (dirty) {
            var saved = currentFile.length
                ? draftStore.saveFileWithBase(overleafClient.server, projectId, currentFile,
                                              editor.text, baseText, baseKnown)
                : draftStore.save(overleafClient.server, projectId, editor.text)
            if (saved)
                dirty = false
        }
    }

    function findText(backwards) {
        var needle = searchInput.text
        if (!needle.length)
            return false
        var input = editor._editor
        var haystack = input.text
        var start = backwards ? input.selectionStart - 1 : input.selectionEnd
        var index = backwards ? (start < 0 ? -1 : haystack.lastIndexOf(needle, start))
                              : haystack.indexOf(needle, start)
        if (index < 0)
            index = backwards ? haystack.lastIndexOf(needle) : haystack.indexOf(needle)
        if (index < 0) {
            searchMessage = qsTr("No matches found.")
            return false
        }
        searchMessage = ""
        input.select(index, index + needle.length)
        var rect = input.positionToRectangle(index)
        var point = input.mapToItem(codePane.contentItem, rect.x, rect.y)
        codePane.contentY = Math.max(0, Math.min(codePane.contentHeight - codePane.height,
                                                point.y - (codePane.height + searchPanel.height) / 2))
        return true
    }

    function replaceMatch() {
        if (offline)
            return false
        var input = editor._editor
        var needle = searchInput.text
        if (!needle.length || !replaceInput.text.length || !editor.enabled || input.inputMethodComposing)
            return
        if (input.text.substring(input.selectionStart, input.selectionEnd) !== needle && !findText(false))
            return
        var start = input.selectionStart
        if (!completionKeys.replace(start, input.selectionEnd, replaceInput.text)) {
            searchMessage = qsTr("Could not replace the match.")
            return
        }
        input.cursorPosition = start + replaceInput.text.length
        findText(false)
    }

    function replaceAllMatches() {
        if (offline)
            return false
        var input = editor._editor
        var needle = searchInput.text
        if (!needle.length || !replaceInput.text.length || !editor.enabled || input.inputMethodComposing)
            return
        var parts = input.text.split(needle)
        if (parts.length === 1) {
            searchMessage = qsTr("No matches found.")
            return
        }
        var position = input.cursorPosition
        var replacement = parts.join(replaceInput.text)
        if (!completionKeys.replace(0, input.length, replacement)) {
            searchMessage = qsTr("Could not replace the match.")
            return
        }
        input.cursorPosition = Math.min(position, input.length)
        searchMessage = qsTr("Replaced %1 matches.").arg(parts.length - 1)
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
            if (typeof overleafClient.probeProjectComments === "function")
                overleafClient.probeProjectComments(projectId)
        }
    }

    function saveAndCompile() {
        if (offline)
            return
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

    function saveBeforeFileOperation() {
        if (!currentFile.length || overleafClient.sourceBusy)
            return false
        saveDraft()
        if (dirty)
            return false
        overleafClient.saveProjectSource(projectId, currentFile, baseText, baseKnown, editor.text)
        return true
    }

    function finishFileOperationSave(path, text) {
        if (path !== currentFile || editor.text !== text) {
            saveDraft()
            return false
        }
        baseText = text
        baseKnown = true
        if (!draftStore.saveFileWithBase(overleafClient.server, projectId, path, text, text, true)) {
            dirty = true
            return false
        }
        dirty = false
        return true
    }

    function fileOperationFinished(action, path, type, newPath) {
        if (action !== "rename" && action !== "delete")
            return
        var includes = function(value) {
            return value === path || (type === "folder" && value.indexOf(path + "/") === 0)
        }
        var mapped = function(value) {
            return action === "rename" ? newPath + value.substring(path.length) : ""
        }
        var error = ""
        if (currentFile.length && includes(currentFile)) {
            currentFile = mapped(currentFile)
            if (currentFile.length) {
                if (!draftStore.saveFileWithBase(overleafClient.server, projectId, currentFile,
                                                 editor.text, baseText, baseKnown))
                    error = draftStore.error
            } else {
                initialized = false
                editor.text = ""
                showingCachedFile = false
                baseText = ""
                baseKnown = false
                dirty = false
                initialized = true
            }
            if (!draftStore.saveLastFile(overleafClient.server, projectId, currentFile) && !error.length)
                error = draftStore.error
            updateCoverSnippet()
        }
        if (compileRoot.length && includes(compileRoot)) {
            compileRoot = mapped(compileRoot)
            if (!draftStore.saveCompileRoot(overleafClient.server, projectId, compileRoot) && !error.length)
                error = draftStore.error
        }
        if (error.length)
            actionError = error
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
        if (offline) {
            overleafClient.openProjectSource(projectId, path)
            return true
        }
        saveDraft()
        if (dirty || overleafClient.sourceBusy)
            return false
        if (!draftStore.loadFile(overleafClient.server, projectId, path)) {
            actionError = draftStore.error
            return false
        }
        if (draftStore.exists) {
            initialized = false
            currentFile = path
            editor.text = draftStore.text
            baseText = draftStore.baseText
            baseKnown = draftStore.baseKnown
            initialized = true
            showingCachedFile = true
        }
        overleafClient.openProjectSource(projectId, path)
        return true
    }

    function syncAllowed() {
        if (offline) {
            actionError = qsTr("SyncTeX is not available offline.")
            return false
        }
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
        if (attachedPdfPage && pageStack.currentPage === attachedPdfPage)
            pageStack.navigateBack()
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
        if (draftStore.error.length) {
            actionError = draftStore.error
            return
        }
        if (lastFile.length)
            openProjectFile(lastFile)
        else if (!hasStandaloneDraft) {
            if (compileRoot.length)
                openProjectFile(compileRoot)
            else
                overleafClient.openMainProjectSource(projectId)
        }
    }

    function initializeEditor() {
        if (started)
            return
        started = true
        if (typeof overleafClient.restoreProjectPdf === "function")
            overleafClient.restoreProjectPdf(projectId)
        if (offline) {
            initialized = true
            overleafClient.openMainProjectSource(projectId)
            return
        }
        compileRoot = draftStore.loadCompileRoot(overleafClient.server, projectId)
        if (draftStore.error.length)
            return
        if (draftStore.load(overleafClient.server, projectId)) {
            editor.text = draftStore.text
            hasStandaloneDraft = draftStore.exists
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
            pdfNavigationTimer.restart()
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
        ignoreUnknownSignals: true
        onReconnected: {
            if (!page.started)
                return
            if (page.currentFile.length)
                overleafClient.openProjectSource(page.projectId, page.currentFile)
            else
                overleafClient.openMainProjectSource(page.projectId)
        }
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
            if (page.offline) {
                page.initialized = false
                page.currentFile = overleafClient.sourcePath
                editor.text = overleafClient.sourceText
                page.baseText = editor.text
                page.baseKnown = true
                page.showingCachedFile = true
                page.initialized = true
                return
            }
            if (coverSnippetTimer.running) {
                coverSnippetTimer.stop()
                page.updateCoverSnippet()
            }
            if (!draftStore.loadFile(overleafClient.server, page.projectId, overleafClient.sourcePath))
                return
            page.initialized = false
            page.currentFile = overleafClient.sourcePath
            page.showingCachedFile = false
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
            if (!useDraft && !draftStore.saveFileWithBase(overleafClient.server, page.projectId,
                    page.currentFile, editor.text, editor.text, true))
                page.actionError = draftStore.error
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
            if (pdfLoader.item && positions.length) {
                pdfLoader.item.showSyncPosition(positions[0])
                if (!panes.sideBySide && page.attachedPdfPage && page.status === PageStatus.Active)
                    pageStack.navigateForward()
            }
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
        objectName: "editorSearchShortcut"
        sequence: "Ctrl+F"
        enabled: page.status === PageStatus.Active
        onActivated: {
            page.searchVisible = true
            searchInput.forceActiveFocus()
        }
    }
    Shortcut {
        objectName: "searchCloseShortcut"
        sequence: "Escape"
        enabled: page.status === PageStatus.Active && page.searchVisible
                 && !searchInput.inputMethodComposing && !replaceInput.inputMethodComposing
                 && !editor.inputMethodComposing
        autoRepeat: false
        onActivated: page.searchVisible = false
    }
    Shortcut {
        objectName: "searchPreviousShortcut"
        sequence: "Up"
        enabled: page.status === PageStatus.Active && page.searchVisible && editor.enabled
                 && searchInput.text.length > 0
                 && (searchInput._editor.activeFocus || replaceInput._editor.activeFocus)
                 && !searchInput.inputMethodComposing && !replaceInput.inputMethodComposing
        onActivated: page.findText(true)
    }
    Shortcut {
        objectName: "searchNextShortcut"
        sequence: "Down"
        enabled: page.status === PageStatus.Active && page.searchVisible && editor.enabled
                 && searchInput.text.length > 0
                 && (searchInput._editor.activeFocus || replaceInput._editor.activeFocus)
                 && !searchInput.inputMethodComposing && !replaceInput.inputMethodComposing
        onActivated: page.findText(false)
    }
    Shortcut {
        objectName: "searchReplaceShortcut"
        sequence: "Tab"
        enabled: page.status === PageStatus.Active && page.searchVisible && !page.offline && editor.enabled
                 && searchInput.text.length > 0 && replaceInput.text.length > 0
                 && (searchInput._editor.activeFocus || replaceInput._editor.activeFocus)
                 && !searchInput.inputMethodComposing && !replaceInput.inputMethodComposing
                 && !editor.inputMethodComposing
        autoRepeat: false
        onActivated: page.replaceMatch()
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
        id: completionHintUpdate
        interval: 0
        onTriggered: {
            if (page.completionAvailable && !page.completionWasAvailable) {
                page.completionTabHintActive = overleafClient.completionHintCount < 5
                if (page.completionTabHintActive)
                    overleafClient.recordCompletionHintShown()
            } else if (!page.completionAvailable) {
                page.completionTabHintActive = false
            }
            page.completionWasAvailable = page.completionAvailable
        }
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
                objectName: "projectSharing"
                visible: !page.offline
                text: qsTr("Sharing")
                onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                    "projectId": page.projectId, "mode": "share"
                })
            }
            MenuItem {
                objectName: "projectHistory"
                visible: !page.offline
                text: qsTr("History")
                onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                    "projectId": page.projectId, "mode": "history"
                })
            }
            MenuItem {
                objectName: "projectComments"
                visible: overleafClient.commentsAvailable
                text: qsTr("Comments")
                onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                    "projectId": page.projectId, "mode": "comments"
                })
            }
            MenuItem {
                objectName: "projectChat"
                visible: !page.offline
                text: qsTr("Chat")
                onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                    "projectId": page.projectId, "mode": "chat"
                })
            }
            MenuItem {
                objectName: "editorSearch"
                text: qsTr("Find and replace")
                onClicked: {
                    page.searchVisible = true
                    searchInput.forceActiveFocus()
                }
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
                enabled: !page.offline && !overleafClient.archiveBusy
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
                enabled: !page.offline && !overleafClient.sourceBusy && !overleafClient.pdfBusy
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
            property bool sideBySide: page.largeScreen && width > height
            onSideBySideChanged: pdfNavigationTimer.restart()

            SilicaFlickable {
                id: codePane
                objectName: "editorCodePane"
                x: panes.sideBySide && overleafClient.pdfOnLeft ? panes.width / 2 : 0
                width: panes.sideBySide ? panes.width / 2 : panes.width
                height: panes.height
                contentHeight: codeColumn.height
                clip: true
                Column {
                    id: codeColumn
                    width: parent.width
                    opacity: page.openingSource ? 0.3 : 1
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        color: Theme.secondaryColor
                        visible: page.showingCachedFile
                        text: overleafClient.sourceBusy ? qsTr("Showing a local copy while updating…")
                                                       : qsTr("Showing a local copy; it may be out of date.")
                    }
                    Label {
                        objectName: "sourceStatus"
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        visible: page.currentFile.length > 0 && (!page.baseKnown || editor.text !== page.baseText)
                        text: qsTr("Local changes to %1 are not saved to Overleaf.").arg(page.currentFile)
                    }
                    Label {
                        objectName: "editorError"
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        color: Theme.errorColor
                        visible: draftStore.error.length > 0 || overleafClient.sourceError.length > 0
                                 || (page.offline && overleafClient.error && overleafClient.error.length > 0)
                                 || page.actionError.length > 0 || externalPdf.error.length > 0
                                 || (overleafClient.archiveProject === page.projectId
                                     && overleafClient.archiveError.length > 0)
                        text: draftStore.error.length ? draftStore.error
                              : overleafClient.sourceError.length ? overleafClient.sourceError
                              : page.offline && overleafClient.error && overleafClient.error.length ? overleafClient.error
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
                        Binding { target: editor._editor; property: "persistentSelection"; value: true }
                        Binding { target: editor._editor; property: "readOnly"; value: page.offline }
                        onTextChanged: {
                            if (page.initialized) {
                                page.dirty = true
                                page.coverHasEdits = true
                                coverSnippetTimer.restart()
                                saveTimer.restart()
                            }
                        }
                        Text {
                            id: completionHint
                            objectName: "latexCompletionHint"
                            parent: editor._editor
                            x: editor._editor.cursorRectangle.x
                            y: editor._editor.cursorRectangle.y
                            width: Math.max(0, parent.width - x)
                            clip: true
                            visible: editor._editor.activeFocus && !!page.latexSuggestion
                                     && (editor._editor.cursorPosition === editor._editor.length
                                         || editor._editor.text.charAt(editor._editor.cursorPosition) === "\n")
                            text: page.latexSuggestion
                                  ? page.latexSuggestion.display
                                    + (page.completionTabHintActive ? "  <- " + qsTr("Press Tab to accept") : "")
                                  : ""
                            font: editor._editor.font
                            color: editor.color
                            opacity: 0.65
                            z: 1
                        }
                        Rectangle {
                            id: completionPopup
                            objectName: "latexCompletionPopup"
                            parent: codePane
                            property point cursorPoint: {
                                var scroll = codePane.contentY
                                var cursor = editor._editor.cursorRectangle
                                return editor._editor.mapToItem(codePane, cursor.x, cursor.y)
                            }
                            x: Math.max(0, Math.min(codePane.width - width, cursorPoint.x))
                            y: cursorPoint.y + editor._editor.cursorRectangle.height + height <= codePane.height
                               ? Math.max(0, cursorPoint.y + editor._editor.cursorRectangle.height)
                               : Math.max(0, cursorPoint.y - height)
                            width: Math.min(codePane.width, popupText.implicitWidth + Theme.paddingMedium * 2)
                            height: popupText.height + Theme.paddingSmall * 2
                            z: 3
                            color: page.editorBackground
                            border.color: editor.color
                            visible: page.completionAvailable && !completionHint.visible
                            Text {
                                id: popupText
                                objectName: "latexCompletionPopupText"
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width - Theme.paddingMedium * 2)
                                wrapMode: Text.Wrap
                                text: page.latexSuggestion
                                      ? page.latexSuggestion.display
                                        + (page.completionTabHintActive ? "  <- " + qsTr("Press Tab to accept") : "")
                                      : ""
                                font: editor._editor.font
                                color: editor.color
                            }
                        }
                        CompletionKeyFilter {
                            id: completionKeys
                            objectName: "latexCompletionKeyFilter"
                            target: editor._editor
                            enabled: !!page.latexSuggestion
                                     && (page.latexSuggestion.snippet || page.latexCompletion.length > 0)
                            onAccepted: {
                                var text = page.latexCompletion
                                var position = editor._editor.cursorPosition
                                if (page.latexSuggestion.snippet) {
                                    var suggestion = page.latexSuggestion
                                    var lineStart = editor._editor.text.lastIndexOf("\n", position - 1) + 1
                                    var start = position - suggestion.prefixLength
                                    var indent = editor._editor.text.substring(lineStart, start)
                                    var opening = "\\begin{" + suggestion.environment + "}"
                                            + (suggestion.environment === "tabular" ? "{c}" : "")
                                            + "\n" + indent + "    "
                                    if (completionKeys.replace(start, position,
                                                               opening + "\n" + indent
                                                               + "\\end{" + suggestion.environment + "}"))
                                        editor._editor.cursorPosition = start + opening.length
                                    else
                                        page.actionError = qsTr("Could not insert the LaTeX completion.")
                                    return
                                }
                                editor._editor.insert(position, text)
                                var brace = text.indexOf("{")
                                editor._editor.cursorPosition = brace >= 0
                                        ? position + brace + 1
                                        : position + text.length
                                          + (editor._editor.text.charAt(position + text.length) === "}" ? 1 : 0)
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

            Rectangle {
                id: searchPanel
                objectName: "editorSearchPanel"
                x: codePane.x
                width: codePane.width
                height: visible ? Math.min(searchContents.height, codePane.height * 0.7) : 0
                color: page.editorBackground
                border.color: Theme.secondaryColor
                visible: page.searchVisible
                z: 4
                SilicaFlickable {
                    anchors.fill: parent
                    contentHeight: searchContents.height
                    clip: true
                    Column {
                        id: searchContents
                        width: parent.width
                        TextField {
                            id: searchInput
                            objectName: "searchInput"
                            width: parent.width
                            label: qsTr("Find")
                            placeholderText: qsTr("Find")
                            onTextChanged: page.searchMessage = ""
                            EnterKey.onClicked: page.findText(false)
                        }
                        TextField {
                            id: replaceInput
                            objectName: "replaceInput"
                            width: parent.width
                            label: qsTr("Replace with")
                            placeholderText: qsTr("Replace with")
                        }
                        Label {
                            x: Theme.horizontalPageMargin
                            width: parent.width - 2 * x
                            wrapMode: Text.Wrap
                            color: Theme.secondaryColor
                            text: page.searchMessage.length ? page.searchMessage
                                  : qsTr("Search is case-sensitive and applies to the current file.")
                        }
                        SilicaFlickable {
                            id: searchActions
                            objectName: "searchActions"
                            width: parent.width
                            height: Theme.itemSizeSmall
                            contentWidth: searchActionsRow.width
                            contentHeight: height
                            flickableDirection: Flickable.HorizontalFlick
                            clip: true
                            Row {
                                id: searchActionsRow
                                objectName: "searchActionsRow"
                                spacing: Theme.paddingSmall
                                readonly property real minimumWidth: searchPrevious.width + searchNext.width
                                                                     + replaceButton.implicitWidth
                                                                     + replaceAllButton.implicitWidth
                                                                     + searchClose.width
                                                                     + 4 * spacing
                                width: Math.max(searchActions.width, minimumWidth)
                                height: searchActions.height
                                Button {
                                    id: searchPrevious
                                    objectName: "searchPrevious"
                                    anchors.verticalCenter: parent.verticalCenter
                                    preferredWidth: Theme.buttonWidthTiny
                                    width: implicitHeight
                                    icon.source: "image://theme/icon-m-up-alt"
                                    Accessible.name: qsTr("Previous")
                                    Accessible.role: Accessible.Button
                                    enabled: editor.enabled && searchInput.text.length > 0
                                    onClicked: page.findText(true)
                                }
                                Button {
                                    id: searchNext
                                    objectName: "searchNext"
                                    anchors.verticalCenter: parent.verticalCenter
                                    preferredWidth: Theme.buttonWidthTiny
                                    width: implicitHeight
                                    icon.source: "image://theme/icon-m-down-alt"
                                    Accessible.name: qsTr("Next")
                                    Accessible.role: Accessible.Button
                                    enabled: editor.enabled && searchInput.text.length > 0
                                    onClicked: page.findText(false)
                                }
                                Button {
                                    id: replaceButton
                                    objectName: "searchReplace"
                                    anchors.verticalCenter: parent.verticalCenter
                                    preferredWidth: Theme.buttonWidthExtraSmall
                                    width: implicitWidth + (searchActionsRow.width - searchActionsRow.minimumWidth) / 2
                                    text: qsTr("Replace")
                                    enabled: !page.offline && editor.enabled && searchInput.text.length > 0 && replaceInput.text.length > 0
                                    onClicked: page.replaceMatch()
                                }
                                Button {
                                    id: replaceAllButton
                                    objectName: "searchReplaceAll"
                                    anchors.verticalCenter: parent.verticalCenter
                                    preferredWidth: Theme.buttonWidthExtraSmall
                                    width: implicitWidth + (searchActionsRow.width - searchActionsRow.minimumWidth) / 2
                                    text: qsTr("Replace all")
                                    enabled: !page.offline && editor.enabled && searchInput.text.length > 0 && replaceInput.text.length > 0
                                    onClicked: page.replaceAllMatches()
                                }
                                Button {
                                    id: searchClose
                                    objectName: "searchClose"
                                    anchors.verticalCenter: parent.verticalCenter
                                    preferredWidth: Theme.buttonWidthTiny
                                    width: implicitHeight
                                    icon.source: "image://theme/icon-m-cancel"
                                    Accessible.name: qsTr("Close")
                                    Accessible.role: Accessible.Button
                                    onClicked: page.searchVisible = false
                                }
                            }
                            HorizontalScrollDecorator {}
                        }
                    }
                    VerticalScrollDecorator {}
                }
            }

            Item {
                id: pdfPane
                objectName: "editorPdfPane"
                parent: !panes.sideBySide && page.attachedPdfPage ? page.attachedPdfPage.contentHost : panes
                visible: panes.sideBySide || !!page.attachedPdfPage
                x: panes.sideBySide && !overleafClient.pdfOnLeft ? codePane.width : 0
                y: 0
                width: panes.sideBySide ? panes.width - codePane.width : parent.width
                height: parent.height
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
                        text: qsTr("Pull down from the top or press Ctrl+S to save and compile again.")
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
                    enabled: !page.offline && !overleafClient.sourceBusy && !overleafClient.pdfBusy
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
