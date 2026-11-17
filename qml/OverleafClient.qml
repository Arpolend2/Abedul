import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"

ApplicationWindow {
    initialPage: Qt.resolvedUrl("pages/LoginPage.qml")
    cover: Qt.resolvedUrl("pages/CoverPage.qml")
    allowedOrientations: Orientation.All
    MouseNavigation {
        objectName: "mouseNavigation"
        stack: pageStack
    }
}
