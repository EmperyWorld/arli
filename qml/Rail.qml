import QtQuick
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    fill: Theme.surface

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        StatusLamp {
            Layout.alignment: Qt.AlignHCenter
            phase: root.desk.workerState
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.desk.workerPid > 0 ? String(root.desk.workerPid) : "—"
            color: Theme.faint
            font.family: Theme.mono
            font.pixelSize: Theme.fontSize(9)
        }

        Item { Layout.preferredHeight: 8 }

        NavButton {
            Layout.fillWidth: true
            title: "Чат"
            note: "ход"
            active: root.desk.page === 0
            onClicked: root.desk.page = 0
        }
        NavButton {
            Layout.fillWidth: true
            title: "Настр."
            note: "LLM"
            active: root.desk.page === 1
            onClicked: root.desk.page = 1
        }
        NavButton {
            Layout.fillWidth: true
            title: "Она"
            note: "роль"
            active: root.desk.page === 2
            onClicked: root.desk.page = 2
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.desk.daemonUp ? "●" : "○"
            color: root.desk.daemonUp ? Theme.emerald : Theme.crimson
            font.pixelSize: Theme.fontSize(16)
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.desk.sliceText
            color: Theme.faint
            font.family: Theme.mono
            font.pixelSize: Theme.fontSize(9)
            elide: Text.ElideRight
        }
    }
}
