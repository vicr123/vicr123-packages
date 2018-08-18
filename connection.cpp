#include "connection.h"

using namespace Functions;
extern QString rootPath;
//extern Api* api;

Connection::Connection(QObject *parent) : QSslSocket(parent)
{
    req.connection = this;
    connect(this, SIGNAL(disconnected()), this, SLOT(deleteLater()));
}

void Connection::dataAvailable() {
    if (req.method == "") {
        QByteArray firstLine = this->readLine();
        QStringList headerParts = QString(firstLine).remove("\r\n").split(" ");
        if (headerParts.count() != 3) {
            Response rsp;
            rsp.statusCode = 400;
            rsp.WriteToConnection(this, "[invalid]");

            req.invalid = true;
            return;
        }

        req.method = headerParts.at(0);
        req.path = headerParts.at(1);
        req.version = headerParts.at(2);

        if (req.version != "HTTP/1.1") {
            Response rsp;
            rsp.statusCode = 505;
            rsp.WriteToConnection(this, req.path);
            req.invalid = true;
            return;
        }
        req.requestContents.append(firstLine);
    }
    req.appendBuffer(this->readAll());
}


void Connection::initiate() {
    connect(this, SIGNAL(readyRead()), this, SLOT(dataAvailable()));
}

void Connection::stopReading() {
    disconnect(this, SIGNAL(readyRead()), this, SLOT(dataAvailable()));
}

Request Connection::getRequest() {
    return req;
}

void Connection::close() {
    QSslSocket::close();
    this->deleteLater();
}

void Response::WriteToConnection(Connection *connection, QString path) const {
    if (delay > 0) {
        Response newRsp(*this);
        newRsp.delay = 0;

        QTimer::singleShot(delay, [=] {
            newRsp.WriteToConnection(connection, path);
        });
    } else {
        if (!connection->isOpen()) {
            return;
        }

        QByteArray explanation = getHTTPStatusCode(statusCode).toUtf8();
        QString logString = connection->remoteAddress() + ": HTTP " + QString::number(statusCode) + " " + explanation + " | " + connection->getRequest().method + " " + path;

        //Fill fields
        QByteArray dataOut = contents;
        if (dataOut.isEmpty() && allowEmptyContents == false) {
            dataOut.append("<html><head><title>" + QString::number(statusCode) + " " + explanation + "</title></head><body>");
            if (errorExplanation != "") {
                dataOut.append("<p>" + errorExplanation + "</p>");
            } else {
                dataOut.append(QString(QString::number(statusCode) + " " + explanation).toUtf8());
            }
            dataOut.append("<hr />vicr123 Package Distribution System</body></html>");
        }

        QMap<QString, QString> headersOut = headers;
        headersOut.insert("Content-Length", QString::number(dataOut.length()));
        headersOut.insert("Server", "vicr123BugReport/1.0");

        if (connection->getRequest().headers.contains("Accept-Encoding") && connection->getRequest().path != "/api/socket") {
            QString encoding = connection->getRequest().headers.value("Accept-Encoding");
            QStringList acceptedEncoding = encoding.split(", ");
            if (acceptedEncoding.contains("gzip")) {
                //Compress body with gzip
                dataOut = gzip(dataOut);
                headersOut.insert("Content-Length", QString::number(dataOut.length()));
                headersOut.insert("Content-Encoding", "gzip");
            }
        }

        QByteArray response = "HTTP/1.1 ";
        response.append(QString::number(statusCode));
        response.append(" " + explanation + "\r\n");

        //Begin Headers
        for (QString key : headersOut.keys()) {
            response.append(key + ": " + headersOut.value(key) + "\r\n");
        }
        response.append("\r\n");

        //Begin Body
        response.append(dataOut);

        //Send response
        int numberWritten = connection->write(response);
        if (numberWritten != response.length()) {
            warn("Bytes written not as expected: " + QString::number(numberWritten) + " written, " + QString::number(response.length()) + " expected");
        }
        connection->flush();

        if (!doNotClose) {
            connection->disconnectFromHost();
        }

        if (statusCode == 200 || statusCode == 101 || statusCode == 204) {
            log(logString);
        } else {
            warn(logString);
        }
    }
}

QString Connection::remoteAddress() {
    return "[" + peerAddress().toString() + "]";
}

Request::Request() {
}

void Request::appendBuffer(QByteArray buffer) {
    requestContents.append(buffer);
    if (stopProcess) {
        return;
    }

    this->buffer.append(buffer);
    bytesReceived += buffer.count();

    if (this->buffer.length() > 10486784) { //10 MB + 1024 bytes
        //Close connection - payload too large
        Response err;
        err.statusCode = 413;
        err.WriteToConnection(connection, path);
        return;
    }

    /*if (!buffer.endsWith("\r\n")) {
        return;
    }*/

    if (!readHeaders) {
        //Append to the body
        QByteArray dataToAppend = this->buffer.mid(dataRead);
        body.append(dataToAppend);
        dataRead += dataToAppend.length();

        bodyRead +=  dataToAppend.length();
        if (bodyRead >= headers.value("Content-Length").toInt()) {
            processCompletedRequest();
            stopProcess = true;
        }
        return;
    }

    QString req = QString(this->buffer);
    QStringList lines = req.split("\r\n");
    for (int i = 0; i < lines.count(); i++) {
        if (stopProcess) {
            break;
        }

        QString line = lines.at(i);
        if (line == "" && readHeaders) { //End of section
            if (method == "POST" || method == "PATCH") {
                readHeaders = false;
                dataRead += 2;
                appendBuffer(QByteArray());
                return;
            } else {
                processCompletedRequest();
                stopProcess = true;
            }
        } else {
            if (readHeaders) {
                if (line.indexOf(":") != -1) {
                    QString key = line.left(line.indexOf(":")).trimmed();
                    QString value = line.mid(line.indexOf(":") + 1).trimmed();

                    headers.insert(key, value);
                }
            } else {
                QByteArray itemsToAppend = QString(line + "\r\n").toUtf8();
                body.append(itemsToAppend);

                bodyRead += itemsToAppend.length();
                if (bodyRead >= headers.value("Content-Length").toInt()) {
                    processCompletedRequest();
                    stopProcess = true;
                    return;
                }
            }
        }
        dataRead += line.length() + 2;
    }
}

void Request::timeout() {
    Response err;
    err.statusCode = 408;
    err.WriteToConnection(connection, path);
}

void Request::processCompletedRequest() {
    bool isHead = false;
    if (method == "HEAD") {
        method = "GET";
        isHead = true;
    }

    QSettings settings;
    /*if (settings.value("ssl/forceSsl", false).toBool() && !connection->isEncrypted()) {
        //Redirect to HTTPS
        Response rsp;
        rsp.statusCode = 301;

        QString redirectLocation = "https://" + settings.value("www/host").toString();
        if (settings.value("ssl/sslConnectionPort", 443).toInt() != 443) {
            redirectLocation += ":" + QString::number(settings.value("ssl/sslConnectionPort", 443).toInt());
        }
        redirectLocation += path;
        rsp.headers.insert("Location", redirectLocation);

        rsp.WriteToConnection(connection, path);
        return;
    }*/

    /*if (path.startsWith("/api/")) {
        Response rsp = api->processPath(*this);

        if (isHead) {
            method = "HEAD";
            rsp.contents.clear();
            rsp.allowEmptyContents = true;
        }

        if (!rsp.doNotWrite) {
            rsp.WriteToConnection(connection, path);
        }
        return;
    }*/

    //Serve content
    if (path == "/push") {
        if (method != "POST") {
            Response err;
            err.statusCode = 405;
            err.WriteToConnection(connection, path);
            return;
        }

        QString authentication = headers.value("Authorization");
        if (!authentication.startsWith("Basic ")) {
            if (method != "POST") {
                Response err;
                err.statusCode = 401;
                err.WriteToConnection(connection, path);
                return;
            }
        }

        QByteArray authBytes = QByteArray::fromBase64(authentication.mid(6).toUtf8());
        if (authBytes != settings.value("user/auth").toString()) {
            Response err;
            err.statusCode = 401;
            err.WriteToConnection(connection, path);
            return;
        }

        if (!headers.contains("Filename")) {
            Response err;
            err.statusCode = 400;
            err.contents = "{\"Status\": \"Error\",\"Error\":\"No Filename\"}";
            err.WriteToConnection(connection, path);
            return;
        }

        QString filename = headers.value("Filename");
        if (filename.endsWith("pkg.tar.xz")) {
            //This is an Arch package
            QDir dir(rootPath + "/arch/x86_64");
            QFile file(dir.absoluteFilePath(filename));

            if (!file.open(QFile::WriteOnly)) {
                Response err;
                err.statusCode = 500;
                err.WriteToConnection(connection, path);
                return;
            }

            //Write to the file
            file.write(body);
            file.close();

            //Add to repository
            if (headers.value("DoNotAdd", "False") == "False") {
                QString repoFile = headers.value("Repository", "theapps") + ".db.tar.gz";

                QProcess* process = new QProcess();
                process->setWorkingDirectory(dir.absolutePath());
                process->start("repo-add", QStringList() << repoFile << filename);
                QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=](int exitCode, QProcess::ExitStatus exitStatus) {
                    Response err;
                    if (exitCode == 0) {
                        err.statusCode = 200;
                        err.contents = "{\"Status\": \"OK\"}";
                        good("Updated " + filename + " in Arch Linux Repository " + repoFile);
                    } else {
                        err.statusCode = 500;

                        QJsonObject errorObject;
                        errorObject.insert("Status", "Error");
                        errorObject.insert("Output", QString(process->readAll()));
                        err.contents = QJsonDocument(errorObject).toJson();

                        Functions::err("Failed to add " + filename + " to the Arch Linux repository " + repoFile);
                    }
                    err.WriteToConnection(connection, path);
                });
            } else {
                //Invalid file
                Response err;
                err.statusCode = 200;
                err.contents = "{\"Status\": \"OK\"}";
                err.WriteToConnection(connection, path);
            }
            return;
        } else {
            //Invalid file
            Response err;
            err.statusCode = 400;
            err.contents = "{\"Status\": \"Error\",\"Error\":\"Invalid File\"}";
            err.WriteToConnection(connection, path);
            return;
        }
    } else {
        if (method != "GET") {
            //Method Not Allowed
            Response err;
            err.statusCode = 405;

            if (isHead) {
                method = "HEAD";
                err.contents.clear();
                err.allowEmptyContents = true;
            }

            err.WriteToConnection(connection, path);
            return;
        }

        if (path.endsWith("/")) {
            if (path.contains('?')) {
                path = path.left(path.indexOf("?"));
            }

            QDir dir(rootPath + path);
            if (!dir.exists()) {
                Response err;
                err.statusCode = 404;

                if (isHead) {
                    method = "HEAD";
                    err.contents.clear();
                    err.allowEmptyContents = true;
                }

                err.WriteToConnection(connection, path);
                return;
            }

            QFile listingFile(settings.value("packages/dirListingFile").toString());
            if (!listingFile.exists()) {
                Functions::err("Directory Listing file not present");
                Response err;
                err.statusCode = 500;
                err.WriteToConnection(connection, path);

                if (isHead) {
                    method = "HEAD";
                    err.contents.clear();
                    err.allowEmptyContents = true;
                }

                return;
            }

            listingFile.open(QFile::ReadOnly);
            QString response = listingFile.readAll();
            listingFile.close();

            QDir::Filters filters = QDir::Dirs | QDir::Files | QDir::NoDot;
            if (path == "/") filters |= QDir::NoDotDot;

            QFileInfoList files = dir.entryInfoList(filters);
            QString dirListingEntries;
            for (QFileInfo file : files) {
                dirListingEntries += "<a href=\"" + file.fileName() + (file.isDir() ? "/" : "") + "\">" + file.fileName() + "</a><br />";
            }

            response = response.arg(path, dirListingEntries);

            Response resp;
            resp.statusCode = 200;
            resp.contents = response.toUtf8();
            resp.headers.insert("Content-Type", "text/html");

            if (isHead) {
                method = "HEAD";
                resp.contents.clear();
                resp.allowEmptyContents = true;
            }

            resp.WriteToConnection(connection, path);
            return;
        } else {
            if (path.contains('?')) {
                path = path.left(path.indexOf("?"));
            }

            QFile file(rootPath + path);
            if (!file.exists()) {
                Response err;
                err.statusCode = 404;
                err.WriteToConnection(connection, path);

                if (isHead) {
                    method = "HEAD";
                    err.contents.clear();
                    err.allowEmptyContents = true;
                }

                return;
            }

            file.open(QFile::ReadOnly);
            QByteArray buffer = file.readAll();
            file.close();

            QMimeDatabase mimeDb;
            QMimeType mimetype = mimeDb.mimeTypeForFileNameAndData(file.fileName(), buffer);

            Response resp;
            resp.statusCode = 200;
            resp.contents = buffer;
            resp.headers.insert("Content-Type", mimetype.name());

            if (isHead) {
                method = "HEAD";
                resp.contents.clear();
                resp.allowEmptyContents = true;
            }

            resp.WriteToConnection(connection, path);
            return;
        }
    }
    Response err;
    err.statusCode = 500;

    if (isHead) {
        method = "HEAD";
        err.contents.clear();
        err.allowEmptyContents = true;
    }

    err.WriteToConnection(connection, path);
}
