import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Notifications 1.0
import "pages"

ApplicationWindow {
    id: appWindow
    initialPage: Qt.resolvedUrl("pages/LoginPage.qml")
    cover: Component {
        CoverPage { navigationStack: appWindow.pageStack }
    }
    allowedOrientations: Orientation.All
    // Aurora 5.2 defaults each page, including picker subpages, to portrait.
    _defaultPageOrientations: Orientation.All
    Loader {
        id: backgroundCookies
        property bool started: false
        active: started
        source: Qt.resolvedUrl("pages/BackgroundCookieService.qml")
    }
    BackgroundLogin {
        client: overleafClient
    }
    Loader {
        id: serverRemoval
        property string requestedServer: ""
        active: false
        source: Qt.resolvedUrl("pages/ServerCookieRemovalService.qml")
        onLoaded: item.start(requestedServer)
        onStatusChanged: {
            if (status === Loader.Error) {
                overleafClient.setServerRemovalPending(false)
                overleafClient.reportAccountRemovalError(qsTr("Could not load browser cookie removal."))
                active = false
            }
        }
    }
    Connections {
        target: overleafClient
        ignoreUnknownSignals: true
        onInteractiveCookiePendingChanged: if (overleafClient.interactiveCookiePending) backgroundCookies.started = true
        onServerRemovalRequested: {
            serverRemoval.requestedServer = server
            if (serverRemoval.active)
                serverRemoval.item.start(server)
            else
                serverRemoval.active = true
        }
    }
    Notification {
        id: archiveNotification
        appName: "Abedul"
        appIcon: "ru.arpolend.abedul"
        summary: qsTr("Project ZIP downloaded")
        body: qsTr("Saved in Downloads.")
    }
    Connections {
        target: overleafClient
        onArchiveDownloaded: archiveNotification.publish()
    }
    MouseNavigation {
        objectName: "mouseNavigation"
        stack: pageStack
    }
}
