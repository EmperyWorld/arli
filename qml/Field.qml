import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

ColumnLayout {
    id: root
    property string label: ""
    property string placeholder: ""
    property alias text: edit.text
    property int echo: TextInput.Normal
    signal committed(string value)
    spacing: 6

    Text {
        text: root.label
        color: Theme.muted
        font.pixelSize: Theme.fontSize(12)
    }
    TextField {
        id: edit
        Layout.fillWidth: true
        placeholderText: root.placeholder
        echoMode: root.echo
        color: Theme.ink
        placeholderTextColor: Theme.faint
        font.pixelSize: Theme.fontSize(14)
        selectByMouse: true
        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.bg
            border.color: edit.activeFocus ? Theme.goldDim : Theme.border
        }
        Accessible.name: root.label
        onTextEdited: root.committed(text)
    }
}
