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
        var menu = flickable.pullDownMenu
        var minimum = flickable.originY
        var maximum = minimum + Math.max(0, flickable.contentHeight - flickable.height)
        flickable.cancelFlick()
        if (menu && menu.enabled && (menu.active || (delta > 0 && flickable.contentY <= minimum))) {
            if (!menu.active)
                menu.active = true
            var position = Math.max(minimum - menu.height,
                                    Math.min(minimum, flickable.contentY - delta))
            flickable.contentY = position
            if (position >= minimum && delta < 0)
                menu.close()
            return
        }
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
