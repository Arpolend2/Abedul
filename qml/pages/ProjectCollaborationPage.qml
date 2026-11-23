import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property string projectId
    property string mode: "chat"
    property var editorPage
    property string selectedThread: ""
    property bool pullRefreshPending: false
    readonly property var collaborationClient: overleafClient.collaboration

    function openHistoryTarget(target) {
        if (editorPage && editorPage.openHistoryLocation(target))
            pageStack.pop(editorPage)
    }

    function refreshFromPull() {
        if (pullRefreshPending || collaborationClient.busy) {
            flick.PullToRefresh.refreshCompleted(false)
            return
        }
        collaborationClient.refresh()
        pullRefreshPending = true
        refreshStartTimer.restart()
    }
    function finishPullRefresh() {
        if (pullRefreshPending && !collaborationClient.busy) {
            pullRefreshPending = false
            flick.PullToRefresh.refreshCompleted(collaborationClient.error.length === 0)
        }
    }
    Timer {
        id: refreshStartTimer
        interval: 0
        onTriggered: page.finishPullRefresh()
    }
    Component.onCompleted: collaborationClient.open(projectId, mode)
    Component.onDestruction: collaborationClient.close()
    Connections {
        target: collaborationClient
        onSubmitted: input.text = ""
        onChanged: page.finishPullRefresh()
    }
    SilicaListView {
        id: flick
        objectName: "collaborationList"
        anchors.fill: parent
        PullToRefresh.enabled: !collaborationClient.busy
        PullToRefresh.description: qsTr("Pull down to refresh")
        PullToRefresh.refreshHandler: function() { page.refreshFromPull() }
        model: collaborationClient.entriesModel
        header: Column {
            width: flick.width
            spacing: Theme.paddingMedium
            PageHeader {
                title: page.mode === "share" ? qsTr("Sharing")
                     : page.mode === "history" ? qsTr("History")
                     : page.mode === "comments" ? qsTr("Comments") : qsTr("Chat")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                text: page.mode === "share" ? qsTr("Invited collaborators with access to this project.")
                    : page.mode === "history" ? qsTr("Recent server history only. Restore and older history are not available here.")
                    : page.mode === "comments" ? qsTr("Select a thread to reply. New comments and document positions are not supported here; the server may not provide comments.")
                    : qsTr("Refresh to see new messages. Messages are not updated live.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                visible: collaborationClient.error.length > 0
                text: collaborationClient.error
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                visible: !!collaborationClient.notice
                text: collaborationClient.notice || ""
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: collaborationClient.busy
                text: qsTr("Loading…")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                visible: !collaborationClient.busy && !collaborationClient.error.length && collaborationClient.entries.length === 0
                text: page.mode === "share" ? qsTr("No collaborators") : qsTr("No entries")
            }
            SectionHeader {
                visible: page.mode === "share" && collaborationClient.entries.length > 0
                text: qsTr("Collaborators")
            }
        }
        delegate: BackgroundItem {
            property var entryData: entry
            width: flick.width
            height: (page.mode === "share" ? memberDetails.height
                     : page.mode === "history" ? Math.max(rowText.implicitHeight, historyChanges.height)
                     : rowText.implicitHeight) + 2 * Theme.paddingMedium
            enabled: page.mode === "comments" || page.mode === "share" || page.mode === "history"
            onClicked: {
                if (page.mode === "comments")
                    page.selectedThread = entryData.target
                else if (page.mode === "history" && page.editorPage
                         && entryData.navigation && entryData.navigation.length === 1)
                    page.openHistoryTarget(entryData.navigation[0])
            }
            Label {
                id: rowText
                x: Theme.horizontalPageMargin
                y: Theme.paddingMedium
                width: page.mode === "history" ? (parent.width - 2 * x - Theme.paddingMedium) * 0.42
                                               : parent.width - 2 * x
                wrapMode: Text.Wrap
                visible: page.mode !== "share"
                textFormat: Text.PlainText
                color: page.mode === "comments" && page.selectedThread === entryData.target
                       ? Theme.highlightColor : Theme.primaryColor
                text: entryData.body || ""
            }
            Column {
                id: historyChanges
                objectName: "historyChanges"
                x: rowText.x + rowText.width + Theme.paddingMedium
                y: Theme.paddingMedium
                width: parent.width - Theme.horizontalPageMargin - x
                spacing: Theme.paddingSmall
                visible: page.mode === "history"
                Repeater {
                    model: page.mode === "history" && !entryData.preview ? (entryData.changes || []) : []
                    delegate: Column {
                        width: historyChanges.width
                        Label {
                            width: parent.width
                            text: modelData.action
                            textFormat: Text.PlainText
                            color: modelData.kind === "remove" ? Theme.errorColor : Theme.highlightColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                        }
                        Label {
                            width: parent.width
                            text: modelData.path
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
                Repeater {
                    objectName: "historyPreviewRepeater"
                    model: page.mode === "history" ? (entryData.preview || []) : []
                    delegate: Label {
                        objectName: "historyPreview"
                        width: historyChanges.width
                        text: (modelData.kind === "i" ? "+ " : modelData.kind === "d" ? "− " : "")
                              + modelData.text
                        textFormat: Text.PlainText
                        color: modelData.kind === "d" ? Theme.errorColor
                               : modelData.kind === "i" ? Theme.highlightColor : Theme.secondaryColor
                        font.family: modelData.kind === "path" ? Theme.fontFamily : "monospace"
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.WrapAnywhere
                    }
                }
                Label {
                    width: parent.width
                    visible: !!entryData.previewError
                    text: entryData.previewError || ""
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: page.mode === "history" && entryData.navigation && entryData.navigation.length > 1
                           ? entryData.navigation : []
                    delegate: Button {
                        width: historyChanges.width
                        text: modelData.path
                        onClicked: page.openHistoryTarget(modelData)
                    }
                }
            }
            Column {
                id: memberDetails
                property bool confirmingRemoval: false
                x: Theme.horizontalPageMargin
                y: Theme.paddingMedium
                width: parent.width - 2 * x
                spacing: Theme.paddingSmall
                visible: page.mode === "share"
                Label {
                    objectName: "collaboratorName"
                    width: parent.width
                    text: entryData.name || ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    visible: !!entryData.email
                    text: entryData.email || ""
                    textFormat: Text.PlainText
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }
                Label {
                    objectName: "collaboratorAccess"
                    width: parent.width
                    text: (entryData.access || "") + (entryData.kind === "invite" ? " · " + qsTr("Pending invitation") : "")
                    color: Theme.highlightColor
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }
                Button {
                    objectName: "changeCollaboratorAccess"
                    width: Math.min(parent.width, Theme.buttonWidthLarge)
                    visible: !!entryData.id && entryData.kind === "member"
                             && (entryData.privilege === "readOnly" || entryData.privilege === "readAndWrite")
                             && !memberDetails.confirmingRemoval
                    enabled: !collaborationClient.busy
                    text: entryData.privilege === "readOnly" ? qsTr("Allow editing") : qsTr("Make read-only")
                    onClicked: collaborationClient.changeAccess(entryData.id, entryData.privilege !== "readOnly")
                }
                Button {
                    objectName: "resendInvitation"
                    width: Math.min(parent.width, Theme.buttonWidthLarge)
                    visible: !!entryData.id && entryData.kind === "invite" && !memberDetails.confirmingRemoval
                    enabled: !collaborationClient.busy
                    text: qsTr("Resend invitation")
                    onClicked: collaborationClient.resendInvite(entryData.id)
                }
                Button {
                    objectName: "removeCollaborator"
                    width: Math.min(parent.width, Theme.buttonWidthLarge)
                    visible: !!entryData.id && entryData.privilege !== "owner" && !memberDetails.confirmingRemoval
                    enabled: !collaborationClient.busy
                    text: entryData.kind === "invite" ? qsTr("Revoke invitation") : qsTr("Remove access")
                    onClicked: memberDetails.confirmingRemoval = true
                }
                Button {
                    objectName: "confirmRemoveCollaborator"
                    width: Math.min(parent.width, Theme.buttonWidthLarge)
                    visible: memberDetails.confirmingRemoval
                    enabled: !collaborationClient.busy
                    text: entryData.kind === "invite" ? qsTr("Confirm revoke") : qsTr("Confirm removal")
                    onClicked: {
                        memberDetails.confirmingRemoval = false
                        if (entryData.kind === "invite")
                            collaborationClient.revokeInvite(entryData.id)
                        else
                            collaborationClient.removeMember(entryData.id)
                    }
                }
                Button {
                    width: Math.min(parent.width, Theme.buttonWidthLarge)
                    visible: memberDetails.confirmingRemoval
                    text: qsTr("Cancel")
                    onClicked: memberDetails.confirmingRemoval = false
                }
            }
        }
        footer: Column {
            width: flick.width
            spacing: Theme.paddingMedium
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.mode === "comments" && page.selectedThread.length > 0
                text: qsTr("Selected thread: %1").arg(page.selectedThread)
                truncationMode: TruncationMode.Fade
            }
            SectionHeader {
                visible: page.mode === "share"
                text: qsTr("Invite a collaborator")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                visible: page.mode === "share"
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Inviting collaborators requires project administrator access.")
            }
            TextSwitch {
                id: readOnly
                visible: page.mode === "share"
                text: qsTr("Read-only access")
                checked: false
            }
            TextArea {
                id: input
                width: parent.width
                visible: page.mode !== "history"
                enabled: !collaborationClient.busy
                label: page.mode === "share" ? qsTr("Email") : qsTr("Message")
                placeholderText: label
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.mode !== "history"
                enabled: !collaborationClient.busy && input.text.trim().length > 0
                         && (page.mode !== "comments" || page.selectedThread.length > 0)
                text: page.mode === "share" ? qsTr("Send invitation") : qsTr("Send")
                onClicked: collaborationClient.submit(input.text, page.selectedThread, readOnly.checked)
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
        WheelScroll { parent: flick; flickable: flick }
    }
}
