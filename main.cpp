#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>

#include "playerwindow.h"

namespace {
constexpr const char *kSingleInstanceServerName = "Deed.SingleInstance";
constexpr const char *kRaiseCommand = "__DEED_RAISE__";

QStringList audioFilesFromArguments(const QStringList &arguments)
{
    QStringList files;

    for (int i = 1; i < arguments.size(); ++i) {
        const QFileInfo info(arguments.at(i));
        if (!info.exists() || !info.isFile()) {
            continue;
        }

        const QString suffix = info.suffix().toLower();
        if (suffix == "mp3" || suffix == "wav" || suffix == "ogg" || suffix == "flac") {
            files.append(info.absoluteFilePath());
        }
    }

    return files;
}

bool sendCommandToRunningInstance(const QStringList &files, int connectTimeoutMs = 150)
{
    QLocalSocket socket;
    socket.connectToServer(kSingleInstanceServerName);
    if (!socket.waitForConnected(connectTimeoutMs)) {
        return false;
    }

    QByteArray payload;
    if (files.isEmpty()) {
        payload = QByteArray(kRaiseCommand);
    } else {
        payload = files.join(QStringLiteral("\n")).toUtf8();
    }

    socket.write(payload);
    if (!socket.waitForBytesWritten(500)) {
        socket.disconnectFromServer();
        return false;
    }

    socket.flush();
    socket.disconnectFromServer();
    return true;
}

bool startSingleInstanceServer(QLocalServer &server, const QStringList &launchFiles)
{
    if (server.listen(kSingleInstanceServerName)) {
        return true;
    }

    if (server.serverError() == QAbstractSocket::AddressInUseError) {
        if (sendCommandToRunningInstance(launchFiles, 250)) {
            return false;
        }

        qWarning() << "Stale local server detected, removing socket for" << kSingleInstanceServerName;
        QLocalServer::removeServer(kSingleInstanceServerName);

        if (server.listen(kSingleInstanceServerName)) {
            return true;
        }
    }

    qWarning() << "Failed to start single-instance server:"
               << server.errorString();
    return false;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QStringList launchFiles = audioFilesFromArguments(app.arguments());
    if (sendCommandToRunningInstance(launchFiles)) {
        return 0;
    }

    QLocalServer server;
    if (!startSingleInstanceServer(server, launchFiles)) {
        if (sendCommandToRunningInstance(launchFiles, 250)) {
            return 0;
        }
        return 1;
    }

    PlayerWindow window;
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window]() {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, &window, [socket, &window]() {
                const QString payload = QString::fromUtf8(socket->readAll()).trimmed();
                if (payload.isEmpty() || payload == QString::fromUtf8(kRaiseCommand)) {
                    window.show();
                    window.raise();
                    window.activateWindow();
                    return;
                }

                const QStringList files = payload.split(QStringLiteral("\n"), Qt::SkipEmptyParts);
                window.handleCommandLineLaunch(files, true);
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        }
    });

    window.resize(700, 500);
    window.show();
    window.handleCommandLineLaunch(launchFiles, false);

    return app.exec();
}
