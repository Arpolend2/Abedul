import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page
    objectName: "projectsPage"
    allowedOrientations: Orientation.All
    backNavigation: false
    canNavigateForward: (!overleafClient.busy || overleafClient.projectsCached) && !pendingDetachedProject.length
    property bool showArchived: false
    property bool showTrash: false
    property string lastProjectId: ""
    property string navigationError: ""
    property string pendingDetachedProject: ""
    property bool pullRefreshPending: false

    function rememberProject(project) {
        lastProjectId = project.id
        if (!draftStore.saveLastProject(overleafClient.server, project.id))
            navigationError = draftStore.error
        else
            navigationError = ""
    }

    function attachLastProject() {
        var attached = pageStack.nextPage(page)
        if (attached && attached.projectId === lastProjectId)
            return
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

    function refreshProjectsFromPull() {
        if (pullRefreshPending || overleafClient.busy || overleafClient.backgroundCookiePending)
            return
        pullRefreshPending = true
        overleafClient.refreshProjects()
        pullRefreshStartTimer.restart()
    }

    function finishPullRefreshIfIdle() {
        if (!pullRefreshPending || overleafClient.busy || overleafClient.backgroundCookiePending)
            return
        pullRefreshPending = false
        projectList.PullToRefresh.refreshCompleted(overleafClient.error.length === 0)
    }

    function openCreateProjectDialog() {
        pageStack.push(createProjectDialog)
    }

    function openProjectActionDialog(project, action) {
        var editor = overleafClient.coverEditor
        if (action === "trash" && editor && editor.projectId === project.id && editor.dirty) {
            editor.saveDraft()
            if (editor.dirty) {
                navigationError = draftStore.error
                return
            }
        }
        pageStack.push(projectActionDialog, { "project": project, "action": action })
    }

    Connections {
        target: overleafClient
        ignoreUnknownSignals: true
        onProjectsChanged: if (page.status === PageStatus.Active) attachTimer.restart()
        onBusyChanged: page.finishPullRefreshIfIdle()
        onBackgroundCookiePendingChanged: page.finishPullRefreshIfIdle()
        onProjectTrashChanged: {
            if (action !== "trash" && action !== "delete")
                return
            if (page.lastProjectId === projectId) {
                page.lastProjectId = ""
                if (!draftStore.saveLastProject(overleafClient.server, ""))
                    page.navigationError = draftStore.error
            }
            if (overleafClient.coverEditor && overleafClient.coverEditor.projectId === projectId)
                overleafClient.setCoverEditor(null)
            page.pendingDetachedProject = projectId
            detachTimer.restart()
        }
    }
    Timer {
        id: detachTimer
        interval: 100
        onTriggered: {
            if (!page.pendingDetachedProject.length)
                return
            if (page.status !== PageStatus.Active || pageStack.busy || pageStack.currentPage !== page) {
                restart()
                return
            }
            var attached = pageStack.nextPage(page)
            if (attached && attached.projectId === page.pendingDetachedProject)
                pageStack.popAttached(attached, PageStackAction.Immediate)
            page.pendingDetachedProject = ""
        }
    }
    Component.onCompleted: {
        lastProjectId = draftStore.loadLastProject(overleafClient.server)
        if (draftStore.error.length)
            navigationError = draftStore.error
    }
    onStatusChanged: {
        if (status === PageStatus.Active) {
            overleafClient.coverShowsProjects = true
            attachTimer.restart()
        }
    }
    Timer {
        id: attachTimer
        interval: 0
        onTriggered: {
            if (page.status === PageStatus.Active && pageStack.currentPage === page)
                page.attachLastProject()
        }
    }
    Timer {
        id: pullRefreshStartTimer
        interval: 250
        onTriggered: page.finishPullRefreshIfIdle()
    }

    AppBar {
        id: projectsAppBar
        objectName: "projectsAppBar"
        headerText: page.showTrash ? qsTr("Trash")
                                   : page.showArchived ? qsTr("Archived projects")
                                                       : qsTr("Projects")
        maximumHeaderWidth: Theme.dp(180)

        AppBarButton {
            objectName: "createProject"
            text: qsTr("New project")
            context: qsTr("New project")
            icon.source: "image://theme/icon-m-add"
            layoutDirection: Qt.LeftToRight
            visible: !page.showArchived && !page.showTrash
            enabled: overleafClient.authenticated && overleafClient.offline !== true && !overleafClient.busy
                     && !overleafClient.projectCreationBusy
            onClicked: page.openCreateProjectDialog()
        }
        AppBarButton {
            objectName: "showArchived"
            text: page.showArchived ? qsTr("Projects") : qsTr("Archive")
            context: page.showArchived ? qsTr("Projects") : qsTr("Archive")
            icon.source: "image://theme/icon-m-file-archive-folder"
            layoutDirection: Qt.LeftToRight
            visible: !page.showTrash
            onClicked: page.showArchived = !page.showArchived
        }
        AppBarButton {
            objectName: "showTrash"
            text: page.showTrash ? qsTr("Projects") : qsTr("Trash")
            context: page.showTrash ? qsTr("Projects") : qsTr("Trash")
            icon.source: page.showTrash ? "image://theme/icon-m-file-folder"
                                         : "image://theme/icon-m-delete"
            layoutDirection: Qt.LeftToRight
            onClicked: {
                page.showTrash = !page.showTrash
                page.showArchived = false
            }
        }
        AppBarButton {
            objectName: "changeServer"
            text: qsTr("Server")
            context: qsTr("Server")
            icon.source: "image://theme/icon-m-lan"
            layoutDirection: Qt.LeftToRight
            enabled: !overleafClient.busy
            onClicked: pageStack.pop()
        }
        AppBarButton {
            objectName: "projectSettings"
            text: qsTr("Settings")
            context: qsTr("Settings")
            icon.source: "image://theme/icon-splus-setting"
            layoutDirection: Qt.LeftToRight
            onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
        }
    }

    SilicaListView {
        id: projectList
        objectName: "projectList"
        anchors.top: projectsAppBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        PullToRefresh.enabled: !overleafClient.busy && !overleafClient.backgroundCookiePending
        PullToRefresh.description: qsTr("Pull down to refresh projects")
        PullToRefresh.refreshHandler: function() { page.refreshProjectsFromPull() }
        header: Column {
            width: parent.width
            Label {
                objectName: "projectCreationError"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                visible: text.length > 0
                text: page.navigationError.length ? page.navigationError
                      : overleafClient.projectActionError.length ? overleafClient.projectActionError
                      : overleafClient.projectCreationError.length ? overleafClient.projectCreationError
                      : overleafClient.error
            }
            Label {
                objectName: "projectCreationBusy"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                visible: overleafClient.projectCreationBusy
                text: qsTr("Creating project…")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                visible: overleafClient.projectsCached
                text: overleafClient.offline === true && (overleafClient.busy || overleafClient.backgroundCookiePending)
                      ? qsTr("Connecting in the background…")
                      : overleafClient.offline === true ? qsTr("Offline: cached projects may be out of date.")
                      : overleafClient.busy ? qsTr("Showing cached projects while updating…")
                                          : qsTr("Showing cached projects. Pull down to refresh.")
            }
        }
        model: {
            var result = []
            var projects = overleafClient.projects
            for (var i = 0; i < projects.length; ++i) {
                if (page.showTrash ? projects[i].trashed
                                   : !projects[i].trashed && projects[i].archived === page.showArchived)
                    result.push(projects[i])
            }
            return result
        }
        delegate: ListItem {
            width: ListView.view.width
            contentHeight: Theme.itemSizeMedium
            enabled: !overleafClient.busy || overleafClient.projectsCached
            onClicked: {
                if (page.showTrash)
                    return
                page.rememberProject(modelData)
                page.attachLastProject()
                pageStack.navigateForward()
            }
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * x
                truncationMode: TruncationMode.Fade
                color: modelData.id === page.lastProjectId ? Theme.highlightColor : Theme.primaryColor
                text: modelData.name
            }
            menu: ContextMenu {
                MenuItem {
                    visible: !page.showTrash && !overleafClient.projectsCached && !overleafClient.busy
                    text: qsTr("Move to trash")
                    onClicked: page.openProjectActionDialog(modelData, "trash")
                }
                MenuItem {
                    visible: page.showTrash && !overleafClient.projectsCached && !overleafClient.busy
                    text: qsTr("Restore")
                    onClicked: overleafClient.changeProjectTrash(modelData.id, "restore")
                }
                MenuItem {
                    visible: page.showTrash && !overleafClient.projectsCached && !overleafClient.busy && modelData.accessLevel === "owner"
                    text: qsTr("Delete permanently")
                    onClicked: page.openProjectActionDialog(modelData, "delete")
                }
            }
        }
        ViewPlaceholder {
            objectName: "projectsLoading"
            enabled: overleafClient.busy && !overleafClient.projectsCached
            text: qsTr("Loading projects…")
        }
        ViewPlaceholder {
            enabled: !overleafClient.busy && projectList.count === 0
            text: overleafClient.error.length ? ""
                  : page.showTrash ? qsTr("Trash is empty")
                  : page.showArchived ? qsTr("No archived projects") : qsTr("No projects")
        }
        VerticalScrollDecorator {}
        WheelScroll {
            parent: projectList
            flickable: projectList
        }
    }

    Component {
        id: projectActionDialog
        Dialog {
            id: actionDialog
            property var project
            property string action
            canAccept: !overleafClient.busy
            onAccepted: overleafClient.changeProjectTrash(project.id, action)
            SilicaFlickable {
                anchors.fill: parent
                contentHeight: actionContents.height
                Column {
                    id: actionContents
                    width: parent.width
                    DialogHeader {
                        title: actionDialog.action === "trash" ? qsTr("Move to trash") : qsTr("Delete permanently")
                        acceptText: actionDialog.action === "trash" ? qsTr("Move") : qsTr("Delete")
                    }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: actionDialog.action === "trash"
                              ? qsTr("Move “%1” to the trash? You can restore it later.").arg(actionDialog.project.name)
                              : qsTr("Permanently delete “%1”? This cannot be undone.").arg(actionDialog.project.name)
                    }
                }
                VerticalScrollDecorator {}
            }
        }
    }

    Component {
        id: createProjectDialog
        Dialog {
            id: dialog
            objectName: "createProjectDialog"
            canAccept: projectName.text.trim().length > 0 && !overleafClient.projectCreationBusy
            onAccepted: overleafClient.createProject(projectName.text.trim())

            SilicaFlickable {
                anchors.fill: parent
                contentHeight: createContents.height
                Column {
                    id: createContents
                    width: parent.width
                    DialogHeader {
                        title: qsTr("New project")
                    }
                    TextField {
                        id: projectName
                        objectName: "projectNameInput"
                        width: parent.width
                        placeholderText: qsTr("Project name")
                        text: qsTr("Untitled project")
                        EnterKey.enabled: text.trim().length > 0
                        EnterKey.onClicked: dialog.accept()
                    }
                }
                VerticalScrollDecorator {}
            }
        }
    }
}
