import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Arli

pragma ComponentBehavior: Bound

Flickable {
    contentWidth: width
    contentHeight: col.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 16

        Panel {
            Layout.fillWidth: true
            implicitHeight: hero.implicitHeight + 36
            fill: Theme.goldWash
            border.color: Theme.goldDim
            ColumnLayout {
                id: hero
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 18
                spacing: 8
                Text {
                    text: "АРЛИ"
                    color: Theme.gold
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontSize(12)
                    font.letterSpacing: 4
                }
                Text {
                    text: Voice.name
                    color: Theme.ink
                    font.pixelSize: Theme.fontSize(40)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Локальный агент. Говорит о себе в женском роде и не отпускает задачу, пока та не сделана."
                    color: Theme.muted
                    font.pixelSize: Theme.fontSize(15)
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            implicitHeight: roleCol.implicitHeight + 32
            ColumnLayout {
                id: roleCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 8
                Text {
                    text: "РОЛЬ"
                    color: Theme.gold
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontSize(11)
                    font.letterSpacing: 1.8
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: Voice.role
                    color: Theme.ink
                    font.pixelSize: Theme.fontSize(20)
                    lineHeight: 1.35
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 720 ? 3 : 1
            columnSpacing: 12
            rowSpacing: 12

            Repeater {
                model: [
                    ["Честная", "Не делает вид, что готово, если не готово. Ошибку инструмента говорит прямо и чинит следующим шагом."],
                    ["Дерзкая", "Глупый план и пустые обороты не гладит. Режет воду и называет вещи своими именами."],
                    ["До результата", "Помогает до конца. Не бросает ход на красивом плане, если файлы ещё не приведены в порядок."]
                ]
                delegate: Panel {
                    id: traitPanel
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: trait.implicitHeight + 28
                    ColumnLayout {
                        id: trait
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 6
                        Text {
                            text: traitPanel.modelData[0]
                            color: Theme.gold
                            font.pixelSize: Theme.fontSize(18)
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: traitPanel.modelData[1]
                            color: Theme.muted
                            font.pixelSize: Theme.fontSize(14)
                            lineHeight: 1.35
                        }
                    }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            implicitHeight: rules.implicitHeight + 28
            ColumnLayout {
                id: rules
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 8
                Text {
                    text: "КАК РАБОТАЕТ"
                    color: Theme.gold
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontSize(11)
                    font.letterSpacing: 1.8
                }
                Repeater {
                    model: [
                        "Файлы трогает только внутри workdir. Перед правкой оставляет копию в .arli-bak.",
                        "Команду shell не запускает молча: ждёт карточку «разрешить» или «отклонить».",
                        "Мысль перед инструментом короткая. Длинная вода в ответе — против роли.",
                        "Если инструмент ошибся, следующий вызов она поправляет сама, ход не бросает."
                    ]
                    delegate: Text {
                        required property string modelData
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "—  " + modelData
                        color: Theme.ink
                        font.pixelSize: Theme.fontSize(14)
                    }
                }
            }
        }
    }
}
