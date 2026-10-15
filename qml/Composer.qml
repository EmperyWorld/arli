import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    signal hardStopRequested()
    readonly property bool canSend: desk.daemonUp && !desk.busy && desk.dialogId > 0 && desk.draft.trim().length > 0 && desk.draft.length <= 4000
    function focusInput() { input.forceActiveFocus() }
    fill: Theme.surface
    border.color: input.activeFocus ? Theme.goldDim : Theme.border
    implicitHeight: dock.implicitHeight + (Theme.dense ? 16 : 24)
    ColumnLayout {
        id: dock
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: Theme.dense ? 8 : 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Задача"; color: Theme.muted; font.pixelSize: Theme.fontSize(11); font.bold: true }
            Text { Layout.fillWidth: true; text: root.desk.settingsDirty ? "Настройки не сохранены" : (root.desk.readOnly ? "Только чтение" : "Правки разрешены"); color: root.desk.readOnly ? Theme.amber : Theme.faint; font.pixelSize: Theme.fontSize(11); horizontalAlignment: Text.AlignRight }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(Theme.dense ? 40 : 52, Math.min(140, input.contentHeight + 16))
            TextArea {
                id: input
                objectName: "composerInput"
                Accessible.name: "Задача для Арли"
                placeholderText: root.desk.dialogId > 0 ? "Опишите задачу. Enter — новая строка, Ctrl+Enter — отправить" : "Создайте или выберите диалог"
                text: root.desk.draft
                color: Theme.ink; placeholderTextColor: Theme.faint
                wrapMode: TextArea.Wrap
                font.pixelSize: Theme.fontSize(15)
                selectByMouse: true
                background: null
                onTextChanged: if (root.desk.draft !== text) root.desk.draft = text
                Keys.onPressed: (event) => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        if (root.canSend) root.desk.send()
                        event.accepted = true
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: root.desk.draft.length > 4000 ? "Сократите текст: " + root.desk.draft.length + " / 4000" :
                      (!root.desk.daemonUp ? "Нет связи с оркестратором" : (root.desk.busy ? "Черновик сохранён · можно готовить следующий запрос" : "Ctrl+Enter · отправить"))
                color: root.desk.draft.length > 4000 ? Theme.crimson : Theme.muted
                font.pixelSize: Theme.fontSize(11)
                elide: Text.ElideRight
            }
            Text { visible: root.desk.draft.length >= 3500 && root.desk.draft.length <= 4000; text: root.desk.draft.length + " / 4000"; color: Theme.amber; font.pixelSize: Theme.fontSize(11) }
            Button { visible: root.desk.busy; text: "Стоп"; onClicked: root.desk.cancel() }
            Button {
                objectName: "hardStopButton"
                visible: root.desk.busy; text: "Прервать"; flat: true
                ToolTip.visible: hovered; ToolTip.text: "Принудительно завершить процесс агента"
                onClicked: root.hardStopRequested()
            }
            Button { objectName: "sendButton"; text: "Отправить ↑"; highlighted: true; enabled: root.canSend; onClicked: root.desk.send() }
        }
    }
}
