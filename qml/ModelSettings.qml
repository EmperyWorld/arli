pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    Layout.fillWidth: true
    implicitHeight: modelCol.implicitHeight + 28
    ColumnLayout {
        id: modelCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 10
        Text { text: "МОДЕЛЬ"; color: Theme.gold; font.family: Theme.mono; font.pixelSize: Theme.fontSize(11); font.letterSpacing: 1.6 }
        Field { label: "Адрес"; text: root.desk.llmBaseUrl; onCommitted: (v) => { root.desk.llmBaseUrl = v; root.desk.markDirty() } }
        Field { label: "Имя модели"; text: root.desk.llmModel; onCommitted: (v) => { root.desk.llmModel = v; root.desk.markDirty() } }
        ComboBox {
            Layout.fillWidth: true
            model: ["gemma4", "qwen3", "generic"]
            currentIndex: root.desk.llmProfile === "qwen3" ? 1 : root.desk.llmProfile === "generic" ? 2 : 0
            onActivated: (idx) => { root.desk.llmProfile = model[idx]; root.desk.markDirty() }
        }
        Field {
            label: "Температура: −1 — профиль, 0 — минимум случайности, до 2"
            text: String(root.desk.llmTemperature)
            onCommitted: (value) => {
                const number = Number(value.replace(",", "."))
                if (value.trim().length > 0 && Number.isFinite(number) && (number === -1 || (number >= 0 && number <= 2))) {
                    root.desk.llmTemperature = number
                    root.desk.markDirty()
                }
            }
        }
        RowLayout {
            Chip {
                label: root.desk.llmReachable ? "LLM на связи" : "LLM не видна"
                ink: root.desk.llmReachable ? Theme.emerald : Theme.amber
                wash: Theme.bg
            }
            Chip {
                label: root.desk.hasApiKey ? "ключ задан" : "ключа нет"
                ink: root.desk.hasApiKey ? Theme.emerald : Theme.amber
                wash: Theme.bg
            }
            Item { Layout.fillWidth: true }
            Button { text: "Стереть"; onClicked: root.desk.clearApiKey() }
        }
        Field {
            id: keyField
            label: "Новый ключ"
            placeholder: "Не показывается после записи"
            echo: TextInput.Password
        }
        Button {
            text: "Сохранить ключ"
            enabled: keyField.text.trim().length > 0
            onClicked: {
                root.desk.setApiKey(keyField.text)
                keyField.text = ""
            }
        }
        Button { text: "Проверить связь"; enabled: root.desk.daemonUp && !root.desk.settingsDirty; onClicked: root.desk.refresh() }
    }
}
