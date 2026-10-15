pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    signal renameRequested()
    signal deleteRequested()
    signal dialogChosen()
    function focusFilter() { filter.forceActiveFocus() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12
        spacing: 10
        Button {
            Layout.fillWidth: true
            text: "+ Новый диалог"; highlighted: true
            ToolTip.visible: hovered; ToolTip.text: "Ctrl+N"
            onClicked: { root.desk.createDialog(); root.desk.page = 0 }
        }
        TextField {
            id: filter
            objectName: "dialogFilterInput"
            Layout.fillWidth: true
            placeholderText: "Поиск диалогов · Ctrl+F"
            Accessible.name: "Поиск диалогов"
            text: root.desk.dialogFilter
            selectByMouse: true
            Keys.onDownPressed: { list.currentIndex = 0; list.forceActiveFocus() }
            Keys.onEscapePressed: { root.desk.dialogFilter = ""; text = "" }
            onTextEdited: root.desk.dialogFilter = text
        }
        Button {
            Layout.fillWidth: true; visible: root.desk.dialogFilter.length > 0
            text: "Сбросить поиск"; flat: true
            onClicked: { root.desk.dialogFilter = ""; filter.text = "" }
        }
        Text { text: "ДИАЛОГИ · " + list.count; color: Theme.muted; font.pixelSize: Theme.fontSize(11); font.letterSpacing: 1.4 }
        ListView {
            id: list
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 6
            keyNavigationEnabled: true; highlightFollowsCurrentItem: true
            Keys.onReturnPressed: {
                if (currentIndex >= 0) { root.desk.openDialog(root.desk.dialogs.idAt(currentIndex)); root.desk.page = 0; root.dialogChosen() }
            }
            model: root.desk.dialogs
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: ItemDelegate {
                id: row
                required property int index
                required property var dialogId
                required property string title
                required property string updatedAt
                width: list.width; height: Theme.dense ? 56 : 70
                highlighted: dialogId === root.desk.dialogId
                Accessible.name: title
                onClicked: { list.currentIndex = index; root.desk.openDialog(dialogId); root.desk.page = 0; root.dialogChosen() }
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: row.highlighted ? Theme.goldWash : (row.hovered ? Theme.elevated : "transparent")
                    border.color: row.highlighted || row.visualFocus || (list.activeFocus && list.currentIndex === row.index) ? Theme.goldDim : "transparent"
                }
                contentItem: Column {
                    spacing: 5
                    Text { width: parent.width; text: row.title || "Без названия"; color: row.highlighted ? Theme.gold : Theme.ink; font.pixelSize: Theme.fontSize(13); elide: Text.ElideRight }
                    Text {
                        width: parent.width
                        text: {
                            const date = new Date(row.updatedAt)
                            return isNaN(date.getTime()) ? row.updatedAt : Qt.formatDateTime(date, "dd MMM · hh:mm")
                        }
                        color: Theme.faint; font.pixelSize: Theme.fontSize(11); elide: Text.ElideRight
                    }
                }
            }
            Text {
                anchors.centerIn: parent; width: parent.width - 16
                visible: list.count === 0
                text: root.desk.dialogs.loading ? "Поиск диалогов…" : root.desk.dialogFilter.length ? "Ничего не найдено. Попробуйте другой запрос." : "Создайте первый диалог, чтобы начать."
                color: Theme.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; font.pixelSize: Theme.fontSize(13)
            }
        }
        Button {
            objectName: "loadMoreDialogsButton"
            Layout.fillWidth: true
            visible: root.desk.dialogs.hasMore || root.desk.dialogs.loading || root.desk.dialogs.pageError.length > 0
            enabled: !root.desk.dialogs.loading
            text: root.desk.dialogs.loading ? "Загрузка…" : root.desk.dialogs.refreshRequired ? "Обновить список" : "Загрузить ещё"
            onClicked: root.desk.dialogs.loadMore()
        }
        Text {
            Layout.fillWidth: true; visible: root.desk.dialogs.pageError.length > 0
            text: root.desk.dialogs.pageError; color: Theme.amber
            wrapMode: Text.Wrap; font.pixelSize: Theme.fontSize(11)
        }
        RowLayout {
            Layout.fillWidth: true
            Button { Layout.fillWidth: true; text: "Правила диалога"; flat: true; enabled: root.desk.dialogId > 0; onClicked: instructionsPopup.open() }
            Button { Layout.fillWidth: true; text: "Переименовать"; flat: true; enabled: root.desk.dialogId > 0; onClicked: root.renameRequested() }
            Button { text: "×"; implicitWidth: 38; flat: true; enabled: root.desk.dialogId > 0; Accessible.name: "Удалить диалог"; ToolTip.visible: hovered; ToolTip.text: "Удалить диалог"; onClicked: root.deleteRequested() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        Text { text: "Рабочая папка"; color: Theme.faint; font.pixelSize: Theme.fontSize(10) }
        Text {
            Layout.fillWidth: true
            text: root.desk.workdir || "Рабочая папка не выбрана"
            color: Theme.muted; font.pixelSize: Theme.fontSize(11); elide: Text.ElideMiddle
        }
        Text { text: "Arli " + root.desk.versionText; color: Theme.faint; font.pixelSize: Theme.fontSize(11) }
    }
    DialogInstructions { id: instructionsPopup; desk: root.desk }
}
