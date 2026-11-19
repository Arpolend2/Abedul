import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    id: cover
    property var navigationStack
    property var editorPage: overleafClient.coverEditor
    readonly property bool showProjectList: !editorPage || overleafClient.coverShowsProjects
    readonly property bool showOnlyPdf: !!editorPage && !editorPage.coverSideBySide
                                         && !!editorPage.attachedPdfPage && !!navigationStack
                                         && navigationStack.currentPage === editorPage.attachedPdfPage
    property string lastOpenedProjectId: ""
    property bool wasVisible: false
    property string navigationError: ""
    property var visibleProjects: {
        var result = []
        var projects = overleafClient.projects
        for (var i = 0; i < projects.length; ++i) {
            if (!projects[i].trashed)
                result.push(projects[i])
        }
        return result
    }

    OpenCachedPdf { id: externalPdf }

    Component.onCompleted: {
        if (overleafClient.userId === undefined || overleafClient.userId.length)
            lastOpenedProjectId = draftStore.loadLastProject(overleafClient.server)
        if (editorPage)
            lastOpenedProjectId = editorPage.projectId
    }
    onEditorPageChanged: {
        if (editorPage)
            lastOpenedProjectId = editorPage.projectId
    }
    Connections {
        target: overleafClient
        ignoreUnknownSignals: true
        onServerChanged: {
            if (overleafClient.userId === undefined || overleafClient.userId.length)
                cover.lastOpenedProjectId = draftStore.loadLastProject(overleafClient.server)
            else
                cover.lastOpenedProjectId = ""
        }
        onAccountChanged: {
            if (overleafClient.userId.length)
                cover.lastOpenedProjectId = draftStore.loadLastProject(overleafClient.server)
        }
    }
    onStatusChanged: {
        if (status === Cover.Active)
            wasVisible = true
    }
    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationActive && cover.wasVisible) {
                cover.wasVisible = false
                cover.openCoverPage()
            }
        }
    }

    function openCoverPage(showProjects) {
        if (showProjects === undefined)
            showProjects = showProjectList
        var stack = navigationStack
        if (!stack) {
            navigationError = qsTr("Project navigation is not available.")
            return false
        }
        if (stack.busy) {
            navigationError = qsTr("Wait for the current page transition.")
            return false
        }
        var projects = stack.find(function(page) { return page.objectName === "projectsPage" })
        if (showProjects) {
            if (!projects)
                return false
            if (stack.currentPage !== projects) {
                if (editorPage && editorPage.dirty) {
                    editorPage.saveDraft()
                    if (editorPage.dirty) {
                        navigationError = draftStore.error
                        return false
                    }
                }
                stack.pop(projects, PageStackAction.Immediate)
            }
        } else {
            if (!editorPage)
                return false
            if (stack.currentPage === projects && stack.nextPage(projects) === editorPage) {
                stack.navigateForward(PageStackAction.Immediate)
            } else if (stack.currentPage !== editorPage && !showOnlyPdf) {
                var editor = stack.find(function(page) { return page === cover.editorPage })
                if (!editor) {
                    navigationError = qsTr("The open project is no longer available.")
                    return false
                }
                stack.pop(editor, PageStackAction.Immediate)
            }
        }
        navigationError = ""
        return true
    }

    function switchCover(showProjects) {
        if (openCoverPage(showProjects))
            overleafClient.coverShowsProjects = showProjects
    }

    Column {
        id: coverContent
        objectName: "coverContent"
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: cover.editorPage ? cover.coverActionArea.top : parent.bottom
            leftMargin: Theme.paddingSmall
            rightMargin: Theme.paddingSmall
            topMargin: Theme.paddingSmall
            bottomMargin: cover.editorPage ? Theme.paddingSmall : Theme.paddingMedium
        }
        spacing: Theme.paddingSmall
        clip: true

        Label {
            objectName: "coverStatus"
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.highlightColor
            visible: text.length > 0
            text: cover.navigationError.length ? cover.navigationError
                  : externalPdf.error.length ? externalPdf.error
                  : cover.editorPage && !cover.showProjectList && cover.editorPage.coverPdfError
                    ? cover.editorPage.coverPdfError
                  : cover.editorPage && !cover.showProjectList && overleafClient.archiveProject === cover.editorPage.projectId
                    ? (overleafClient.archiveError.length ? overleafClient.archiveError
                       : overleafClient.archiveBusy ? qsTr("Downloading ZIP…")
                       : overleafClient.archivePath.length ? qsTr("ZIP saved in Downloads") : "")
                    : ""
        }
        Item {
            id: snippetArea
            width: parent.width
            height: Math.max(snippet.visible ? snippet.height : 0, pdfPeek.visible ? pdfPeek.height : 0)
            visible: !!cover.editorPage && !cover.showProjectList

            Label {
                id: snippet
                objectName: "coverSnippet"
                visible: !cover.showOnlyPdf
                width: pdfPeek.visible ? (parent.width - Theme.paddingSmall) / 2 : parent.width
                x: pdfPeek.visible && overleafClient.pdfOnLeft ? parent.width - width : 0
                text: cover.editorPage && !cover.showProjectList ? cover.editorPage.coverSnippet : ""
                textFormat: Text.PlainText
                font.family: "monospace"
                font.pixelSize: Math.round(Theme.fontSizeExtraSmall * 0.6)
                wrapMode: Text.NoWrap
                clip: true
            }
            Image {
                id: pdfPeek
                objectName: "coverPdfPeek"
                visible: !!cover.editorPage && !cover.showProjectList && !!cover.editorPage.coverPdfSnapshot
                         && (cover.editorPage.coverSideBySide || cover.showOnlyPdf)
                width: cover.showOnlyPdf ? parent.width : (parent.width - Theme.paddingSmall) / 2
                height: Math.max(0, coverContent.height - snippetArea.y)
                x: cover.showOnlyPdf || overleafClient.pdfOnLeft ? 0 : parent.width - width
                source: visible ? cover.editorPage.coverPdfSnapshot.url : ""
                fillMode: Image.PreserveAspectCrop
                clip: true
                horizontalAlignment: Image.AlignHCenter
                verticalAlignment: Image.AlignTop
                // ItemGrabResult URLs exist only in Qt Quick's image cache.
                cache: true
                smooth: true
            }
        }
        Repeater {
            objectName: "coverProjects"
            model: cover.editorPage && !cover.showProjectList ? [] : cover.visibleProjects
            Label {
                objectName: "coverProject-" + index
                width: parent.width
                text: modelData.name
                color: modelData.id === cover.lastOpenedProjectId ? Theme.highlightColor : Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                truncationMode: TruncationMode.Fade
            }
        }
        Label {
            width: parent.width
            visible: (!cover.editorPage || cover.showProjectList) && cover.visibleProjects.length === 0
            text: overleafClient.busy ? qsTr("Loading projects…") : qsTr("No projects")
            font.pixelSize: Theme.fontSizeExtraSmall
            wrapMode: Text.Wrap
        }
    }

    CoverActionList {
        enabled: !!cover.editorPage && !cover.showProjectList
                 && overleafClient.pdfPath.length > 0 && !overleafClient.pdfBusy
        CoverAction {
            objectName: "coverShowProjects"
            iconSource: "image://theme/icon-cover-previous"
            onTriggered: cover.switchCover(true)
        }
        CoverAction {
            objectName: "coverOpenPdf"
            iconSource: "image://theme/icon-splus-cover-pdf"
            onTriggered: externalPdf.open()
        }
        CoverAction {
            objectName: "coverDownloadZip"
            iconSource: "image://theme/icon-splus-downloads"
            onTriggered: if (!overleafClient.archiveBusy)
                             overleafClient.downloadProjectArchive(cover.editorPage.projectId)
        }
    }
    CoverActionList {
        enabled: !!cover.editorPage && !cover.showProjectList
                 && (!overleafClient.pdfPath.length || overleafClient.pdfBusy)
        CoverAction {
            objectName: "coverShowProjectsWithoutPdf"
            iconSource: "image://theme/icon-cover-previous"
            onTriggered: cover.switchCover(true)
        }
        CoverAction {
            objectName: "coverDownloadZipWithoutPdf"
            iconSource: "image://theme/icon-splus-downloads"
            onTriggered: if (!overleafClient.archiveBusy)
                             overleafClient.downloadProjectArchive(cover.editorPage.projectId)
        }
    }
    CoverActionList {
        enabled: !!cover.editorPage && cover.showProjectList && !overleafClient.busy
        CoverAction {
            objectName: "coverShowEditor"
            iconSource: "image://theme/icon-cover-next"
            onTriggered: cover.switchCover(false)
        }
    }
}
