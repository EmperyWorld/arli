import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var desk
    title: "Правила диалога"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 32 : 560)
    standardButtons: Dialog.Save | Dialog.Cancel
    onOpened: editor.text = desk.dialogInstructions
    onAccepted: desk.saveDialogInstructions(editor.text)
    ColumnLayout {
        width: parent.width
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "Постоянные ограничения и требования этого диалога. Сохраняются между ходами и перезапусками. Изменения действуют для новых задач; уже отправленные используют прежние правила. Очистите поле, чтобы снять правила."
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            TextArea { id: editor; wrapMode: TextEdit.Wrap; selectByMouse: true; placeholderText: "Например: не менять публичные интерфейсы; сохранять CRLF." }
        }
        Label { text: "До 16 КиБ. Если контекст не вмещает правила, задача остановится без их обрезки."; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
}
