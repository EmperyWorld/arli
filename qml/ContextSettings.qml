pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Panel {
    id: root
    required property var desk
    Layout.fillWidth: true
    implicitHeight: ctxCol.implicitHeight + 28
    ColumnLayout {
        id: ctxCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 10
        Text { text: "КОНТЕКСТ"; color: Theme.gold; font.family: Theme.mono; font.pixelSize: Theme.fontSize(11); font.letterSpacing: 1.6 }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: Theme.fontSize(12)
            text: "Окно — сколько токенов принимает модель. На 80% ход сжимает раннюю часть сессии и продолжает. Шаги — сколько кругов инструментов в одном ходе. Пишутся верхней кнопкой."
        }
        GridLayout {
            columns: 2
            Label { text: "Окно, токены"; color: Theme.muted }
            SpinBox {
                from: 2048
                to: 131072
                stepSize: 1024
                value: root.desk.contextTokens
                onValueModified: { root.desk.contextTokens = value; root.desk.markDirty() }
            }
            Label { text: "Шаги хода"; color: Theme.muted }
            SpinBox {
                from: 8
                to: 256
                value: root.desk.maxSteps
                onValueModified: { root.desk.maxSteps = value; root.desk.markDirty() }
            }
        }
    }
}
