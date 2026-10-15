import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Switch {
    id: root
    property string label: ""
    property bool on: false
    signal flipped()
    Layout.fillWidth: true
    text: label
    checked: on
    Accessible.name: label
    onToggled: flipped()
}
