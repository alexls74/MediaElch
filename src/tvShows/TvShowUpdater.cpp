#include "TvShowUpdater.h"

#include <QBuffer>
#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "data/Storage.h"
#include "data/TvShow.h"
#include "globals/Globals.h"
#include "globals/Manager.h"
#include "notifications/NotificationBox.h"
#include "quazip/quazip/quazip.h"
#include "quazip/quazip/quazipfile.h"
#include "scrapers/TheTvDb.h"

TvShowUpdater::TvShowUpdater(QObject *parent) : QObject(parent), m_tvdb{nullptr}
{
    for (TvScraperInterface *inter : Manager::instance()->tvScrapers()) {
        if (inter->identifier() == "tvdb") {
            m_tvdb = static_cast<TheTvDb *>(inter);
            break;
        }
    }
}

TvShowUpdater *TvShowUpdater::instance(QObject *parent)
{
    static TvShowUpdater *instance = nullptr;
    if (!instance) {
        instance = new TvShowUpdater(parent);
    }
    return instance;
}

void TvShowUpdater::updateShow(TvShow *show, bool force)
{
    if (m_updatedShows.contains(show) && !force) {
        return;
    }

    m_updatedShows.append(show);

    if (!show->episodeGuideUrl().isEmpty()) {
        QNetworkRequest request(QUrl(show->episodeGuideUrl()));
        QNetworkReply *reply = m_qnam.get(request);
        reply->setProperty("storage", Storage::toVariant(reply, show));
        connect(reply, &QNetworkReply::finished, this, &TvShowUpdater::onLoadFinished);
    } else if (show->id().isValid() || show->tvdbId().isValid()) {
        TvDbId id = !show->tvdbId().isValid() ? show->id() : show->tvdbId();
        QUrl url(QString("https://www.thetvdb.com/api/%1/series/%2/all/%3.xml")
                     .arg(m_tvdb->apiKey(), id.toString(), m_tvdb->language()));
        QNetworkReply *reply = m_qnam.get(QNetworkRequest(url));
        reply->setProperty("storage", Storage::toVariant(reply, show));
        connect(reply, &QNetworkReply::finished, this, &TvShowUpdater::onLoadFinished);
    } else {
        return;
    }

    NotificationBox::instance()->showProgressBar(
        tr("Updating TV Shows"), Constants::TvShowUpdaterProgressMessageId, true);
    int value = NotificationBox::instance()->value(Constants::TvShowUpdaterProgressMessageId);
    int maxValue = NotificationBox::instance()->maxValue(Constants::TvShowUpdaterProgressMessageId);
    NotificationBox::instance()->progressBarProgress(value, maxValue + 1, Constants::TvShowUpdaterProgressMessageId);
}

void TvShowUpdater::onLoadFinished()
{
    int value = NotificationBox::instance()->value(Constants::TvShowUpdaterProgressMessageId);
    int maxValue = NotificationBox::instance()->maxValue(Constants::TvShowUpdaterProgressMessageId);
    NotificationBox::instance()->progressBarProgress(value + 1, maxValue, Constants::TvShowUpdaterProgressMessageId);
    if (value + 1 == maxValue) {
        NotificationBox::instance()->hideProgressBar(Constants::TvShowUpdaterProgressMessageId);
    }

    auto reply = static_cast<QNetworkReply *>(QObject::sender());
    reply->deleteLater();
    TvShow *show = reply->property("storage").value<Storage *>()->show();
    if (!show) {
        return;
    }

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QString msg = (reply->url().toString().endsWith(".zip")) ? unzipContent(data) : QString::fromUtf8(data);
        m_tvdb->fillDatabaseWithAllEpisodes(msg, show);
        show->clearMissingEpisodes();
        show->fillMissingEpisodes();
    } else {
        qWarning() << "Network Error" << reply->errorString();
    }
}

QString TvShowUpdater::unzipContent(QByteArray content)
{
    QString unzippedContent;
    QBuffer buffer(&content);
    QuaZip zip(&buffer);
    if (!zip.open(QuaZip::mdUnzip)) {
        qWarning() << "Zip file could not be opened";
        return QString();
    }
    QuaZipFile file(&zip);
    for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile()) {
        if (zip.getCurrentFileName() == m_tvdb->language() + ".xml") {
            file.open(QIODevice::ReadOnly);
            QByteArray ba = file.readAll();
            file.close();
            unzippedContent = QString::fromUtf8(ba);
        }
    }
    if (zip.getZipError() != UNZ_OK) {
        qWarning() << "There was an error while uncompressing the file";
        return QString();
    }
    return unzippedContent;
}
