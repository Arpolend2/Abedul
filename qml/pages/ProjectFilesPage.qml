import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string projectId
    property string projectName
    property var editorPage

    Component.onCompleted: overleafClient.loadProjectFiles(projectId)
    Component.onDestruction: overleafClient.closeProjectFiles()

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
                text: qsTr("Read-only file list. Creating, renaming and deleting files is not available yet.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                visible: overleafClient.filesError.length > 0
                text: overleafClient.filesError
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
            enabled: modelData.type === "doc" && !overleafClient.sourceBusy
            onClicked: {
                if (page.editorPage.openProjectFile(modelData.path))
                    pageStack.pop()
            }
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
