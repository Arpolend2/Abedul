import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    backNavigation: false
    property bool showArchived: false
    property string lastProjectId: ""
    property string navigationError: ""

    function rememberProject(project) {
        lastProjectId = project.id
        if (!draftStore.saveLastProject(overleafClient.server, project.id))
            navigationError = draftStore.error
        else
            navigationError = ""
    }

    function attachLastProject() {
        for (var i = 0; i < overleafClient.projects.length; ++i) {
            var project = overleafClient.projects[i]
            if (project.id === lastProjectId && !project.trashed) {
                pageStack.pushAttached(Qt.resolvedUrl("EditorPage.qml"), {
                    "projectId": project.id,
                    "projectName": project.name,
                    "deferOpen": true
                })
                return
            }
        }
    }

    Component.onCompleted: {
        lastProjectId = draftStore.loadLastProject(overleafClient.server)
        if (draftStore.error.length)
            navigationError = draftStore.error
    }
    onStatusChanged: if (status === PageStatus.Active) attachTimer.restart()
    Timer {
        id: attachTimer
        interval: 0
        onTriggered: {
            if (page.status === PageStatus.Active && pageStack.currentPage === page)
                page.attachLastProject()
        }
    }

    SilicaListView {
        id: projectList
        objectName: "projectList"
        anchors.fill: parent
        header: Column {
            width: parent.width
            PageHeader { title: page.showArchived ? qsTr("Archived projects") : qsTr("Projects") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                visible: text.length > 0
                text: page.navigationError.length ? page.navigationError : overleafClient.error
            }
        }
        model: {
            var result = []
            var projects = overleafClient.projects
            for (var i = 0; i < projects.length; ++i) {
                if (!projects[i].trashed && projects[i].archived === page.showArchived)
                    result.push(projects[i])
            }
            return result
        }
        PullDownMenu {
            MenuItem {
                objectName: "projectSettings"
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                objectName: "changeServer"
                text: qsTr("Pick a different server")
                enabled: !overleafClient.busy
                onClicked: pageStack.pop()
            }
            MenuItem {
                text: page.showArchived ? qsTr("Show projects") : qsTr("Show archived projects")
                onClicked: page.showArchived = !page.showArchived
            }
        }
        delegate: ListItem {
            width: ListView.view.width
            contentHeight: Theme.itemSizeMedium
            onClicked: {
                page.rememberProject(modelData)
                pageStack.push(Qt.resolvedUrl("EditorPage.qml"), {
                    "projectId": modelData.id,
                    "projectName": modelData.name
                })
            }
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * x
                truncationMode: TruncationMode.Fade
                color: modelData.id === page.lastProjectId ? Theme.highlightColor : Theme.primaryColor
                text: modelData.name
            }
        }
        ViewPlaceholder {
            objectName: "projectsLoading"
            enabled: overleafClient.busy
            text: qsTr("Loading projects…")
        }
        ViewPlaceholder {
            enabled: !overleafClient.busy && projectList.count === 0
            text: overleafClient.error.length ? ""
                  : page.showArchived ? qsTr("No archived projects") : qsTr("No projects")
        }
        VerticalScrollDecorator {}
        WheelScroll {
            parent: projectList
            flickable: projectList
        }
    }
}
