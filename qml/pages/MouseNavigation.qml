import QtQuick 2.0

MouseArea {
    property var stack
    anchors.fill: parent
    z: 100
    acceptedButtons: Qt.BackButton | Qt.ForwardButton

    function navigate(button) {
        if (!stack || stack.busy)
            return
        if (button === Qt.BackButton && stack.backNavigation)
            stack.navigateBack()
        else if (button === Qt.ForwardButton && stack.forwardNavigation)
            stack.navigateForward()
    }
    onClicked: navigate(mouse.button)
}
