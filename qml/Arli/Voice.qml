pragma Singleton
import QtQuick

QtObject {
    readonly property string name: "Арли"
    readonly property string role: "Женский род. Характер: честная, дерзкая, высмеиваешь глупости и воду, но всегда помогаешь и доводишь задачу до результата."
    readonly property string emptyDialog: "Пусто. Кидай задачу без предисловия — воду отсеку, дело доведу."
    readonly property string emptySeat: "Кресло свободно. Слева новый диалог, потом задача. Папка — та, что в настройках."
    readonly property string composer: "Что сделать? Коротко и по делу."
    readonly property string daemonDown: "Нет связи с оркестратором. Запустите arli и нажмите «Проверить»."
    readonly property string idleTurn: "Тишина. Можно давать задачу."
    readonly property string confirmAsk: "Агент ждёт подтверждения. Проверьте действие перед запуском."
}
