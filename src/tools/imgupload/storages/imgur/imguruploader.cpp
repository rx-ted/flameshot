// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "imguruploader.h"
#include "utils/confighandler.h"
#include "utils/filenamehandler.h"
#include "utils/history.h"
#include "widgets/loadspinner.h"
#include "widgets/notificationwidget.h"

#include <QBuffer>
#include <QDate>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QShortcut>

static constexpr auto UPLOAD_URL = "https://picx.19981204.xyz/rest/upload";
static constexpr auto DELETE_URL = "https://picx.19981204.xyz/rest/del/";
static constexpr int REQUEST_TIMEOUT_MS = 10000;

ImgurUploader::ImgurUploader(const QPixmap& capture, QWidget* parent)
  : ImgUploaderBase(capture, parent)
{
    m_NetworkAM = new QNetworkAccessManager(this);
    connect(m_NetworkAM,
            &QNetworkAccessManager::finished,
            this,
            &ImgurUploader::handleReply);
}

void ImgurUploader::handleReply(QNetworkReply* reply)
{
    spinner()->deleteLater();
    m_currentImageName.clear();
    QJsonDocument response = QJsonDocument::fromJson(reply->readAll());
    QJsonObject json = response.object();
    int code = json[QStringLiteral("code")].toInt();
    if (reply->error() == QNetworkReply::NoError && code == 200) {
        QJsonArray dataArr = json[QStringLiteral("data")].toArray();
        if (dataArr.isEmpty()) {
            setInfoLabelText(tr("No data in response."));
            return;
        }
        QJsonObject data = dataArr.at(0).toObject();
        setImageURL(QUrl(data[QStringLiteral("url")].toString()));

        auto deleteToken = data[QStringLiteral("delToken")].toString();

        // save history
        m_currentImageName = imageURL().toString();
        int lastSlash = m_currentImageName.lastIndexOf("/");
        if (lastSlash >= 0) {
            m_currentImageName = m_currentImageName.mid(lastSlash + 1);
        }

        // save image to history
        History history;
        m_currentImageName =
          history.packFileName("imgur", deleteToken, m_currentImageName);
        history.save(pixmap(), m_currentImageName);

        emit uploadOk(imageURL());
    } else {
        QString errorMsg = json[QStringLiteral("msg")].toString();
        if (errorMsg.isEmpty()) {
            errorMsg = reply->errorString();
        }
        setInfoLabelText(errorMsg);
    }
    new QShortcut(Qt::Key_Escape, this, SLOT(close()));
}

void ImgurUploader::upload()
{
    QString apiKey = ConfigHandler().uploadClientSecret();
    if (apiKey.isEmpty()) {
        setInfoLabelText(
          tr("API key not found. Please set it in the configuration."));
        return;
    }

    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    pixmap().save(&buffer, "PNG");

    QHttpMultiPart* multiPart =
      new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral(
                         "form-data; name=\"files\"; filename=\"image.png\"")));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QVariant(QStringLiteral("image/png")));
    filePart.setBody(byteArray);
    multiPart->append(filePart);

    QString path = QDate::currentDate().toString(QStringLiteral("yyyy/MM"));
    QHttpPart pathPart;
    pathPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"path\"")));
    pathPart.setBody(path.toUtf8());
    multiPart->append(pathPart);

    QUrl url(UPLOAD_URL);
    QNetworkRequest request(url);
    request.setRawHeader("X-API-Key", apiKey.toUtf8());
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);

    QNetworkReply* reply = m_NetworkAM->post(request, multiPart);
    multiPart->setParent(reply);
}

void ImgurUploader::deleteImage(const QString& fileName,
                                const QString& deleteToken)
{
    Q_UNUSED(fileName)
    QUrl url(QString(DELETE_URL) + deleteToken);
    QNetworkRequest request(url);
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);
    QNetworkReply* reply = m_NetworkAM->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit deleteOk();
        } else {
            notification()->showMessage(tr("Failed to delete the image."));
        }
    });
}
