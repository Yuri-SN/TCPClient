#include "tcpclient.h"

/* ServiceHeader
 * Для работы с потоками наши данные необходимо сериализовать.
 * Поскольку типы данных не стандартные перегрузим оператор << Для работы с
 * ServiceHeader
 */
QDataStream &operator>>(QDataStream &out, ServiceHeader &data)
{
    out >> data.id;
    out >> data.idData;
    out >> data.status;
    out >> data.len;

    return out;
};

QDataStream &operator<<(QDataStream &in, ServiceHeader &data)
{
    in << data.id;
    in << data.idData;
    in << data.status;
    in << data.len;

    return in;
};

QDataStream &operator<<(QDataStream &out, const StatServer &stat)
{
    out << stat.incBytes;
    out << stat.sendBytes;
    out << stat.revPck;
    out << stat.sendPck;
    out << stat.workTime;
    out << stat.clients;

    return out;
}

QDataStream &operator>>(QDataStream &in, StatServer &stat)
{
    in >> stat.incBytes;
    in >> stat.sendBytes;
    in >> stat.revPck;
    in >> stat.sendPck;
    in >> stat.workTime;
    in >> stat.clients;

    return in;
}

/*
 * Поскольку мы являемся клиентом, инициализацию сокета
 * проведем в конструкторе. Также необходимо соединить
 * сокет со всеми необходимыми нам сигналами.
 */
TCPclient::TCPclient(QObject *parent)
    : QObject(parent)
    , socket(new QTcpSocket(this))
{
    connect(socket, &QTcpSocket::readyRead, this, &TCPclient::ReadyRead);

    connect(socket, &QTcpSocket::connected, this, &TCPclient::slotConnected);
    connect(socket, &QTcpSocket::disconnected, this, &TCPclient::slotDisconnected);
}

/* write
 * Метод отправляет запрос на сервер. Сериализировать будем
 * при помощи QDataStream
 */
void TCPclient::SendRequest(ServiceHeader head)
{
    SendData(head, QString());
}

/* write
 * Такой же метод только передаем еще данные.
 */
void TCPclient::SendData(ServiceHeader head, QString str)
{
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);

    out << head;

    if (!str.isEmpty()) {
        out << str;
    }

    socket->write(block);
    socket->flush();
}

/*
 * \brief Метод подключения к серверу
 */
void TCPclient::ConnectToHost(QHostAddress host, uint16_t port)
{
    socket->connectToHost(host, port);
}

/*
 * \brief Метод отключения от сервера
 */
void TCPclient::DisconnectFromHost()
{
    if (socket->state() == QTcpSocket::ConnectedState) {
        socket->disconnectFromHost();
    }
}

void TCPclient::ReadyRead()
{
    QDataStream incStream(socket);

    if (incStream.status() != QDataStream::Ok) {
        QMessageBox msg;
        msg.setIcon(QMessageBox::Warning);
        msg.setText("Ошибка открытия входящего потока для чтения данных!");
        msg.exec();
    }

    // Читаем до конца потока
    while (incStream.atEnd() == false) {
        // Если мы обработали предыдущий пакет, мы скинули значение idData в 0
        if (servHeader.idData == 0) {
            // Проверяем количество полученных байт. Если доступных байт меньше чем
            // заголовок, то выходим из обработчика и ждем новую посылку. Каждая новая
            // посылка дописывает данные в конец буфера
            if (socket->bytesAvailable() < sizeof(ServiceHeader)) {
                return;
            } else {
                // Читаем заголовок
                incStream >> servHeader;
                // Проверяем на корректность данных. Принимаем решение по заранее
                // известному ID пакета. Если он "битый" отбрасываем все данные в
                // поисках нового ID.
                if (servHeader.id != ID) {
                    uint16_t hdr = 0;
                    while (incStream.atEnd()) {
                        incStream >> hdr;
                        if (hdr == ID) {
                            incStream >> servHeader.idData;
                            incStream >> servHeader.status;
                            incStream >> servHeader.len;
                            break;
                        }
                    }
                }
            }
        }

        // Если получены не все данные, то выходим из обработчика. Ждём новую
        // посылку
        if (socket->bytesAvailable() < servHeader.len) {
            return;
        } else {
            // Обработка данных
            ProcessingData(servHeader, incStream);
            servHeader.idData = 0;
            servHeader.status = 0;
            servHeader.len = 0;
        }
    }
}

/*
 * Остался метод обработки полученных данных. Согласно протоколу
 * мы должны прочитать данные из сообщения и вывести их в ПИ.
 * Поскольку все типы сообщений нам известны реализуем выбор через
 * switch. Реализуем получение времени.
 */
void TCPclient::ProcessingData(ServiceHeader header, QDataStream &stream)
{
    switch (header.idData) {
    case GET_TIME: {
        QDateTime time;
        stream >> time;
        emit sig_sendTime(time);
        qDebug() << "[LOG] Получено время от сервера:" << time.toString();
        break;
    }
    case GET_SIZE: {
        uint32_t freeSize = 0;
        stream >> freeSize;
        emit sig_sendFreeSize(freeSize);
        qDebug() << "[LOG] Получено свободное место на сервере:" << freeSize << "байт";
        break;
    }
    case GET_STAT: {
        StatServer stat;
        stream >> stat;
        emit sig_sendStat(stat);
        qDebug() << "[LOG] Получена статистика сервера:";
        qDebug() << "  Принято байт:" << stat.incBytes;
        qDebug() << "  Отправлено байт:" << stat.sendBytes;
        qDebug() << "  Принято пакетов:" << stat.revPck;
        qDebug() << "  Отправлено пакетов:" << stat.sendPck;
        qDebug() << "  Время работы сервера:" << stat.workTime << "сек.";
        qDebug() << "  Клиентов:" << stat.clients;
        break;
    }
    case SET_DATA: {
        QString reply;
        stream >> reply;
        emit sig_SendReplyForSetData(reply);
        qDebug() << "[LOG] Ответ сервера на SET_DATA:" << reply;
        break;
    }
    case CLEAR_DATA: {
        QString reply;
        stream >> reply;
        emit sig_SendReplyForSetData(reply);
        qDebug() << "[LOG] Сервер очистил данные. Ответ:" << reply;
        break;
    }
    default: {
        qDebug() << "[LOG] Неизвестный idData в заголовке:" << header.idData;
        emit sig_Error(ERR_NO_FUNCT);
        break;
    }
    }
}

void TCPclient::slotConnected()
{
    emit sig_connectStatus(STATUS_SUCCES);
}

void TCPclient::slotDisconnected()
{
    emit sig_Disconnected();
}
