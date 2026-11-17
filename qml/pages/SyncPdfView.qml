// SPDX-FileCopyrightText: 2022-2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.0
import Sailfish.Silica 1.0
import ru.auroraos.amberpdf 1.0

Rectangle {
    id: root

    property alias count: pdfView.count
    property alias documentProvider: pdfView.documentProvider
    property alias currentIndex: pdfView.currentIndex
    property alias catchBound: pdfView.catchBound
    property alias orientation: pdfView.orientation
    property alias annotationsPaint: pdfView.annotationsPaint
    property alias notesPaint: pdfView.notesPaint
    property alias zoom: pdfView.itemScale
    property alias contentTopMargin: pdfView.contentTopMargin
    property alias pdfPath: pdfView.pdfPath
    property alias pageRange: pdfView.pageRange
    property alias status: pdfView.status
    property alias contentX: viewFlick.contentX
    property alias contentY: viewFlick.contentY
    property alias bookmarksModel: pdfView.bookmarksModel
    property alias searchPhraseModel: pdfView.searchPhraseModel
    property alias fileVersion: pdfView.fileVersion
    property alias documentEdited: pdfView.documentEdited
    property alias pagesWithNotesModel: pdfView.pagesWithNotesModel
    property alias pageNumberComponent: pdfView.pageNumberComponent
    property alias grayScaleRendering: pdfView.grayScaleRendering
    property alias pageAspectRatio: pdfView.pageAspectRatio
    property alias reverse: pdfView.reverse
    property alias pageCheckComponent: pdfView.pageCheckComponent
    property alias hiddenPagesOpacity: pdfView.hiddenPagesOpacity
    property alias specialPagesCount: pdfView.specialPagesCount
    property alias pageSpacing: pdfView.pageSpacing
    property alias maxPageIndex: pdfView.maxPageIndex
    property alias minPageIndex: pdfView.minPageIndex
    property alias fitToPage: pdfView.fitToPage
    property alias centering: pdfView.centering
    property alias userBookmarksModel: pdfView.userBookmarksModel
    property bool pinchingActive: false
    property var geometry
    property int syncHighlightPage: -1
    property point syncHighlightPoint: Qt.point(0, 0)
    signal syncToSource(int pageNumber, real h, real v)
    signal syncFailure(string message)
    property int lastSavedPdfViewZCoordinate: 0

    property var painter: paintArea
    property string lineColor: "red"
    property real lineWidth: 2

    property bool interactive: true
    property bool drawMode: false
    property bool textHighlightMode: false
    property bool noteCreatingMode: false
    property string textHighlightColor: "#e3e601"
    property real chosenPage
    property bool pageChoosing: false

    property real pageWidth
    property real pageHeight

    property bool needGoToPage
    property bool isPagedView: false
    property int deviceOrientation: Qt.Vertical

    property bool isScrolling: false

    signal clicked
    signal clickedUrl(string url)
    signal noteActivate(string noteText, string author, int pageIndex, int annotationId, string color, string noteType)
    signal contentChanged()
    signal annotationAdded()
    signal documentSaved(bool saveStatus)
    signal holding(var lastMouse, var screenCoordinates)
    signal noteRemoved(int pageIndex, int noteId, bool result)
    signal createNote(var screenCoordinates)

    function syncPointAt(point) {
        if (!geometry || !geometry.ready) {
            syncFailure(qsTr("PDF page geometry is not ready."))
            return
        }
        var viewportX = point.x - viewFlick.contentX
        var viewportY = point.y - viewFlick.contentY
        for (var index = 0; index < pdfView.count; ++index) {
            var size = geometry.page(index)
            if (!size.width || !size.height)
                continue
            var width = pdfView.getActualPageWidth(index) * pdfView.itemScale
            var height = pdfView.getActualPageHeight(index) * pdfView.itemScale
            if (!(width > 0) || !(height > 0))
                continue
            var x = width <= viewFlick.width ? (viewFlick.width - width) / 2 : -viewFlick.contentX
            var y = pdfView.contentTopMargin + pdfView.getPageStart(index) - Math.max(0, viewFlick.contentY)
            if (viewportX < x || viewportX > x + width || viewportY < y || viewportY > y + height)
                continue
            if (!size.supported) {
                syncFailure(qsTr("SyncTeX is unavailable for this PDF page's crop box."))
                return
            }
            var dx = (viewportX - x) * size.width / width
            var dy = (viewportY - y) * size.height / height
            var h = dx
            var v = dy
            if (size.rotation === 1) {
                h = dy
                v = size.width - dx
            } else if (size.rotation === 2) {
                h = size.width - dx
                v = size.height - dy
            } else if (size.rotation === 3) {
                h = size.height - dy
                v = dx
            }
            syncToSource(index + 1, h, v)
            return
        }
        syncFailure(qsTr("Select a point on a PDF page."))
    }

    function showSyncPosition(position) {
        var index = position.page - 1
        var size = geometry ? geometry.page(index) : null
        if (!geometry || !geometry.ready || index < 0 || index >= pdfView.count || !size.width || !size.height) {
            syncFailure(qsTr("PDF page geometry is not ready."))
            return
        }
        if (!size.supported) {
            syncFailure(qsTr("SyncTeX is unavailable for this PDF page's crop box."))
            return
        }
        var h = position.h
        var v = position.v
        var dx = h
        var dy = v
        if (size.rotation === 1) {
            dx = size.width - v
            dy = h
        } else if (size.rotation === 2) {
            dx = size.width - h
            dy = size.height - v
        } else if (size.rotation === 3) {
            dx = v
            dy = size.height - h
        }
        if (dx < 0 || dx > size.width || dy < 0 || dy > size.height) {
            syncFailure(qsTr("The SyncTeX position is outside the PDF page."))
            return
        }
        var width = pdfView.getActualPageWidth(index) * pdfView.itemScale
        var height = pdfView.getActualPageHeight(index) * pdfView.itemScale
        if (!(width > 0) || !(height > 0)) {
            syncFailure(qsTr("PDF page geometry is not ready."))
            return
        }
        viewFlick.contentY = pdfView.contentTopMargin + pdfView.getPageStart(index)
                            + dy * height / size.height - viewFlick.height / 2
        if (width > viewFlick.width)
            viewFlick.contentX = dx * width / size.width - viewFlick.width / 2
        viewFlick.returnToBounds()
        syncHighlightPage = index
        syncHighlightPoint = Qt.point(dx / size.width, dy / size.height)
        syncHighlightTimer.restart()
    }

    function startDrawing(pageIndex) {
        prepareToDrawing(pageIndex);
        paintArea.lineWidth = root.lineWidth;
        drawMode = true;
        interactive = false;
        lastSavedPdfViewZCoordinate = pdfView.z;
        pdfView.z = -1;
    }

    function adaptToOrientation() {
        paintArea.scalePoints(pdfView.getActualPageWidth(root.chosenPage) / root.pageWidth, pdfView.getActualPageHeight(root.chosenPage) / root.pageHeight);
        root.pageWidth = pdfView.getActualPageWidth(root.chosenPage);
        root.pageHeight = pdfView.getActualPageHeight(root.chosenPage);
        updateBoundRectPosition();
        if (root.needGoToPage) {
            goToPage(root.chosenPage);
            root.needGoToPage = false;
        }
    }

    function updateBoundRectPosition() {
        var xBias = 0;
        var yBias = 0;

        if (!isPagedView) {
            yBias = root.contentTopMargin;
            xBias = root.pageSpacing;
        }

        if (pdfView.orientation === Qt.Vertical) {
            boundRect.y = pdfView.getPageStart(root.chosenPage) + yBias;
        } else {
            boundRect.x = pdfView.getPageStart(root.chosenPage) + xBias;
        }

        boundRect.width = root.pageWidth * pdfView.itemScale;
        boundRect.height = root.pageHeight * pdfView.itemScale;
    }

    function stopDrawing() {
        drawMode = false;
        interactive = true;
        pdfView.z = lastSavedPdfViewZCoordinate;
    }

    function getPageHeight(pageIndex) {
        return pdfView.getPageHeight(pageIndex)
    }

    function getPageStart(pageIndex) {
        return pdfView.getPageStart(pageIndex)
    }

    function prepareToDrawing(pageIndex) {
        root.chosenPage = pageIndex;
        root.pageWidth = pdfView.getActualPageWidth(root.chosenPage);
        root.pageHeight = pdfView.getActualPageHeight(root.chosenPage);
        updateBoundRectPosition();
    }

    function startTextHighlighting(pageIndex) {
        prepareToDrawing(pageIndex);
        textHighlightMode = true;
        interactive = false;
    }

    function stopTextHighlight() {
        textHighlightMode = false;
        interactive = true;
    }

    function correctPosition() {
        viewFlick.contentY = pdfView.contentY
        viewFlick.contentX = pdfView.contentX

        viewFlick.returnToBounds()
    }

    function goToPage(pageIndex) {
        if (pdfView.getActualPageHeight(pageIndex) * pdfView.itemScale <= viewFlick.height) {
            viewPositionBlocker.start()
            pdfView.positionViewAtIndex(pageIndex)
        }
    }

    function goToNextPage() {
        if (isScrolling)
            return
        scrollToPage(currentIndex + 1)
    }

    function goToPreviousPage() {
        if (isScrolling)
            return
        if (currentIndex - 1 < minPageIndex)
            scrollToPage(maxPageIndex)
        else
            scrollToPage(currentIndex - 1)
    }

    function scrollToPage(pageIndex) {
        isScrolling = true
        viewPositionBlocker.start()
        pdfView.scrollViewToIndex(pageIndex)
        isScrolling = false
    }

    function saveDocumentAs(path) {
        pdfView.saveDocumentAs(path)
    }

    function addUserBookmarkForPage(pageNumber, pageText) {
        pdfView.addUserBookmark(pageNumber, pageText);
    }

    function getPageNumberByPoint(point) {
        point.x += viewFlick.contentX;
        point.y += viewFlick.contentY;
        return pdfView.getPageByPoint(point) + 1;
    }

    function getStartingTextByPage(pageNumber) {
        return pdfView.getStartingTextByPage(pageNumber - 1);
    }

    function deleteUserBookmarkForPage(pageNumber) {
        pdfView.deleteUserBookmark(pageNumber);
    }

    function getPageByPoint(point) {
        point.x += viewFlick.contentX
        point.y += viewFlick.contentY
        return pdfView.getPageByPoint(point)
    }

    /**
    * Supports Square, HighLight, Text note types only
    */
    function addAnnotationAtPoint(point, author, content, color, noteType) {
        pdfView.addAnnotationAtPoint(point, color, author, content, noteType,
                                     boundRect.width / pdfView.itemScale,
                                     boundRect.height / pdfView.itemScale,
                                     root.chosenPage)
    }

    /**
    * For drawing Ink-annotations
    */
    function drawInkAnnotation(author, content) {
        if (paintArea.lines.length <= 0)
            return;
        for (var i = 0; i < paintArea.lines.length; i++) {
            pdfView.drawInkAnnotation(paintArea.lines[i].points, paintArea.lines[i].color, root.lineWidth, author, content,
                                      boundRect.width / pdfView.itemScale,
                                      boundRect.height / pdfView.itemScale,
                                      root.chosenPage);
        }
    }
    /**
    * Supports Square, HighLight, Text note types only
    */
    function addAnnotation(rect, author, content, color, noteType) {
        rect.x += viewFlick.contentX
        rect.y += viewFlick.contentY
        pdfView.addAnnotation(rect, color, author, content, noteType)
    }
    function removeNote(pageIndex, noteIndex) {
        pdfView.removeNote(pageIndex, noteIndex)
    }
    function editNote(pageIndex, noteIndex, newColor, newContent, noteType) {
        pdfView.editNote(pageIndex, noteIndex, newContent, newColor, noteType)
    }
    function findPhrase(str) {
        pdfView.findPhrase(str);
    }
    function cancelSearch() {
        pdfView.cancelSearch();
    }
    function isPageInPainted(pageNumber) {
        return pdfView.isPageInPainted(pageNumber);
    }
    function startNoteCreating(pageNumber) {
        prepareToDrawing(pageNumber);
        noteCreatingMode = true;
        interactive = false;
    }
    function stopNoteCreating() {
        noteCreatingMode = false;
        interactive = true;
    }
    function highlightNextSearchResult() {
        pdfView.highlightNextSearchResult();
    }
    function highlightPreviousSearchResult() {
        pdfView.highlightPreviousSearchResult();
    }
    function findSelectedSearchResult(index) {
        pdfView.findSelectedSearchResult(index);
    }
    function getCurrentSearchResult() {
        return pdfView.getCurrentSearchResult();
    }
    function getSearchResultCountString() {
        return pdfView.getSearchResultCountString();
    }
    function clearSearchResults() {
        pdfView.clearSearchResults();
    }

    color: "transparent"
    data: [
    Rectangle {
        parent: root
        visible: root.syncHighlightPage >= 0 && syncHighlightTimer.running
        z: 10
        width: 14
        height: 14
        radius: 7
        color: "#e83c42"
        border.color: "white"
        border.width: 2
        x: {
            if (root.syncHighlightPage < 0)
                return 0
            var w = pdfView.getActualPageWidth(root.syncHighlightPage) * pdfView.itemScale
            return (w <= viewFlick.width ? (viewFlick.width - w) / 2 : -viewFlick.contentX)
                    + root.syncHighlightPoint.x * w - width / 2
        }
        y: root.syncHighlightPage < 0 ? 0
             : pdfView.contentTopMargin + pdfView.getPageStart(root.syncHighlightPage)
               - Math.max(0, viewFlick.contentY)
               + root.syncHighlightPoint.y * pdfView.getActualPageHeight(root.syncHighlightPage)
                 * pdfView.itemScale - height / 2
    },
    Timer {
        id: syncHighlightTimer
        interval: 3000
    },
        PdfViewPrivate {
            id: pdfView

            parent: viewFlick
            height: parent.height
            width: parent.width
            anchors.top: parent.top

            z: drawMode || textHighlightMode || noteCreatingMode || pageChoosing ? -1 : 0
            isPagedView: root.isPagedView

            onWidthChanged: {
                viewPositionBlocker.start()
                root.correctPosition()
            }
            onHeightChanged: {
                viewPositionBlocker.start()
                root.correctPosition()
            }
            onContentChanged: {
                root.correctPosition()
                root.contentChanged()
            }
            onContentHeightChanged: {
                if (root.drawMode || root.textHighlightMode || root.noteCreatingMode) {
                    root.adaptToOrientation();
                }
            }

            onOrientationChanged: {
                root.correctPosition()
            }
            onItemScaleChanged: root.correctPosition()
            onClickedUrl: root.clickedUrl(url)
            onClickedGoToPage: root.correctPosition()
            onNoteActivated: root.noteActivate(noteText, author, pageIndex, annotationId, color, noteType)
            onDocumentSaved: root.documentSaved(saveStatus)
            onNoteRemoved: root.noteRemoved(pageIndex, noteId, result)
            onAnnotationAdded: root.annotationAdded()
            onPageSwapAnimationEnded: {
                viewFlick.contentY = pdfView.contentY
                viewFlick.contentX = pdfView.contentX
            }
        },

        SilicaFlickable {
            id: viewFlick

            property var fixedScaleValues: isPagedView ? [1.0, 1.5, 2] : [0.5, 1.0, 1.5, 2]
            property real maximumItemScale: fixedScaleValues[fixedScaleValues.length - 1]
            property real minimumItemScale: fixedScaleValues[0]
            property real maximumPinchScale: root.drawMode ? 3.5 : 6.0
            property real storedRootContentPosition: 0
            property real previousContentX: 0
            property int previousPageIndex: 0
            property real standartMaxVelocity: 1

            interactive: root.interactive

            function scaleAroundPoint(center, newScale) {
                pdfView.scaleAroundPoint(center, newScale)

                if (isPagedView)
                    holdViewAtPage();
            }
            function clicked(mouse) {
                var screenX = mouse.x - contentX
                var screenY = mouse.y - contentY

                pdfView.clicked(Qt.point(mouse.x, mouse.y))

                root.clicked()
            }
            function doubleClicked(mouse) {
                var currentScale = fixedScaleValues.indexOf(Math.round(pdfView.itemScale * 100) / 100)

                if (currentScale === -1) {
                    scaleAroundPoint(Qt.point(mouse.x, mouse.y), 1.0)
                    return
                }

                var newScale = fixedScaleValues[Math.max(1, (++currentScale) % fixedScaleValues.length)]
                scaleAroundPoint(Qt.point(mouse.x, mouse.y), newScale)
            }
            function holdViewAtPage() {
                var itemScale = pdfView.itemScale;
                var scaledPageWidth = pdfView.getActualPageWidth(currentIndex) * itemScale;
                var scaledSpacing = pageSpacing * itemScale;
                var viewWidth = width;

                var pageStartX = currentIndex * (scaledPageWidth + scaledSpacing);
                var pageEndX = pageStartX + scaledPageWidth;

                var newLeftEdge = contentX;
                var newRightEdge = contentX + viewWidth;

                if (newLeftEdge < pageStartX) {
                    contentX = pageStartX;
                } else if (newRightEdge > pageEndX) {
                    contentX = pageEndX - viewWidth;
                }
            }

            contentHeight: Math.max(height, (pdfView.orientation === Qt.Vertical
                                    ? pdfView.contentHeight * pdfView.itemScale + pdfView.lastPageIndent
                                    : pdfView.contentHeight))
            contentWidth: Math.max(width, (pdfView.orientation === Qt.Vertical
                                   ? pdfView.contentWidth
                                   : pdfView.contentWidth * pdfView.itemScale))
            flickableDirection: Flickable.AutoFlickDirection
            anchors.fill: parent

            Component.onCompleted: standartMaxVelocity = maximumFlickVelocity

            onContentYChanged: {
                if (viewPositionBlocker.running)
                    return

                pdfView.contentY = contentY
            }
            onContentXChanged: {
                if (viewPositionBlocker.running)
                    return

                pdfView.contentX = contentX
            }
            onMovementStarted: {
                if (!isPagedView)
                    return;

                previousPageIndex = currentIndex;
                previousContentX = contentX;
            }
            onMovementEnded: {
                if (!isPagedView)
                    return;

                var itemScale = pdfView.itemScale;
                var scaledPageWidth = pdfView.getActualPageWidth(currentIndex) * itemScale;
                var scaledSpacing = pageSpacing * itemScale;
                var viewWidth = width;

                var pageStartX = previousPageIndex * (scaledPageWidth + scaledSpacing);
                var pageEndX = pageStartX + scaledPageWidth;

                var leftEdgeX = contentX;
                var rightEdgeX = contentX + viewWidth;

                var overscrollLeft = pageStartX - leftEdgeX;
                var overscrollRight = rightEdgeX - pageEndX;

                var threshold = 0.3 * viewWidth;
                if (root.deviceOrientation !== Orientation.Portrait) {
                    threshold = 0.15 * viewWidth;
                }

                if (overscrollRight > threshold) {
                    scrollToPage(previousPageIndex + 1);
                    return;
                }

                if (overscrollLeft > threshold) {
                    scrollToPage(previousPageIndex - 1);
                    return;
                }

                if (overscrollRight > 0) {
                    scrollAnimation.to = pageEndX - viewWidth;
                    scrollAnimation.start();
                } else if (overscrollLeft > 0) {
                    scrollAnimation.to = pageStartX;
                    scrollAnimation.start();
                }
            }

            onQuickScrollAnimatingChanged: {
                if (quickScrollAnimating) {
                    switch (root.orientation) {
                    case Qt.Horizontal:
                        viewFlick.storedRootContentPosition = contentX
                        break
                    case Qt.Vertical:
                        viewFlick.storedRootContentPosition = contentY
                        break
                    }
                } else {
                    switch (root.orientation) {
                    case Qt.Horizontal:
                        if (contentX < viewFlick.storedRootContentPosition) {
                            root.goToPage(0)
                        } else if (contentX > viewFlick.storedRootContentPosition) {
                            root.goToPage(root.count - 1)
                        }
                        break
                    case Qt.Vertical:
                        if (contentY < viewFlick.storedRootContentPosition) {
                            root.goToPage(0)
                        } else if (contentY > viewFlick.storedRootContentPosition) {
                            root.goToPage(root.count - 1)
                        }
                        break
                    }
                }
            }

            PropertyAnimation {
                id: scrollAnimation

                target: viewFlick
                duration: 100
                property: "contentX"
            }

            Timer {
                id: viewPositionBlocker

                interval: 100
                repeat: false

                onTriggered: root.correctPosition()
            }

            ScrollDecorator {  }

            PinchArea {
                id: pinchArea

                property real startScale
                property bool moving: false
                property int pinchTimerDelay: 800
                property bool pinching: false

                anchors.fill: parent
                enabled: (pdfView.status === PdfDocument.Ready) && root.interactive

                onMovingChanged: if (moving) root.moving()
                onPinchStarted: {
                    afterPinchDelayTimer.stop();
                    root.pinchingActive = true;

                    if (viewFlick.moving)
                        viewFlick.cancelFlick()
                    pinching = true
                    startScale = pdfView.itemScale
                }
                onPinchUpdated: {
                    viewFlick.scaleAroundPoint(pinch.center,
                                               Math.max(viewFlick.minimumItemScale,
                                                        Math.min(viewFlick.maximumPinchScale, startScale * pinch.scale)))
                }
                onPinchFinished: {
                    pinching = false;
                    paintArea.scale = pdfView.itemScale / startScale;
                    updateBoundRectPosition();
                    viewFlick.returnToBounds();
                    viewFlick.returnToBounds()
                    afterPinchDelayTimer.start();
                }

                Timer {
                    id: afterPinchDelayTimer

                    running: false
                    repeat: false
                    interval: parent.pinchTimerDelay
                    onTriggered: {
                        root.pinchingActive = false;
                    }
                }

                MouseArea {
                    id: mouseArea

                    anchors.fill: parent
                    enabled: !root.drawMode
                    property point lastMouse
                    property double deltaX: 10.0
                    property double deltaY: 10.0
                    property int doubleClickLatency: 350
                    property int holdLatency: 800

                    signal click(var lastMouse)
                    signal doubleClick(var lastMouse)
                    signal hold(var lastMouse, var screenCoordinates)

                    onClick: viewFlick.clicked(lastMouse)
                    onDoubleClick:  {
                        if (!root.drawMode)
                            root.syncPointAt(lastMouse)
                    }
                    onHold: root.holding(lastMouse, screenCoordinates)
                    onPressed: {
                        lastMouse = Qt.point(mouseX, mouseY)
                        holdTimer.start()
                    }
                    onReleased: {
                        lastMouse = Qt.point(mouseX, mouseY)

                        if (doubleClickTimer.running && holdTimer.running
                                && Math.abs(mouseX - doubleClickTimer.lastMouse.x) <= deltaX
                                && Math.abs(mouseY - doubleClickTimer.lastMouse.y) <= deltaY) {
                            doubleClickTimer.stop()
                            doubleClick(lastMouse)

                            holdTimer.stop()

                            return
                        }

                        if (holdTimer.running) {
                            holdTimer.stop()
                            doubleClickTimer.lastMouse = lastMouse
                            doubleClickTimer.start()
                        } else {
                            doubleClickTimer.stop()
                        }
                    }
                    onMouseXChanged: {
                        if (!holdTimer.running)
                            return

                        if (Math.abs(mouseX - lastMouse.x) > deltaX)
                            holdTimer.stop()
                    }
                    onMouseYChanged: {
                        if (!holdTimer.running)
                            return

                        if (Math.abs(mouseY - lastMouse.y) > deltaY)
                            holdTimer.stop()
                    }

                    Timer {
                        id: doubleClickTimer

                        property var lastMouse

                        interval: mouseArea.doubleClickLatency

                        onTriggered: mouseArea.click(mouseArea.lastMouse)
                    }
                    Timer {
                        id: holdTimer

                        interval: mouseArea.holdLatency
                        repeat: false

                        onTriggered: {
                            if (doubleClickTimer.running)
                                doubleClickTimer.stop()

                            mouseArea.hold(mouseArea.lastMouse,
                                           Qt.point(mouseArea.lastMouse.x - viewFlick.contentX,
                                                    mouseArea.lastMouse.y - viewFlick.contentY))
                        }
                    }
                }
            }
        },

        Rectangle {
            id: boundRect

            parent: pinchArea
            anchors {
                horizontalCenter: (pdfView.orientation === Qt.Vertical ? parent.horizontalCenter : undefined)
                verticalCenter: (pdfView.orientation === Qt.Vertical ? undefined : parent.verticalCenter)
            }
            visible: (root.drawMode || root.textHighlightMode || root.noteCreatingMode) && !pinchArea.pinching
            color: "transparent"
            border.color: "red"
            border.width: 10

            PainterArea {
                id: paintArea

                enabled: root.drawMode && !root.interactive
                pauseDrawing: root.interactive
                anchors.fill: parent
            }

            MouseArea {
                property int pressX
                property int pressY

                anchors.fill: parent
                enabled: root.textHighlightMode && !root.interactive

                onPressed: {
                    pressX = mouseX
                    pressY = mouseY
                }
                onReleased: {
                    pdfView.highlightText(Qt.point(pressX, pressY), Qt.point(mouseX, mouseY), textHighlightColor, root.chosenPage);
                    textHighlightPreview.model = [];
                }
                onPositionChanged: {
                    textHighlightPreview.model = pdfView.findTextRects(Qt.point(pressX, pressY), Qt.point(mouseX, mouseY), root.chosenPage);
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.noteCreatingMode && !root.interactive

                onPressAndHold: createNote(Qt.point(mouseX, mouseY))
            }

            Repeater {
                id: textHighlightPreview

                anchors.fill: parent

                Rectangle {
                    x: modelData.x
                    y: modelData.y
                    width: modelData.width
                    height: modelData.height
                    color: "blue"
                    opacity: 0.2
                }
            }
        }
    ]
    onLineColorChanged: paintArea.lineColor = lineColor;
    onLineWidthChanged: {
        paintArea.lineWidth = lineWidth;
    }
    onIsPagedViewChanged: {
        if (isPagedView) {
            viewFlick.maximumFlickVelocity = 1;
        } else {
            viewFlick.maximumFlickVelocity = viewFlick.standartMaxVelocity;
        }
    }
}
