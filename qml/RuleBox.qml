import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

ColumnLayout {
    id: root
    property string label: ""
    property string hint: ""
    property alias text: area.text
    spacing: 6

    Text { text: root.label; color: Theme.ink; font.pixelSize: Theme.fontSize(14) }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: root.hint
        color: Theme.muted
        font.pixelSize: Theme.fontSize(12)
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 110
        TextArea {
        id: area
        selectByMouse: true
        Accessible.name: root.label
        wrapMode: TextArea.Wrap
        color: Theme.ink
        font.family: Theme.mono
        font.pixelSize: Theme.fontSize(13)
        background: Rectangle {
            color: Theme.bg
            radius: 8
            border.color: area.activeFocus ? Theme.goldDim : Theme.border
        }
        }
    }
}
