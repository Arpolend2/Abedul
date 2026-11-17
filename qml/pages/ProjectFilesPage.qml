import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string projectId
    property string projectName
    property var editorPage
    property string externalError: ""

    function openFile(item) {
        if (overleafClient.sourceBusy)
            return
        externalError = ""
        if (item.type === "file")
            overleafClient.openProjectFileExternally(projectId, item.path)
        else if (item.type === "doc" && editorPage.openProjectFile(item.path))
            pageStack.pop()
    }

    Component.onCompleted: overleafClient.loadProjectFiles(projectId)
    Component.onDestruction: overleafClient.closeProjectFiles()

    Connections {
        target: overleafClient
        onExternalFileReady: {
            URIOpener.openUri(url, function() {}, function(error) {
                page.externalError = qsTr("Could not open the file: %1").arg(error)
            })
        }
    }

    SilicaListView {
        id: fileList
        anchors.fill: parent
        model: overleafClient.projectFiles
        header: Column {
            width: parent.width
            PageHeader { title: qsTr("Project files") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                text: qsTr("Documents open in the editor. Other files open as temporary copies; changes are not synced back.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                visible: overleafClient.filesError.length > 0 || page.externalError.length > 0
                text: overleafClient.filesError.length ? overleafClient.filesError : page.externalError
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                visible: overleafClient.sourceBusy
                text: qsTr("Opening file…")
            }
        }
        PullDownMenu {
            MenuItem {
                text: qsTr("Refresh")
                enabled: !overleafClient.filesBusy
                onClicked: overleafClient.loadProjectFiles(page.projectId)
            }
        }
        delegate: ListItem {
            width: ListView.view.width
            contentHeight: Theme.itemSizeMedium
            enabled: (modelData.type === "doc" || modelData.type === "file") && !overleafClient.sourceBusy
            onClicked: page.openFile(modelData)
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * x
                truncationMode: TruncationMode.Fade
                text: modelData.path + (modelData.type === "folder" ? "/" : "")
            }
        }
        ViewPlaceholder {
            objectName: "projectFilesLoading"
            enabled: overleafClient.filesBusy
            text: qsTr("Loading project files…")
        }
        ViewPlaceholder {
            enabled: !overleafClient.filesBusy && overleafClient.projectFiles.length === 0
                     && overleafClient.filesError.length === 0
            text: qsTr("No project files")
        }
        VerticalScrollDecorator {}
        WheelScroll {
            parent: fileList
            flickable: fileList
        }
    }
}
