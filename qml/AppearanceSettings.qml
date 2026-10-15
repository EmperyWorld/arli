pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

Panel {
    implicitHeight: content.implicitHeight + 32
    ColumnLayout {
        id: content
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16
        spacing: 14
        Text { text: "Внешний вид"; color: Theme.ink; font.pixelSize: Theme.fontSize(21); font.bold: true }
        Text { Layout.fillWidth: true; text: "Применяется сразу. Сохраняется на этом компьютере."; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.fontSize(13) }
        RowLayout {
            Layout.fillWidth: true
            Button { objectName: "darkThemeButton"; text: "Тёмная"; checked: !Theme.light; onClicked: Theme.mode = "dark" }
            Button { objectName: "lightThemeButton"; text: "Светлая"; checked: Theme.light; onClicked: Theme.mode = "light" }
        }
        Text { text: "Размер текста · " + Math.round(Theme.textScale * 100) + "%"; color: Theme.ink; font.pixelSize: Theme.fontSize(13) }
        Slider {
            objectName: "textScaleSlider"
            Layout.fillWidth: true
            from: 0.9; to: 1.2; stepSize: 0.05; value: Theme.textScale
            onMoved: Theme.textScale = value
            Accessible.name: "Масштаб текста"
        }
        ToggleLine { label: "Компактные отступы"; on: Theme.dense; onFlipped: Theme.dense = !Theme.dense }
        ToggleLine { label: "Уменьшить анимации"; on: Theme.reducedMotion; onFlipped: Theme.reducedMotion = !Theme.reducedMotion }
        Button { text: "Сбросить внешний вид"; flat: true; onClicked: { Theme.mode = "dark"; Theme.textScale = 1; Theme.dense = false; Theme.reducedMotion = false } }
        Text { Layout.fillWidth: true; text: "Ширину диалогов и разбора можно менять перетаскиванием разделителей. Ctrl+\\ переключает компоновку; F11 открывает полный экран."; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: Theme.fontSize(13) }
    }
}
