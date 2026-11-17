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
    Notification {
        id: archiveNotification
        appName: "Abedul"
        appIcon: "ru.auroraos.OverleafClient"
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
