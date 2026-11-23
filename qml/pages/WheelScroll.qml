import QtQuick 2.0
import Sailfish.Silica 1.0

MouseArea {
    property var flickable
    property bool controlWheelZoom: false
    property bool trackPointer: false
    signal zoomRequested(int direction)
    signal pointerEntered()

    anchors.fill: parent
    z: 100
    acceptedButtons: Qt.NoButton
    hoverEnabled: trackPointer
    onEntered: pointerEntered()

    function moveWheel(delta) {
        var minimum = flickable.originY
        var maximum = minimum + Math.max(0, flickable.contentHeight - flickable.height)
        flickable.cancelFlick()
        flickable.contentY = Math.max(minimum,
                                      Math.min(maximum, flickable.contentY - delta))
    }
    onWheel: {
        var delta = wheel.pixelDelta.y !== 0
                    ? wheel.pixelDelta.y
                    : wheel.angleDelta.y * Theme.itemSizeSmall / 120
        if (delta === 0) {
            wheel.accepted = false
            return
        }
        if (controlWheelZoom && (wheel.modifiers & Qt.ControlModifier)) {
            zoomRequested(delta > 0 ? 1 : -1)
            wheel.accepted = true
            return
        }
        moveWheel(delta)
        wheel.accepted = true
    }
}
