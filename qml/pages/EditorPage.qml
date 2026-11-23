import QtQuick 2.5
import Sailfish.Silica 1.0
import Aurora.Controls 1.0
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
    readonly property bool wideTablet: largeScreen && width > height
    readonly property bool editorActionsInMore: !wideTablet
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
    readonly property string keyboardShortcutHintText: {
        var shortcuts = [
            qsTr("Ctrl+S: Save and compile"),
            qsTr("Ctrl+Y: Redo"),
            qsTr("Ctrl+F: Find and replace"),
            qsTr("Ctrl+W: Solve the equation with WolframAlpha"),
            qsTr("Ctrl+R: Translate the selection or word with Reverso"),
            qsTr("Ctrl++ / Ctrl+-: Zoom the editor or PDF"),
            qsTr("Ctrl+mouse wheel: Zoom under the pointer"),
            qsTr("Tab: Accept a completion or replace a search match"),
            qsTr("Up / Down: Previous / next search result"),
            qsTr("Escape: Close search")
        ]
        if (aiEnabled) {
            shortcuts.push(qsTr("Ctrl+I: Request an AI completion"))
            shortcuts.push(qsTr("Ctrl+Shift+I: Open AI generation"))
        }
        return shortcuts.join("\n")
    }
    property string zoomTarget: "editor"
    property string wheelTarget: "editor"
    property string coverSnippet: ""
    property string coverSnippetFile: ""
    property bool coverHasEdits: false
    readonly property bool coverSideBySide: panes.sideBySide
    readonly property var latexSuggestion: {
        var input = editor._editor
        if (page.aiSuggestion.length || page.offline || !page.initialized || !editor.enabled || input.inputMethodComposing
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
    readonly property bool aiEnabled: aiClient.aiEnabled
    property string aiSuggestion: ""
    property string aiCompletionOriginal: ""
    property string aiCompletionError: ""
    property string aiClientOtherError: ""
    property int aiCompletionStart: -1
    property int aiCompletionEnd: -1
    property string aiRequestText: ""
    property string aiRequestFile: ""
    property int aiRequestPosition: -1
    property bool aiCompletionPending: false
    onAiEnabledChanged: clearAiCompletion()
    onOfflineChanged: if (page.offline) clearAiCompletion()
    OpenRouterClient {
        id: aiClient
        objectName: "editorAiClient"
        onResultReady: {
            if (!page.aiCompletionPending)
                return
            page.aiCompletionPending = false
            if (page.aiCompletionCurrent())
                page.receiveAiCompletion(text)
        }
        onErrorChanged: {
            if (page.aiCompletionPending && error.length) {
                page.aiCompletionError = error
                page.aiCompletionPending = false
            } else {
                page.aiClientOtherError = error
            }
        }
    }
    OpenRouterClient {
        id: aiGeneratorClient
        objectName: "aiGeneratorClient"
    }
    property bool wolframVisible: false
    property bool wolframInputVisible: false
    property bool wolframInputSubmitted: false
    readonly property bool utilityPopupOpen: wolframVisible || wolframInputVisible
                                             || sciHubPopup.visible || reversoPopup.showing
                                             || aiGeneratorPopup.showing
    function dismissUtilityPopups() {
        closeWolframResult()
        closeWolframInput()
        sciHubPopup.visible = false
        reversoPopup.close()
        aiGeneratorPopup.close()
        if (page.status === PageStatus.Active && editor.enabled)
            editor._editor.forceActiveFocus()
    }
    WolframAlphaClient {
        id: wolframClient
        objectName: "editorWolframAlphaClient"
    }
    function closeWolframResult() {
        if (page.wolframVisible)
            wolframClient.cancel()
        page.wolframVisible = false
    }
    function requestWolframResult() {
        if (page.status !== PageStatus.Active || !page.initialized || !editor.enabled
                || !editor._editor.activeFocus || editor._editor.inputMethodComposing || wolframClient.busy)
            return
        var equation = wolframClient.equationAtCursor(editor.text, editor._editor.cursorPosition)
        if (!equation.length)
            return
        page.closeWolframInput()
        sciHubPopup.visible = false
        reversoPopup.close()
        aiGeneratorPopup.close()
        page.clearAiCompletion()
        wolframClient.reload()
        page.wolframVisible = true
        if (!wolframClient.error.length)
            wolframClient.request(equation)
    }
    function openWolframInput() {
        if (page.wolframInputVisible) {
            wolframInput.forceActiveFocus()
            return
        }
        page.closeWolframResult()
        sciHubPopup.visible = false
        reversoPopup.close()
        aiGeneratorPopup.close()
        page.clearAiCompletion()
        page.wolframInputSubmitted = false
        wolframInput.text = ""
        page.wolframInputVisible = true
        wolframInput.forceActiveFocus()
    }
    function closeWolframInput() {
        if (page.wolframInputVisible)
            wolframClient.cancel()
        page.wolframInputVisible = false
    }
    function submitWolframInput() {
        if (!page.wolframInputVisible || wolframClient.busy)
            return
        wolframClient.reload()
        page.wolframInputSubmitted = true
        if (!wolframClient.error.length)
            wolframClient.request(wolframInput.text)
    }
    function openReverso() {
        page.closeWolframResult()
        page.closeWolframInput()
        sciHubPopup.visible = false
        aiGeneratorPopup.close()
        page.clearAiCompletion()
        reversoPopup.open()
    }
    function translateReversoWord() {
        if (page.status !== PageStatus.Active || !page.initialized || !editor.enabled
                || !editor._editor.activeFocus || editor._editor.inputMethodComposing)
            return
        var text = reversoPopup.client.plainTextAtCursor(editor.text, editor._editor.cursorPosition,
                                                       editor._editor.selectionStart, editor._editor.selectionEnd)
        if (!text.length)
            return
        page.closeWolframResult()
        page.closeWolframInput()
        sciHubPopup.visible = false
        aiGeneratorPopup.close()
        page.clearAiCompletion()
        reversoPopup.translateText(text)
    }
    onCurrentFileChanged: {
        closeWolframResult()
        aiGeneratorPopup.close()
        if (reversoPopup.showing && reversoPopup.reverseDirection)
            reversoPopup.close()
    }
    function aiCompletionCurrent() {
        return page.aiEnabled && aiClient.authenticated && !page.offline && page.initialized
                && page.status === PageStatus.Active
                && editor.enabled && editor._editor.activeFocus && !editor._editor.inputMethodComposing
                && editor._editor.selectionStart === editor._editor.selectionEnd
                && page.currentFile === page.aiRequestFile && editor.text === page.aiRequestText
                && editor._editor.cursorPosition === page.aiRequestPosition
    }
    function aiDocumentText() { return editor.text }
    function aiSelectionStart() { return editor._editor.selectionStart }
    function aiSelectionEnd() { return editor._editor.selectionEnd }
    function aiCursorPosition() { return editor._editor.cursorPosition }
    function openAiGenerator() {
        if (!page.aiEnabled)
            return
        clearAiCompletion()
        closeWolframResult()
        closeWolframInput()
        sciHubPopup.visible = false
        reversoPopup.close()
        if (!aiGeneratorPopup.showing)
            aiGeneratorClient.reload()
        aiGeneratorPopup.open()
    }
    function openSciHubSearch() {
        var url = overleafClient.sciHubSearchUrl(sciHubInput.text)
        if (!url || !url.toString().length || !Qt.openUrlExternally(url))
            sciHubError.text = qsTr("Could not open the address. Check the Sci-Hub address in Settings.")
        else
            sciHubPopup.visible = false
    }
    function applyAiGeneration(file, original, start, end, result) {
        if (page.offline || page.currentFile !== file || editor.text !== original
                || !editor.enabled || !completionKeys.replace(start, end, result)) {
            page.actionError = qsTr("The document changed or cannot be edited. Generate the text again.")
            return false
        }
        return true
    }
    function clearAiCompletion() {
        page.aiSuggestion = ""
        page.aiCompletionOriginal = ""
        page.aiCompletionError = ""
        page.aiCompletionStart = -1
        page.aiCompletionEnd = -1
        if (page.aiCompletionPending) {
            page.aiCompletionPending = false
            aiClient.cancel()
        }
    }
    function receiveAiCompletion(text) {
        var response
        try {
            var json = text.trim()
            if (/^```(?:json)?\s*[\r\n]/i.test(json) && /```$/.test(json))
                json = json.replace(/^```(?:json)?\s*[\r\n]/i, "").replace(/```\s*$/, "")
            response = JSON.parse(json)
        } catch (e) {
            page.aiCompletionError = qsTr("The AI returned an invalid suggestion. Try again.")
            return
        }
        if (!response || (response.action !== "insert" && response.action !== "replace")
                || typeof response.new_text !== "string" || !response.new_text.trim().length
                || /[\r\n]/.test(response.new_text)) {
            page.aiCompletionError = qsTr("The AI returned an invalid suggestion. Try again.")
            return
        }
        var start = page.aiRequestPosition
        var end = start
        if (response.action === "replace") {
            if (typeof response.old_text !== "string" || !response.old_text.length
                    || /[\r\n]/.test(response.old_text)) {
                page.aiCompletionError = qsTr("The AI returned an invalid suggestion. Try again.")
                return
            }
            var old = response.old_text
            var candidate = page.aiRequestText.indexOf(old, Math.max(0, page.aiRequestPosition - old.length))
            var found = -1
            while (candidate >= 0 && candidate <= page.aiRequestPosition) {
                if (candidate + old.length >= page.aiRequestPosition) {
                    if (found >= 0) {
                        page.aiCompletionError = qsTr("The AI could not uniquely locate text at the cursor. Try again.")
                        return
                    }
                    found = candidate
                }
                candidate = page.aiRequestText.indexOf(old, candidate + 1)
            }
            if (found < 0) {
                page.aiCompletionError = qsTr("The AI could not uniquely locate text at the cursor. Try again.")
                return
            }
            start = found
            end = found + old.length
            page.aiCompletionOriginal = old
        }
        page.aiCompletionStart = start
        page.aiCompletionEnd = end
        page.aiSuggestion = response.new_text
    }
    function aiCompletionContext(text, position) {
        return {
            before: text.substring(Math.max(0, position - 512), position),
            after: text.substring(position, Math.min(text.length, position + 128))
        }
    }
    function requestAiCompletion() {
        if (!page.aiEnabled || page.offline || !page.initialized || !editor.enabled || aiClient.busy
                || page.status !== PageStatus.Active || !editor._editor.activeFocus
                || editor._editor.inputMethodComposing
                || editor._editor.selectionStart !== editor._editor.selectionEnd)
            return
        page.clearAiCompletion()
        page.aiRequestText = editor.text
        page.aiRequestFile = page.currentFile
        page.aiRequestPosition = editor._editor.cursorPosition
        page.aiCompletionPending = true
        var context = page.aiCompletionContext(editor.text, page.aiRequestPosition)
        aiClient.requestCompletion(context.before, context.after)
        if (!aiClient.busy) {
            page.aiCompletionPending = false
            if (aiClient.error.length)
                page.aiCompletionError = aiClient.error
        }
    }
    onCompletionAvailableChanged: completionHintUpdate.restart()
    property var coverPdfSnapshot: null
    property string coverPdfError: ""
    property int coverPdfRevision: 0
    property var pendingSyncSource: null
    property var pendingHistoryNavigation: null
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
    _clickablePageIndicators: !utilityPopupOpen

    function updatePdfNavigation() {
        if (typeof pageStack === "undefined")
            return
        if (pageStack.busy) {
            pdfNavigationTimer.restart()
            return
        }
        if (attachedPdfPage && pageStack.currentPage === attachedPdfPage && page.wideTablet
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
            onStatusChanged: {
                if (status === PageStatus.Active) {
                    pdfNavigationTimer.restart()
                    page.scheduleCoverPdfCapture()
                }
            }
            onWidthChanged: if (status === PageStatus.Active) pdfNavigationTimer.restart()
            onHeightChanged: if (status === PageStatus.Active) pdfNavigationTimer.restart()
            Component.onDestruction: if (page.attachedPdfPage === separatePdfPage) page.attachedPdfPage = null
            AppBar {
                id: pdfPageAppBar
                objectName: "pdfPageAppBar"
                visible: page.editorActionsInMore
                width: parent.width
                headerText: page.projectName
                maximumHeaderWidth: Theme.dp(360)

                AppBarSpacer {
                    objectName: "pdfPageActionsSpacer"
                    fixedWidth: 0
                }
                AppBarButton {
                    objectName: "pdfPageCompile"
                    context: qsTr("Compile")
                    icon.source: "image://theme/icon-m-play"
                    enabled: !page.offline && !overleafClient.sourceBusy && !overleafClient.pdfBusy
                    onClicked: page.saveAndCompile()
                }
                AppBarButton {
                    objectName: "pdfPageOpenExternal"
                    context: qsTr("Open PDF in another app")
                    icon.source: "image://theme/icon-m-file-pdf"
                    enabled: overleafClient.pdfPath.length > 0 && !overleafClient.pdfBusy
                    onClicked: externalPdf.open()
                }
            }
            Shortcut {
                sequence: StandardKey.Save
                enabled: separatePdfPage.status === PageStatus.Active
                autoRepeat: false
                onActivated: page.saveAndCompile()
            }
            SilicaFlickable {
                id: pdfPageFlick
                anchors {
                    top: page.editorActionsInMore ? pdfPageAppBar.bottom : parent.top
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                contentHeight: height
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
        coverSnippet = lines.slice(start, start + 30).join("\n")
        coverSnippetFile = currentFile
    }

    function scheduleCoverPdfCapture() {
        ++coverPdfRevision
        if (pdfLoader.item && pdfLoader.item.visible && pdfLoader.item.status === PdfDocument.Ready
                && Qt.application.state === Qt.ApplicationActive)
            coverPdfTimer.restart()
    }

    function captureCoverPdf() {
        var view = pdfLoader.item
        if (!view || !view.visible || Qt.application.state !== Qt.ApplicationActive
                || view.status !== PdfDocument.Ready || view.width <= 0 || view.height <= 0)
            return
        var revision = coverPdfRevision
        var pdfPath = overleafClient.pdfPath
        var scale = 256 / Math.max(view.width, view.height)
        var targetWidth = Math.max(1, Math.round(view.width * scale))
        var targetHeight = Math.max(1, Math.round(view.height * scale))
        var started = view.grabToImage(function(result) {
            if (revision !== page.coverPdfRevision || pdfPath !== overleafClient.pdfPath
                    || view !== pdfLoader.item || !view.visible
                    || Qt.application.state !== Qt.ApplicationActive)
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

    function openHistoryLocation(target) {
        var path = normalizeSyncPath(target.path || "")
        if (!path)
            return false
        pendingHistoryNavigation = { path: path, text: target.text || "" }
        if (path === currentFile && !overleafClient.sourceBusy) {
            finishHistoryNavigation()
        } else if (!openProjectFile(path)) {
            pendingHistoryNavigation = null
            return false
        }
        return true
    }

    function finishHistoryNavigation() {
        var target = pendingHistoryNavigation
        if (!target || target.path !== currentFile)
            return
        pendingHistoryNavigation = null
        var fragment = target.text
        var index = fragment.length ? editor.text.indexOf(fragment) : -1
        if (index < 0 || editor.text.indexOf(fragment, index + 1) >= 0)
            return
        editor._editor.cursorPosition = index
        editor._editor.forceActiveFocus()
        var rect = editor._editor.positionToRectangle(index)
        var mapped = editor._editor.mapToItem(codePane.contentItem, rect.x, rect.y)
        codePane.contentY = Math.max(0, Math.min(codePane.contentHeight - codePane.height,
                                                mapped.y - codePane.height / 2))
        if (!offline && overleafClient.pdfPath.length && !overleafClient.pdfBusy
                && !overleafClient.sourceBusy && baseKnown && editor.text === baseText
                && (currentFile !== compiledSnapshotFile || editor.text === compiledSnapshotText)) {
            syncRequestFile = currentFile
            syncRequestText = editor.text
            var before = editor.text.substring(0, index).split("\n")
            overleafClient.syncSourceToPdf(projectId, currentFile, before.length, before[before.length - 1].length)
        }
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
            aiClient.reload()
            if (deferOpen)
                initializeEditor()
            activateCover()
            pdfNavigationTimer.restart()
            scheduleCoverPdfCapture()
        } else {
            clearAiCompletion()
            closeWolframResult()
            closeWolframInput()
            reversoPopup.close()
            aiGeneratorPopup.close()
            sciHubPopup.visible = false
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
                }
                ++page.coverPdfRevision
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
                page.pendingHistoryNavigation = null
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
            if (/\.tex$/i.test(page.currentFile) && !page.pendingSyncSource && !page.pendingHistoryNavigation) {
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
            if (page.pendingHistoryNavigation && page.pendingHistoryNavigation.path === page.currentFile)
                page.finishHistoryNavigation()
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
        objectName: "editorRedoShortcut"
        sequence: "Ctrl+Y"
        enabled: page.status === PageStatus.Active && page.initialized && !page.offline
                 && editor.enabled && editor._editor.activeFocus && !editor._editor.inputMethodComposing
        onActivated: editor._editor.redo()
    }
    Shortcut {
        objectName: "wolframAlphaShortcut"
        sequence: "Ctrl+W"
        enabled: page.status === PageStatus.Active && page.initialized && editor.enabled
                 && editor._editor.activeFocus && !editor._editor.inputMethodComposing && !wolframClient.busy
        autoRepeat: false
        onActivated: page.requestWolframResult()
    }
    Shortcut {
        objectName: "reversoShortcut"
        sequence: "Ctrl+R"
        enabled: page.status === PageStatus.Active && page.initialized && editor.enabled
                 && editor._editor.activeFocus && !editor._editor.inputMethodComposing
        autoRepeat: false
        onActivated: page.translateReversoWord()
    }
    Shortcut {
        objectName: "aiCompletionShortcut"
        sequence: "Ctrl+I"
        enabled: page.aiEnabled && page.status === PageStatus.Active && !page.offline
                 && editor.enabled && editor._editor.activeFocus && !aiClient.busy
        autoRepeat: false
        onActivated: page.requestAiCompletion()
    }
    Shortcut {
        objectName: "aiGeneratorShortcut"
        sequence: "Ctrl+Shift+I"
        enabled: page.aiEnabled && page.status === PageStatus.Active
        onActivated: page.openAiGenerator()
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
        enabled: page.status === PageStatus.Active && page.searchVisible && !page.utilityPopupOpen
                 && !searchInput.inputMethodComposing && !replaceInput.inputMethodComposing
                 && !editor.inputMethodComposing
        autoRepeat: false
        onActivated: page.searchVisible = false
    }
    Shortcut {
        objectName: "utilityPopupCloseShortcut"
        sequence: "Escape"
        enabled: page.status === PageStatus.Active && page.utilityPopupOpen
        autoRepeat: false
        onActivated: page.dismissUtilityPopups()
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
            page.clearAiCompletion()
        }
        onCursorPositionChanged: {
            page.clearAiCompletion()
            page.closeWolframResult()
        }
        onSelectionStartChanged: page.clearAiCompletion()
        onSelectionEndChanged: page.clearAiCompletion()
        onInputMethodComposingChanged: page.clearAiCompletion()
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
        objectName: "coverPdfTimer"
        interval: 600
        onTriggered: page.captureCoverPdf()
    }

    Timer {
        id: compileHintTimer
        objectName: "compileHintTimer"
        interval: 5000
    }

    Flickable {
        id: editorAppBarScroller
        objectName: "editorAppBarScroller"
        anchors.top: parent.top
        width: Math.max(0, parent.width - (page.forwardNavigation ? Theme.pageStackIndicatorWidth : 0))
        height: editorAppBar.height
        z: Theme.ZOrderHeader
        clip: true
        contentWidth: {
            var required = 2 * Theme.horizontalPageMargin + Theme.paddingMedium + Theme.dp(8)
            for (var i = 0; i < editorAppBar.content.length; ++i) {
                var item = editorAppBar.content[i]
                if (item.visible)
                    required += item.hasOwnProperty("fixedWidth") ? item.fixedWidth : item.implicitWidth
            }
            return Math.max(width, required)
        }
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        onWidthChanged: contentX = Math.max(0, Math.min(contentX, contentWidth - width))
        onContentWidthChanged: contentX = Math.max(0, Math.min(contentX, contentWidth - width))

        HorizontalScrollDecorator {}
    }
    MouseArea {
        anchors.top: editorAppBarScroller.top
        width: page.width
        height: editorAppBarScroller.height
        z: Theme.ZOrderHeader + 1
        enabled: page.utilityPopupOpen
        onClicked: page.dismissUtilityPopups()
    }

    AppBar {
        id: editorAppBar
        objectName: "editorAppBar"
        parent: editorAppBarScroller.contentItem
        width: editorAppBarScroller.contentWidth
        headerText: page.projectName
        maximumHeaderWidth: Theme.dp(360)

        AppBarButton {
            objectName: "saveAndCompile"
            text: qsTr("Compile")
            context: qsTr("Compile")
            icon.source: "image://theme/icon-m-play"
            layoutDirection: Qt.LeftToRight
            visible: !page.editorActionsInMore
            enabled: !page.offline && !overleafClient.sourceBusy && !overleafClient.pdfBusy
            onClicked: page.saveAndCompile()
        }
        AppBarButton {
            objectName: "editorProjectFiles"
            text: qsTr("Files")
            context: qsTr("Files")
            icon.source: "image://theme/icon-m-file-folder"
            layoutDirection: Qt.LeftToRight
            onClicked: pageStack.push(Qt.resolvedUrl("ProjectFilesPage.qml"), {
                "projectId": page.projectId,
                "projectName": page.projectName,
                "editorPage": page
            })
        }
        AppBarButton {
            objectName: "editorMoreActions"
            text: qsTr("More")
            context: qsTr("More")
            icon.source: "image://theme/icon-m-menu"
            layoutDirection: Qt.LeftToRight
            visible: page.editorActionsInMore
            onClicked: moreActionsPopup.open()

            PopupMenu {
                id: moreActionsPopup
                objectName: "editorMorePopup"
                preferredWidth: Math.min(page.width - 2 * Theme.horizontalPageMargin, Theme.dp(420))
                headerBackground: PopupMenuHeader.SolidBackground
                headerText: qsTr("More")

                PopupMenuItem {
                    objectName: "moreDownloadProjectArchive"
                    text: qsTr("Download saved project ZIP")
                    enabled: !page.offline && !overleafClient.archiveBusy
                    onClicked: overleafClient.downloadProjectArchive(page.projectId)
                }
                PopupMenuItem {
                    objectName: "moreEditorSearch"
                    text: qsTr("Find and replace")
                    onClicked: {
                        page.searchVisible = true
                        searchInput.forceActiveFocus()
                    }
                }
                PopupMenuItem {
                    objectName: "moreProjectChat"
                    text: qsTr("Chat")
                    visible: !page.offline
                    onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                        "projectId": page.projectId, "mode": "chat"
                    })
                }
                PopupMenuItem {
                    objectName: "moreProjectHistory"
                    text: qsTr("History")
                    visible: !page.offline
                    onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                        "projectId": page.projectId, "mode": "history", "editorPage": page
                    })
                }
                PopupMenuItem {
                    objectName: "moreProjectSharing"
                    text: qsTr("Access")
                    visible: !page.offline
                    onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                        "projectId": page.projectId, "mode": "share"
                    })
                }
                PopupMenuItem {
                    objectName: "moreProjectComments"
                    text: qsTr("Comments")
                    visible: !page.offline
                    enabled: overleafClient.commentsAvailable
                    onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                        "projectId": page.projectId, "mode": "comments"
                    })
                }
                PopupMenuItem {
                    objectName: "moreAiAssistant"
                    text: qsTr("Generate with AI")
                    visible: page.aiEnabled
                    onClicked: page.openAiGenerator()
                }
                PopupMenuItem {
                    objectName: "moreKeyboardShortcuts"
                    text: qsTr("Keyboard shortcuts")
                    onClicked: moreShortcutsPopup.open()
                }
                PopupMenuItem {
                    objectName: "moreEditorSettings"
                    text: qsTr("Settings")
                    onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                }
            }

            PopupMenu {
                id: moreShortcutsPopup
                objectName: "editorMoreShortcutsPopup"
                preferredWidth: Math.min(page.width - 2 * Theme.horizontalPageMargin, Theme.dp(420))
                headerBackground: PopupMenuHeader.SolidBackground
                headerText: qsTr("Keyboard shortcuts")

                Item {
                    width: parent.width
                    height: moreShortcutList.implicitHeight + 2 * Theme.paddingMedium

                    Label {
                        id: moreShortcutList
                        objectName: "editorMoreShortcutList"
                        anchors {
                            top: parent.top
                            left: parent.left
                            right: parent.right
                            margins: Theme.paddingMedium
                        }
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSizeSmall
                        text: page.keyboardShortcutHintText
                    }
                }
            }
        }
        AppBarSpacer {
            objectName: "editorRightActionsSpacer"
        }
        AppBarButton {
            objectName: "downloadProjectArchive"
            context: qsTr("Download saved project ZIP")
            icon.source: "image://theme/icon-splus-downloads"
            visible: !page.editorActionsInMore
            enabled: !page.offline && !overleafClient.archiveBusy
            onClicked: overleafClient.downloadProjectArchive(page.projectId)
        }
        AppBarButton {
            objectName: "openCachedPdf"
            context: qsTr("Open PDF in another app")
            icon.source: "image://theme/icon-m-file-pdf"
            visible: !page.editorActionsInMore
            enabled: overleafClient.pdfPath.length > 0 && !overleafClient.pdfBusy
            onClicked: externalPdf.open()
        }
        AppBarButton {
            objectName: "editorSearch"
            context: qsTr("Find and replace")
            icon.source: "image://theme/icon-m-search"
            visible: !page.editorActionsInMore
            onClicked: {
                page.searchVisible = true
                searchInput.forceActiveFocus()
            }
        }
        AppBarButton {
            objectName: "projectChat"
            context: qsTr("Chat")
            icon.source: "image://theme/icon-m-chat"
            layoutDirection: Qt.LeftToRight
            visible: !page.offline && !page.editorActionsInMore
            onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                "projectId": page.projectId, "mode": "chat"
            })
        }
        AppBarButton {
            objectName: "projectHistory"
            context: qsTr("History")
            icon.source: "image://theme/icon-m-clock"
            layoutDirection: Qt.LeftToRight
            visible: !page.offline && !page.editorActionsInMore
            onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                "projectId": page.projectId, "mode": "history", "editorPage": page
            })
        }
        AppBarButton {
            objectName: "projectSharing"
            context: qsTr("Access")
            icon.source: "image://theme/icon-m-share"
            layoutDirection: Qt.LeftToRight
            visible: !page.offline && !page.editorActionsInMore
            onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                "projectId": page.projectId, "mode": "share"
            })
        }
        AppBarButton {
            objectName: "projectComments"
            context: overleafClient.commentsAvailable ? qsTr("Comments") : qsTr("Comments unavailable")
            icon.source: Qt.resolvedUrl("../icons/comments.png")
            layoutDirection: Qt.LeftToRight
            visible: !page.offline && !page.editorActionsInMore
            enabled: overleafClient.commentsAvailable
            onClicked: pageStack.push(Qt.resolvedUrl("ProjectCollaborationPage.qml"), {
                "projectId": page.projectId, "mode": "comments"
            })
        }
        AppBarButton {
            objectName: "editorReverso"
            Image {
                objectName: "editorReversoIcon"
                anchors.centerIn: parent
                width: Theme.iconSizeSmallPlus
                height: width
                fillMode: Image.PreserveAspectFit
                source: Qt.resolvedUrl("../icons/translate.png")
                opacity: parent.enabled ? 1 : Theme.opacityLow
            }
            context: qsTr("Reverso")
            enabled: page.status === PageStatus.Active
            onClicked: page.openReverso()
        }
        AppBarButton {
            objectName: "editorWolframAlpha"
            context: qsTr("WolframAlpha")
            preferredWidth: Theme.dp(72)
            Image {
                objectName: "editorWolframAlphaIcon"
                anchors.centerIn: parent
                width: Theme.iconSizeSmallPlus
                height: width
                fillMode: Image.PreserveAspectFit
                source: Qt.resolvedUrl(Theme.colorScheme === Theme.LightOnDark
                                       ? "../icons/rheniumbeta-dark.png" : "../icons/rheniumbeta-light.png")
                opacity: parent.enabled ? 1 : Theme.opacityLow
            }
            enabled: page.status === PageStatus.Active
            onClicked: page.openWolframInput()
        }
        AppBarButton {
            objectName: "editorSciHub"
            context: qsTr("Sci-Hub")
            Image {
                objectName: "editorSciHubIcon"
                anchors.centerIn: parent
                width: Theme.iconSizeSmallPlus
                height: width
                fillMode: Image.PreserveAspectFit
                source: Qt.resolvedUrl(Theme.colorScheme === Theme.LightOnDark
                                       ? "../icons/scihub-light.png" : "../icons/scihub.png")
                opacity: parent.enabled ? 1 : Theme.opacityLow
            }
            enabled: page.status === PageStatus.Active
            onClicked: {
                page.closeWolframResult()
                page.closeWolframInput()
                reversoPopup.close()
                aiGeneratorPopup.close()
                page.clearAiCompletion()
                sciHubPopup.visible = true
                sciHubInput.forceActiveFocus()
            }
        }
        AppBarButton {
            objectName: "editorAiAssistant"
            context: qsTr("Generate with AI")
            icon.source: Qt.resolvedUrl("../icons/ai.png")
            layoutDirection: Qt.LeftToRight
            visible: page.aiEnabled && !page.editorActionsInMore
            onClicked: page.openAiGenerator()
        }
        AppBarButton {
            objectName: "editorKeyboardShortcuts"
            context: qsTr("Keyboard shortcuts")
            icon.source: "image://theme/icon-m-keyboard"
            visible: !page.editorActionsInMore
            onClicked: shortcutsPopup.open()

            PopupMenu {
                id: shortcutsPopup
                objectName: "editorShortcutsPopup"
                preferredWidth: Math.min(page.width - 2 * Theme.horizontalPageMargin, Theme.dp(420))
                headerBackground: PopupMenuHeader.SolidBackground
                headerText: qsTr("Keyboard shortcuts")

                Item {
                    width: parent.width
                    height: shortcutList.implicitHeight + 2 * Theme.paddingMedium

                    Label {
                        id: shortcutList
                        objectName: "editorShortcutList"
                        anchors {
                            top: parent.top
                            left: parent.left
                            right: parent.right
                            margins: Theme.paddingMedium
                        }
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSizeSmall
                        text: page.keyboardShortcutHintText
                    }
                }
            }
        }
        AppBarButton {
            objectName: "editorSettings"
            context: qsTr("Settings")
            icon.source: "image://theme/icon-splus-setting"
            visible: !page.editorActionsInMore
            onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
        }
    }

    Item {
        id: headerPane
        objectName: "editorContent"
        anchors.top: editorAppBarScroller.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        MouseArea {
            objectName: "utilityPopupDismissArea"
            anchors.fill: parent
            z: 19
            enabled: page.utilityPopupOpen && !page.wolframVisible
            onClicked: page.dismissUtilityPopups()
        }
        Rectangle {
            id: sciHubPopup
            objectName: "sciHubPopup"
            readonly property real hatOverhang: Math.min(parent.width / 4, Theme.itemSizeMedium * 1.15)
            anchors.top: parent.top
            anchors.right: parent.right
            z: 23
            width: Math.max(0, Math.min(parent.width - hatOverhang, Theme.itemSizeLarge * 5))
            height: Math.max(0, Math.min(parent.height, sciHubContent.height + 2 * Theme.paddingLarge))
            visible: false
            color: page.editorBackground
            border.color: editor.color
            radius: Theme.paddingLarge
            MouseArea { anchors.fill: parent }
            Image {
                id: sciHubHat
                objectName: "sciHubHat"
                parent: page
                readonly property real baseX: -Math.min(Math.max(0, width - Theme.paddingLarge - Theme.paddingSmall),
                                                        sciHubPopup.hatOverhang)
                x: headerPane.x + sciHubPopup.x + baseX + sciHubHatMetrics.advanceWidth("Sci")
                y: headerPane.y + sciHubPopup.y - height * 0.62 - sciHubHatMetrics.height / 3
                z: Theme.ZOrderHeader + 2
                visible: sciHubPopup.visible
                width: Math.min(sciHubPopup.width / 3, Theme.itemSizeMedium * 1.5)
                height: width * 178 / 224
                fillMode: Image.PreserveAspectFit
                source: Qt.resolvedUrl("../icons/scihub-hat.png")
                FontMetrics {
                    id: sciHubHatMetrics
                    font: sciHubTitle.font
                }
            }
            SilicaFlickable {
                anchors.fill: parent
                anchors.margins: Theme.paddingLarge
                contentHeight: sciHubContent.height
                clip: true
                Column {
                    id: sciHubContent
                    width: parent.width
                    Row {
                        width: parent.width
                        Label {
                            id: sciHubTitle
                            objectName: "sciHubTitle"
                            width: parent.width
                            height: Theme.itemSizeSmall
                            verticalAlignment: Text.AlignVCenter
                            text: qsTr("Sci-Hub")
                            color: editor.color
                        }
                    }
                    Item {
                        width: parent.width
                        height: Math.max(sciHubInput.y + sciHubInput.height,
                                         sciHubSearch.y + sciHubSearch.height)
                        TextField {
                            id: sciHubInput
                            objectName: "sciHubInput"
                            anchors.left: parent.left
                            anchors.right: sciHubSearch.left
                            anchors.rightMargin: Theme.paddingSmall
                            y: Math.max(0, sciHubSearch.height / 2
                                - sciHubInput._editor.mapToItem(sciHubInput, 0,
                                    sciHubInput._editor.height / 2).y)
                            label: qsTr("DOI or article URL")
                            EnterKey.onClicked: if (sciHubInput.text.trim().length) page.openSciHubSearch()
                        }
                        Rectangle {
                            id: sciHubSearch
                            objectName: "sciHubSearch"
                            anchors.right: parent.right
                            y: Math.max(0, sciHubInput._editor.mapToItem(parent, 0,
                                sciHubInput._editor.height / 2).y - height / 2)
                            width: Math.min(parent.width * 0.3, Theme.itemSizeLarge)
                            height: width
                            radius: width / 2
                            color: "#cd181b"
                            border.color: "white"
                            border.width: Math.max(2, Theme.paddingSmall / 2)
                            enabled: sciHubInput.text.trim().length > 0
                            opacity: enabled ? 1 : Theme.opacityLow
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Open")
                            Keys.onReturnPressed: page.openSciHubSearch()
                            Keys.onEnterPressed: page.openSciHubSearch()
                            Text {
                                anchors.centerIn: parent
                                color: "white"
                                text: "гоп"
                                font.bold: true
                                font.pixelSize: Theme.fontSizeLarge
                            }
                            MouseArea { anchors.fill: parent; onClicked: page.openSciHubSearch() }
                        }
                    }
                    Label {
                        id: sciHubError
                        width: parent.width
                        visible: text.length > 0
                        color: Theme.errorColor
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
        AiAssistantPopup {
            id: aiGeneratorPopup
            anchors.top: parent.top
            anchors.right: parent.right
            z: 22
            client: aiGeneratorClient
            editorPage: page
            textColor: editor.color
            backgroundColor: page.editorBackground
        }
        ReversoPopup {
            id: reversoPopup
            property point editorOrigin: {
                var paneX = panes.x
                var paneY = panes.y
                var codeX = codePane.x
                var codeY = codePane.y
                return codePane.mapToItem(headerPane, 0, 0)
            }
            property point cursorPoint: completionPopup.cursorPoint
            property real cursorTop: Math.max(0, Math.min(codePane.height, cursorPoint.y))
            property real cursorBottom: Math.max(0, Math.min(codePane.height,
                cursorPoint.y + editor._editor.cursorRectangle.height))
            property real spaceAbove: cursorTop
            property real spaceBelow: codePane.height - cursorBottom
            property bool placeBelow: spaceBelow >= preferredHeight || spaceBelow >= spaceAbove
            maximumWidth: reverseDirection ? codePane.width : headerPane.width
            maximumHeight: reverseDirection ? (placeBelow ? spaceBelow : spaceAbove) : headerPane.height
            x: reverseDirection
               ? editorOrigin.x + Math.max(0, Math.min(codePane.width - width, cursorPoint.x))
               : headerPane.width - width
            y: reverseDirection
               ? editorOrigin.y + (placeBelow ? cursorBottom : cursorTop - height)
               : 0
            z: 21
            textColor: editor.color
            backgroundColor: page.editorBackground
        }
        Rectangle {
            id: wolframInputPopup
            objectName: "wolframAlphaInputPopup"
            anchors.top: parent.top
            anchors.right: parent.right
            width: Math.min(parent.width, Theme.itemSizeLarge * 5)
            height: Math.min(parent.height, Theme.itemSizeLarge * 4,
                             wolframInputContents.height + 2 * Theme.paddingLarge)
            visible: page.wolframInputVisible
            z: 20
            color: "#303030"
            radius: Theme.paddingLarge
            MouseArea { anchors.fill: parent }
            SilicaFlickable {
                anchors.fill: parent
                anchors.margins: Theme.paddingLarge
                contentHeight: wolframInputContents.height
                clip: true
                Column {
                    id: wolframInputContents
                    width: parent.width
                    Rectangle {
                        objectName: "wolframAlphaInputFrame"
                        width: parent.width
                        height: Theme.itemSizeLarge
                        color: "#303030"
                        radius: Theme.paddingLarge
                        border.color: "#ffad32"
                        border.width: Math.max(2, Theme.paddingSmall / 2)
                        TextInput {
                            id: wolframInput
                            objectName: "wolframAlphaInput"
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.paddingLarge
                            anchors.right: wolframSubmit.left
                            anchors.rightMargin: Theme.paddingSmall
                            anchors.verticalCenter: parent.verticalCenter
                            color: "white"
                            selectionColor: "#ffad32"
                            selectedTextColor: "#303030"
                            font.pixelSize: Theme.fontSizeMedium
                            clip: true
                            readOnly: wolframClient.busy
                            selectByMouse: true
                            onAccepted: page.submitWolframInput()
                            Text {
                                anchors.fill: parent
                                visible: !wolframInput.text.length
                                text: qsTr("Enter what you want to calculate or know about")
                                color: "#bdbdbd"
                                font.family: wolframInput.font.family
                                font.pixelSize: wolframInput.font.pixelSize
                                font.italic: true
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            id: wolframSubmit
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.paddingSmall
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.itemSizeSmall
                            height: width
                            radius: Theme.paddingMedium
                            color: "#ff9f20"
                            opacity: wolframClient.busy ? Theme.opacityLow : 1
                            Text {
                                anchors.centerIn: parent
                                text: "="
                                color: "#303030"
                                font.bold: true
                                font.pixelSize: Theme.fontSizeLarge
                            }
                            MouseArea {
                                objectName: "wolframAlphaSubmit"
                                anchors.fill: parent
                                enabled: !wolframClient.busy
                                onClicked: page.submitWolframInput()
                            }
                        }
                    }
                    Text {
                        objectName: "wolframAlphaInputResult"
                        width: parent.width
                        visible: page.wolframInputSubmitted
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        color: wolframClient.error.length ? Theme.errorColor : "white"
                        font.pixelSize: Theme.fontSizeSmall
                        text: wolframClient.error.length ? wolframClient.error
                              : wolframClient.busy ? qsTr("Waiting for WolframAlpha…")
                              : wolframClient.result
                    }
                }
                VerticalScrollDecorator {}
            }
        }
        Item {
            id: panes
            objectName: "editorPanes"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            property bool sideBySide: page.wideTablet
            onSideBySideChanged: pdfNavigationTimer.restart()

            MouseArea {
                anchors.fill: parent
                z: 4.5
                enabled: page.wolframVisible
                onClicked: page.dismissUtilityPopups()
            }
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
                        textFormat: Text.PlainText
                        color: Theme.errorColor
                        visible: draftStore.error.length > 0 || overleafClient.sourceError.length > 0
                                 || (page.offline && overleafClient.error && overleafClient.error.length > 0)
                                 || page.actionError.length > 0 || externalPdf.error.length > 0
                                 || (page.aiEnabled && page.aiClientOtherError.length > 0)
                                 || (overleafClient.archiveProject === page.projectId
                                     && overleafClient.archiveError.length > 0)
                        text: draftStore.error.length ? draftStore.error
                              : overleafClient.sourceError.length ? overleafClient.sourceError
                              : page.offline && overleafClient.error && overleafClient.error.length ? overleafClient.error
                              : page.actionError.length ? page.actionError
                              : page.aiEnabled && page.aiClientOtherError.length ? page.aiClientOtherError
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
                            page.closeWolframResult()
                            if (reversoPopup.showing && reversoPopup.reverseDirection)
                                reversoPopup.close()
                            if (page.initialized) {
                                page.clearAiCompletion()
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
                            visible: editor._editor.activeFocus && (!!page.latexSuggestion || page.aiSuggestion.length > 0)
                                     && !page.aiCompletionOriginal.length
                                     && (editor._editor.cursorPosition === editor._editor.length
                                         || editor._editor.text.charAt(editor._editor.cursorPosition) === "\n")
                            text: page.latexSuggestion
                                  ? page.latexSuggestion.display
                                    + (page.completionTabHintActive ? "  <- " + qsTr("Press Tab to accept") : "")
                                  : page.aiSuggestion
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
                                var editorX = editor.x
                                var editorY = editor.y
                                var inputX = editor._editor.x
                                var inputY = editor._editor.y
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
                            radius: Theme.paddingLarge
                            visible: !page.wolframVisible && editor._editor.activeFocus
                                     && (page.aiCompletionError.length > 0 || !!page.latexSuggestion || page.aiSuggestion.length > 0)
                                     && (page.aiCompletionError.length > 0 || !completionHint.visible)
                            Text {
                                id: popupText
                                objectName: "latexCompletionPopupText"
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width - Theme.paddingMedium * 2)
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                                text: page.aiCompletionError.length ? page.aiCompletionError
                                      : page.latexSuggestion
                                      ? page.latexSuggestion.display
                                        + (page.completionTabHintActive ? "  <- " + qsTr("Press Tab to accept") : "")
                                      : page.aiCompletionOriginal.length
                                        ? qsTr("Replace:") + " " + page.aiCompletionOriginal
                                          + "\n" + qsTr("With:") + " " + page.aiSuggestion
                                        : page.aiSuggestion
                                font: editor._editor.font
                                color: page.aiCompletionError.length ? Theme.errorColor : editor.color
                            }
                        }
                        CompletionKeyFilter {
                            id: completionKeys
                            objectName: "latexCompletionKeyFilter"
                            target: editor._editor
                            enabled: (!!page.latexSuggestion
                                     && (page.latexSuggestion.snippet || page.latexCompletion.length > 0))
                                     || page.aiSuggestion.length > 0
                            onAccepted: {
                                if (!page.latexSuggestion && page.aiSuggestion.length) {
                                    var aiText = page.aiSuggestion
                                    var aiStart = page.aiCompletionStart
                                    var aiEnd = page.aiCompletionEnd
                                    if (page.currentFile !== page.aiRequestFile || editor.text !== page.aiRequestText
                                            || editor._editor.cursorPosition !== page.aiRequestPosition
                                            || !completionKeys.replace(aiStart, aiEnd, aiText)) {
                                        page.clearAiCompletion()
                                        page.aiCompletionError = qsTr("The document changed or cannot be edited. Request the suggestion again.")
                                        return
                                    }
                                    page.clearAiCompletion()
                                    editor._editor.cursorPosition = aiStart + aiText.length
                                    return
                                }
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
                    flickable: page.wheelTarget === "pdf" && pdfPane.visible && pdfLoader.item
                               ? pdfLoader.item.wheelFlickable : codePane
                    controlWheelZoom: true
                    trackPointer: true
                    onPointerEntered: {
                        page.wheelTarget = "editor"
                        page.zoomTarget = "editor"
                    }
                    onZoomRequested: {
                        page.zoomTarget = page.wheelTarget === "pdf" && pdfPane.visible && pdfLoader.item
                                          ? "pdf" : "editor"
                        page.adjustZoom(direction)
                    }
                }
                ProgressBar {
                    objectName: "aiCompletionProgress"
                    parent: panes
                    property point cursorPoint: completionPopup.cursorPoint
                    x: codePane.x + Math.max(0, Math.min(codePane.width - width, cursorPoint.x))
                    y: codePane.y + (cursorPoint.y + editor._editor.cursorRectangle.height + height <= codePane.height
                       ? Math.max(0, cursorPoint.y + editor._editor.cursorRectangle.height)
                       : Math.max(0, cursorPoint.y - height))
                    width: Math.min(codePane.width / 3, Theme.itemSizeLarge * 2)
                    height: Theme.paddingLarge
                    leftMargin: 0
                    rightMargin: 0
                    visible: page.aiCompletionPending
                             && cursorPoint.y + editor._editor.cursorRectangle.height > 0
                             && cursorPoint.y < codePane.height
                    indeterminate: true
                    z: 4
                }
                Rectangle {
                    id: wolframPopup
                    objectName: "wolframAlphaPopup"
                    parent: panes
                    property point cursorPoint: completionPopup.cursorPoint
                    x: codePane.x + Math.max(0, Math.min(codePane.width - width, cursorPoint.x))
                    y: codePane.y + Math.max(0, Math.min(codePane.height - height,
                        cursorPoint.y + editor._editor.cursorRectangle.height + height <= codePane.height
                        ? cursorPoint.y + editor._editor.cursorRectangle.height : cursorPoint.y - height))
                    width: Math.min(codePane.width, Theme.itemSizeLarge * 4)
                    height: Math.min(codePane.height, wolframText.implicitHeight + 2 * Theme.paddingLarge)
                    visible: page.wolframVisible
                    z: 5
                    color: page.editorBackground
                    border.color: editor.color
                    radius: Theme.paddingLarge
                    MouseArea { anchors.fill: parent }
                    Column {
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        SilicaFlickable {
                            width: parent.width
                            height: parent.height
                            contentHeight: wolframText.height
                            clip: true
                            Text {
                                id: wolframText
                                objectName: "wolframAlphaPopupText"
                                width: parent.width
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                                color: wolframClient.error.length ? Theme.errorColor : editor.color
                                font.pixelSize: Theme.fontSizeSmall
                                text: wolframClient.error.length ? wolframClient.error
                                      : wolframClient.busy ? qsTr("Waiting for WolframAlpha…")
                                      : wolframClient.result
                            }
                            VerticalScrollDecorator {}
                        }
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
                            onVisibleChanged: page.scheduleCoverPdfCapture()
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
                    WheelScroll {
                        objectName: "pdfZoomWheel"
                        anchors.fill: parent
                        z: 2
                        enabled: !!pdfLoader.item
                        flickable: page.wheelTarget === "editor" ? codePane
                                   : pdfLoader.item ? pdfLoader.item.wheelFlickable : null
                        controlWheelZoom: true
                        trackPointer: true
                        onPointerEntered: {
                            page.wheelTarget = "pdf"
                            page.zoomTarget = "pdf"
                        }
                        onZoomRequested: {
                            page.zoomTarget = page.wheelTarget
                            page.adjustZoom(direction)
                        }
                    }
                }
                Button {
                    objectName: "pdfCompileButton"
                    anchors.centerIn: parent
                    text: qsTr("Save and compile")
                    enabled: !page.offline && !overleafClient.sourceBusy && !overleafClient.pdfBusy
                    visible: overleafClient.pdfPath.length === 0 && !page.compileInProgress
                             && !page.editorActionsInMore
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
