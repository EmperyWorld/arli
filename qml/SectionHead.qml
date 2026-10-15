import QtQuick
import QtQuick.Layouts
import Arli

RowLayout {
    id: root
    property string kicker: ""
    property string title: ""
    spacing: 10
    Rectangle {
        Layout.preferredWidth: 3
        Layout.preferredHeight: 28
        radius: 2
        color: Theme.gold
    }
    ColumnLayout {
        spacing: 0
        Text {
            text: root.kicker
            color: Theme.gold
            font.family: Theme.mono
            font.pixelSize: Theme.fontSize(10)
            font.letterSpacing: 1.8
        }
        Text {
            text: root.title
            color: Theme.ink
            font.pixelSize: Theme.fontSize(18)
            font.family: Theme.fontFamily
        }
    }
}
