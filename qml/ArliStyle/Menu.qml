import QtQuick
import QtQuick.Templates as T
import Arli

T.Menu {
    id: control
    implicitWidth: Math.max(240, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: 6
    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: false; clip: true
        currentIndex: control.currentIndex
    }
    background: Rectangle { color: Theme.surface; radius: Theme.radiusSm; border.color: Theme.border }
}
