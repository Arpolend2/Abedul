import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string projectId
    property string projectName
    property var editorPage
    property string externalError: ""
    property string directory: "/"
    property bool ownsFiles: true
    property var selectedItem
    property bool treeLoaded: false
    property var pendingFileOperation: null
    property var uploadQueue: []
    property var uploadCurrent: null
    property string refreshUploadedFile: ""
    backNavigation: !overleafClient.sourceBusy && !pendingFileOperation && !uploadCurrent

    function joinPath(dir, name) {
        return (dir === "/" ? "/" : dir + "/") + name
    }

    function affectsFile(path, type, file) {
        return file.length > 0 && (file === path || (type === "folder" && file.indexOf(path + "/") === 0))
    }

    function canChange(path, type, newPath, allowCurrent) {
        editorPage.saveDraft()
        if (editorPage.dirty) {
            externalError = qsTr("Save the current draft before changing project files.")
            return false
        }
        var files = overleafClient.projectFiles
        var paths = []
        if (type === "doc")
            paths.push(path)
        if (type === "folder") {
            for (var i = 0; i < files.length; ++i) {
                var filePath = files[i].path.charAt(0) === "/" ? files[i].path : "/" + files[i].path
                if (files[i].type === "doc" && filePath.indexOf(path + "/") === 0)
                    paths.push(filePath)
            }
        }
        if (newPath && type === "doc")
            paths.push(newPath)
        if (newPath && type === "folder") {
            var n = paths.length
            for (var j = 0; j < n; ++j)
                paths.push(newPath + paths[j].substring(path.length))
        }
        for (var k = 0; k < paths.length; ++k) {
            if (editorPage.currentFile === paths[k]) {
                if (allowCurrent && affectsFile(path, type, paths[k]))
                    continue
                externalError = qsTr("A local draft exists for this document. It was kept; this operation is blocked.")
                return false
            }
            if (!draftStore.loadFile(overleafClient.server, projectId, paths[k])) {
                externalError = draftStore.error
                return false
            }
            if (draftStore.exists && (!draftStore.baseKnown || draftStore.text !== draftStore.baseText)) {
                externalError = qsTr("A local draft exists for this document. It was kept; this operation is blocked.")
                return false
            }
        }
        return true
    }

    function startFileOperation(action, path, type, newPath, name, url) {
        if (pendingFileOperation || overleafClient.sourceBusy || overleafClient.filesBusy)
            return
        externalError = ""
        var affectsCurrent = affectsFile(path, type, editorPage.currentFile)
        if (!canChange(path, type, newPath, affectsCurrent))
            return
        pendingFileOperation = { action: action, path: path, type: type, newPath: newPath,
                                 name: name, url: url, currentFile: editorPage.currentFile }
        if (affectsCurrent) {
            if (!editorPage.saveBeforeFileOperation()) {
                pendingFileOperation = null
                externalError = draftStore.error.length ? draftStore.error
                                : qsTr("Could not save the open document before changing it.")
            }
        } else {
            runPendingFileOperation()
        }
    }

    function runPendingFileOperation() {
        var operation = pendingFileOperation
        if (!operation)
            return
        pendingFileOperation = null
        if (operation.action === "rename")
            overleafClient.renameProjectEntity(projectId, operation.path, operation.type, operation.name)
        else if (operation.action === "delete")
            overleafClient.deleteProjectEntity(projectId, operation.path, operation.type)
        else if (operation.action === "upload" || operation.action === "uploadReplace") {
            var parent = operation.path.substring(0, operation.path.lastIndexOf("/"))
            if (operation.action === "uploadReplace")
                overleafClient.overwriteProjectFile(projectId, parent.length ? parent : "/",
                                                    operation.url, operation.name, operation.type)
            else
                overleafClient.uploadProjectFile(projectId, parent.length ? parent : "/", operation.url)
        }
    }

    function uploadSelected(files) {
        if (!files.length || uploadCurrent || overleafClient.sourceBusy || overleafClient.filesBusy)
            return
        uploadQueue = files.slice(0)
        nextUpload()
    }

    function nextUpload() {
        if (overleafClient.sourceBusy || overleafClient.filesBusy) {
            uploadTimer.restart()
            return
        }
        if (externalError.length) {
            uploadCurrent = null
            uploadQueue = []
            return
        }
        if (refreshUploadedFile.length) {
            var path = refreshUploadedFile
            refreshUploadedFile = ""
            if (!editorPage.openProjectFile(path)) {
                uploadCurrent = null
                uploadQueue = []
                externalError = qsTr("Could not reopen the replaced document.")
                return
            }
            uploadTimer.restart()
            return
        }
        if (!uploadQueue.length) {
            uploadCurrent = null
            return
        }
        var files = uploadQueue.slice(0)
        uploadCurrent = files.shift()
        uploadQueue = files
        startFileOperation("upload", joinPath(directory, uploadCurrent.name), "doc", "", "", uploadCurrent.url)
        if (externalError.length) {
            uploadCurrent = null
            uploadQueue = []
        }
    }

    function openFile(item) {
        if (overleafClient.sourceBusy)
            return
        externalError = ""
        if (item.type === "folder")
            pageStack.push(Qt.resolvedUrl("ProjectFilesPage.qml"), {
                "projectId": projectId, "projectName": projectName,
                "editorPage": editorPage, "directory": item.path, "ownsFiles": false
            })
        else if (item.type === "file")
            overleafClient.openProjectFileExternally(projectId, item.path)
        else if (item.type === "doc" && editorPage.openProjectFile(item.path))
            pageStack.pop(editorPage)
    }

    Component.onCompleted: if (ownsFiles) overleafClient.loadProjectFiles(projectId)
    Component.onDestruction: if (ownsFiles) overleafClient.closeProjectFiles()

    Connections {
        target: overleafClient
        ignoreUnknownSignals: true
        onReconnected: {
            if (page.ownsFiles)
                overleafClient.loadProjectFiles(page.projectId)
        }
        onFilesBusyChanged: {
            if (!page.ownsFiles)
                return
            if (overleafClient.filesBusy)
                page.treeLoaded = false
            else if (!page.treeLoaded)
                treeTimer.restart()
        }
        onExternalFileReady: {
            if (page.status !== PageStatus.Active)
                return
            URIOpener.openUri(url, function() {}, function(error) {
                page.externalError = qsTr("Could not open the file: %1").arg(error)
            })
        }
        onSourceSaved: {
            var operation = page.pendingFileOperation
            if (!operation)
                return
            if (path !== operation.currentFile || !editorPage.finishFileOperationSave(path, text)) {
                page.pendingFileOperation = null
                page.uploadQueue = []
                page.uploadCurrent = null
                page.externalError = draftStore.error.length ? draftStore.error
                                     : qsTr("The open document changed while saving. Try again.")
                return
            }
            if (!page.canChange(operation.path, operation.type, operation.newPath, true)) {
                page.pendingFileOperation = null
                page.uploadQueue = []
                page.uploadCurrent = null
                return
            }
            page.runPendingFileOperation()
        }
        onSourceErrorChanged: {
            if (page.pendingFileOperation && !overleafClient.sourceBusy && overleafClient.sourceError.length) {
                page.pendingFileOperation = null
                page.externalError = overleafClient.sourceError
                page.uploadQueue = []
                page.uploadCurrent = null
            } else if (page.uploadCurrent && !overleafClient.sourceBusy && overleafClient.sourceError.length) {
                page.uploadQueue = []
                page.uploadCurrent = null
                page.externalError = overleafClient.sourceError
            }
        }
        onFilesErrorChanged: {
            if (page.uploadCurrent && overleafClient.filesError.length) {
                page.uploadQueue = []
                page.uploadCurrent = null
            }
        }
        onProjectFileOverwriteRequested: {
            if (!page.uploadCurrent)
                return
            pageStack.push(overwriteDialog, { "path": path, "type": type, "entityId": entityId })
        }
        onProjectFileOperationFinished: {
            if (page.ownsFiles)
                editorPage.fileOperationFinished(action, path, type, newPath)
            if (page.uploadCurrent && action === "upload") {
                var target = page.joinPath(page.directory, page.uploadCurrent.name)
                if (target === editorPage.currentFile)
                    page.refreshUploadedFile = target
                uploadTimer.restart()
            }
        }
    }

    Timer {
        id: uploadTimer
        interval: 100
        onTriggered: page.nextUpload()
    }

    Timer {
        id: treeTimer
        interval: 100
        onTriggered: {
            if (!overleafClient.filesBusy && !overleafClient.sourceBusy) {
                page.treeLoaded = true
                overleafClient.loadProjectFileTree(page.projectId)
            }
        }
    }

    Component {
        id: picker
        MultiContentPickerDialog {
            title: qsTr("Select files to upload")
            onAccepted: {
                var files = []
                for (var i = 0; i < selectedContent.count; ++i) {
                    var item = selectedContent.get(i)
                    if (!item.url || !item.fileName) {
                        page.externalError = qsTr("Could not read the selected local file.")
                        return
                    }
                    files.push({ name: item.fileName, url: item.url })
                }
                page.externalError = ""
                page.uploadSelected(files)
            }
            onRejected: page.uploadQueue = []
        }
    }

    Component {
        id: overwriteDialog
        Dialog {
            property string path
            property string type
            property string entityId
            Column {
                width: parent.width
                DialogHeader { title: qsTr("Replace file") }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    wrapMode: Text.Wrap
                    text: qsTr("Replace the existing file %1?").arg(path)
                }
            }
            onAccepted: {
                page.startFileOperation("uploadReplace", path, type, "", entityId, page.uploadCurrent.url)
                if (page.externalError.length) {
                    page.uploadQueue = []
                    page.uploadCurrent = null
                }
            }
            onRejected: uploadTimer.restart()
        }
    }

    Component {
        id: renameDialog
        Dialog {
            id: renamePage
            property var item
            canAccept: nameField.text.length > 0
            SilicaFlickable {
                anchors.fill: parent
                contentHeight: renameContents.height
                Column {
                    id: renameContents
                    width: parent.width
                    DialogHeader { title: qsTr("Rename") }
                    TextField {
                        id: nameField
                        width: parent.width
                        label: qsTr("Name")
                        text: renamePage.item.name
                    }
                }
                VerticalScrollDecorator {}
            }
            onAccepted: {
                var target = page.joinPath(page.directory, nameField.text)
                page.startFileOperation("rename", item.path, item.type, target, nameField.text, "")
            }
        }
    }

    Component {
        id: deleteDialog
        Dialog {
            property var item
            SilicaFlickable {
                anchors.fill: parent
                contentHeight: deleteContents.height
                Column {
                    id: deleteContents
                    width: parent.width
                    DialogHeader { title: qsTr("Delete") }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        text: qsTr("Delete %1 from the project? Folders and their contents will be deleted.").arg(item.name)
                    }
                }
                VerticalScrollDecorator {}
            }
            onAccepted: {
                page.startFileOperation("delete", item.path, item.type, "", "", "")
            }
        }
    }

    SilicaListView {
        id: fileList
        objectName: "projectFileList"
        anchors.fill: parent
        model: {
            var result = []
            var seen = {}
            var files = overleafClient.projectFiles
            var prefix = page.directory === "/" ? "/" : page.directory + "/"
            for (var i = 0; i < files.length; ++i) {
                var path = files[i].path
                if (path.charAt(0) !== "/")
                    path = "/" + path
                if (path.indexOf(prefix) !== 0)
                    continue
                var rest = path.substring(prefix.length)
                var slash = rest.indexOf("/")
                var name = slash < 0 ? rest : rest.substring(0, slash)
                if (!name.length || seen["item:" + name])
                    continue
                seen["item:" + name] = true
                result.push({name: name, path: page.joinPath(page.directory, name),
                             type: slash < 0 ? files[i].type : "folder"})
            }
            result.sort(function(a, b) {
                if (a.type === "folder" && b.type !== "folder") return -1
                if (b.type === "folder" && a.type !== "folder") return 1
                return a.name.localeCompare(b.name)
            })
            return result
        }
        header: Column {
            width: parent.width
            PageHeader {
                title: page.directory === "/" ? qsTr("Project files")
                       : page.directory.substring(page.directory.lastIndexOf("/") + 1)
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                visible: overleafClient.filesError.length > 0 || page.externalError.length > 0
                         || (overleafClient.offline === true && overleafClient.error && overleafClient.error.length > 0)
                text: page.externalError.length ? page.externalError
                      : overleafClient.filesError.length ? overleafClient.filesError
                      : overleafClient.error || ""
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: overleafClient.sourceBusy
                text: qsTr("Working…")
            }
        }
        PullDownMenu {
            MenuItem {
                text: qsTr("Upload files")
                visible: overleafClient.offline !== true
                enabled: !overleafClient.filesBusy && !overleafClient.sourceBusy && !page.uploadCurrent
                onClicked: pageStack.push(picker)
            }
            MenuItem {
                text: qsTr("Refresh")
                visible: overleafClient.offline !== true
                enabled: !overleafClient.filesBusy && !overleafClient.sourceBusy && !page.uploadCurrent
                onClicked: {
                    page.treeLoaded = false
                    overleafClient.loadProjectFiles(page.projectId)
                }
            }
        }
        delegate: ListItem {
            width: ListView.view.width
            contentHeight: Theme.itemSizeMedium
            enabled: !overleafClient.sourceBusy && !overleafClient.filesBusy && !page.uploadCurrent
            onClicked: page.openFile(modelData)
            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Rename")
                    visible: overleafClient.offline !== true
                    onClicked: pageStack.push(renameDialog, { item: modelData })
                }
                MenuItem {
                    text: qsTr("Delete")
                    visible: overleafClient.offline !== true
                    onClicked: pageStack.push(deleteDialog, { item: modelData })
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * x
                truncationMode: TruncationMode.Fade
                text: (modelData.type === "folder" ? "📁 " : "") + modelData.name
            }
        }
        ViewPlaceholder {
            enabled: overleafClient.filesBusy
            text: qsTr("Loading project files…")
        }
        ViewPlaceholder {
            enabled: !overleafClient.filesBusy && fileList.count === 0
                     && overleafClient.filesError.length === 0
            text: qsTr("No project files")
        }
        VerticalScrollDecorator {}
        WheelScroll { parent: fileList; flickable: fileList }
    }
}
