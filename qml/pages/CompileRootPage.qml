import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string projectId
    property var editorPage
    property bool compileAfterSelection: false
    property bool pullRefreshPending: false

    function refreshFromPull() {
        if (pullRefreshPending || overleafClient.filesBusy) {
            list.PullToRefresh.refreshCompleted(false)
            return
        }
        overleafClient.loadProjectFiles(page.projectId)
        pullRefreshPending = true
        refreshStartTimer.restart()
    }
    function finishPullRefresh() {
        if (pullRefreshPending && !overleafClient.filesBusy) {
            pullRefreshPending = false
            list.PullToRefresh.refreshCompleted(overleafClient.filesError.length === 0)
        }
    }
    Connections {
        target: overleafClient
        onFilesBusyChanged: page.finishPullRefresh()
    }
    Timer {
        id: refreshStartTimer
        interval: 0
        onTriggered: page.finishPullRefresh()
    }

    Component.onCompleted: overleafClient.loadProjectFiles(projectId)
    Component.onDestruction: overleafClient.closeProjectFiles()

    SilicaListView {
        id: list
        objectName: "compileRootList"
        anchors.fill: parent
        PullToRefresh.enabled: !overleafClient.filesBusy
        PullToRefresh.description: qsTr("Pull down to refresh")
        PullToRefresh.refreshHandler: function() { page.refreshFromPull() }
        header: Column {
            width: parent.width
            PageHeader { title: qsTr("Choose main document") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                visible: overleafClient.filesError.length > 0
                text: overleafClient.filesError
            }
        }
        model: {
            var result = []
            var files = overleafClient.projectFiles
            for (var i = 0; i < files.length; ++i) {
                if (files[i].type === "doc")
                    result.push(files[i])
            }
            return result
        }
        delegate: ListItem {
            width: ListView.view.width
            contentHeight: Theme.itemSizeMedium
            onClicked: {
                page.editorPage.compileRoot = modelData.path
                if (page.compileAfterSelection)
                    overleafClient.compileProjectPdf(page.projectId, modelData.path)
                pageStack.pop()
            }
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * x
                truncationMode: TruncationMode.Fade
                text: modelData.path
            }
        }
        ViewPlaceholder {
            enabled: !overleafClient.filesBusy && list.count === 0
                     && overleafClient.filesError.length === 0
            text: qsTr("No documents found")
        }
        VerticalScrollDecorator {}
        WheelScroll {
            parent: list
            flickable: list
        }
    }
}
