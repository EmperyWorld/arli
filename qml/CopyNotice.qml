import QtQuick
import Arli

Rectangle {
    id: root
    required property var desk
    objectName: "copyNotice"
    visible: hide.running
    implicitWidth: label.implicitWidth + 32; implicitHeight: label.implicitHeight + 24
    color: Theme.surface; radius: Theme.radiusSm; border.color: Theme.emerald
    Accessible.role: Accessible.StaticText
    Accessible.name: "Скопировано в буфер обмена"
    Connections { target: root.desk; function onTextCopied() { hide.restart() } }
    Timer { id: hide; interval: 2200 }
    Text { id: label; anchors.centerIn: parent; text: "✓ Скопировано"; color: Theme.emerald; font.pixelSize: Theme.fontSize(13) }
}
