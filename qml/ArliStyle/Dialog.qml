import QtQuick
import QtQuick.Templates as T
import Arli

T.Dialog {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding,
                             implicitHeaderWidth,
                             implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                              implicitContentHeight + topPadding + bottomPadding
                              + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                              + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    padding: 18
    spacing: 12

    background: Rectangle {
        radius: 18
        color: Theme.panel
        border.width: 1
        border.color: Theme.goldDim
    }

    header: Label {
        text: control.title
        visible: control.title.length > 0
        elide: Label.ElideRight
        font.pixelSize: Theme.fontSize(16)
        font.bold: true
        color: Theme.ink
        padding: 16
        leftPadding: 18
        rightPadding: 18
        background: Rectangle {
            color: "transparent"
        }
    }

    footer: DialogButtonBox {
        visible: count > 0
    }

    T.Overlay.modal: Rectangle {
        color: "#d007060b"
    }

    T.Overlay.modeless: Rectangle {
        color: "#6607060b"
    }
}
