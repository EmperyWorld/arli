pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

ColumnLayout {
    id: root
    required property var desk
    spacing: 12
    Text { Layout.alignment: Qt.AlignHCenter; text: "Ваш помощник в проекте"; color: Theme.gold; font.pixelSize: Theme.fontSize(13); font.bold: true }
    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text: root.desk.dialogId > 0 ? "Что сделаем сегодня?" : "Начните с нового диалога"
        color: Theme.ink; font.pixelSize: Theme.fontSize(28); font.bold: true
    }
    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: "Опишите результат, который хотите получить. Арли изучит файлы, предложит правки и покажет ход работы в панели «Разбор»."
        color: Theme.muted; font.pixelSize: Theme.fontSize(14)
    }
    Button {
        visible: root.desk.dialogId <= 0
        Layout.alignment: Qt.AlignHCenter
        text: "Новый диалог"; highlighted: true
        onClicked: root.desk.createDialog()
    }
    Repeater {
        model: root.desk.dialogId > 0 ? ["Изучи проект и предложи улучшения", "Найди и исправь ошибку, затем проверь результат", "Объясни, как устроен этот проект"] : []
        delegate: Button {
            required property string modelData
            Layout.fillWidth: true
            text: modelData
            onClicked: root.desk.draft = modelData
        }
    }
}
