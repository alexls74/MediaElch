#include "XbmcXml.h"

#include "data/Movie.h"
#include "data/TvShow.h"
#include "data/TvShowEpisode.h"
#include "globals/Globals.h"
#include "globals/Helper.h"
#include "globals/Manager.h"
#include "image/Image.h"
#include "mediaCenterPlugins/kodi/ArtistXmlReader.h"
#include "mediaCenterPlugins/kodi/ArtistXmlWriter.h"
#include "mediaCenterPlugins/kodi/ConcertXmlReader.h"
#include "mediaCenterPlugins/kodi/ConcertXmlWriter.h"
#include "mediaCenterPlugins/kodi/EpisodeXmlReader.h"
#include "mediaCenterPlugins/kodi/MovieXmlReader.h"
#include "mediaCenterPlugins/kodi/MovieXmlWriter.h"
#include "mediaCenterPlugins/kodi/TvShowXmlReader.h"
#include "mediaCenterPlugins/kodi/TvShowXmlWriter.h"
#include "settings/Settings.h"

#include <QApplication>
#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QXmlStreamWriter>

/**
 * @brief XbmcXml::XbmcXml
 * @param parent
 */
XbmcXml::XbmcXml(QObject *parent)
{
    setParent(parent);
}

/**
 * @brief XbmcXml::~XbmcXml
 */
XbmcXml::~XbmcXml() = default;

/**
 * @brief Checks if our MediaCenterPlugin supports a feature
 * @param feature Feature to check
 * @return Feature is supported or not
 */
bool XbmcXml::hasFeature(int feature)
{
    Q_UNUSED(feature);
    return true;
}

QByteArray XbmcXml::getMovieXml(Movie *movie)
{
    Kodi::MovieXmlWriter writer(*movie);
    return writer.getMovieXml();
}

/**
 * @brief Saves a movie (including images)
 * @param movie Movie to save
 * @return Saving success
 * @see XbmcXml::writeMovieXml
 */
bool XbmcXml::saveMovie(Movie *movie)
{
    qDebug() << "Entered, movie=" << movie->name();
    QByteArray xmlContent = getMovieXml(movie);

    if (movie->files().empty()) {
        qWarning() << "Movie has no files";
        return false;
    }

    movie->setNfoContent(xmlContent);

    bool saved = false;
    QFileInfo fi(movie->files().at(0));
    for (auto dataFile : Settings::instance()->dataFiles(DataFileType::MovieNfo)) {
        QString saveFileName = dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, movie->files().count() > 1);
        QString saveFilePath = fi.absolutePath() + "/" + saveFileName;
        QDir saveFileDir = QFileInfo(saveFilePath).dir();
        if (!saveFileDir.exists()) {
            saveFileDir.mkpath(".");
        }
        QFile file(saveFilePath);
        qDebug() << "Saving to" << file.fileName();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "File could not be openend";
        } else {
            file.write(xmlContent);
            file.close();
            saved = true;
        }
    }
    if (!saved) {
        return false;
    }

    for (const auto imageType : Movie::imageTypes()) {
        DataFileType dataFileType = DataFile::dataFileTypeForImageType(imageType);
        if (movie->images().imageHasChanged(imageType) && !movie->images().image(imageType).isNull()) {
            for (DataFile dataFile : Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName =
                    dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, movie->files().count() > 1);
                if (imageType == ImageType::MoviePoster
                    && (movie->discType() == DiscType::BluRay || movie->discType() == DiscType::Dvd)) {
                    saveFileName = "poster.jpg";
                }
                if (imageType == ImageType::MovieBackdrop
                    && (movie->discType() == DiscType::BluRay || movie->discType() == DiscType::Dvd)) {
                    saveFileName = "fanart.jpg";
                }
                QString path = getPath(movie);
                saveFile(path + "/" + saveFileName, movie->images().image(imageType));
            }
        }

        if (movie->images().imagesToRemove().contains(imageType)) {
            for (DataFile dataFile : Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName =
                    dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, movie->files().count() > 1);
                if (imageType == ImageType::MoviePoster
                    && (movie->discType() == DiscType::BluRay || movie->discType() == DiscType::Dvd)) {
                    saveFileName = "poster.jpg";
                }
                if (imageType == ImageType::MovieBackdrop
                    && (movie->discType() == DiscType::BluRay || movie->discType() == DiscType::Dvd)) {
                    saveFileName = "fanart.jpg";
                }
                QString path = getPath(movie);
                QFile(path + "/" + saveFileName).remove();
            }
        }
    }

    if (movie->inSeparateFolder() && !movie->files().isEmpty()) {
        for (const QString &file : movie->images().extraFanartsToRemove()) {
            QFile::remove(file);
        }
        QDir dir(QFileInfo(movie->files().first()).absolutePath() + "/extrafanart");
        if (!dir.exists() && !movie->images().extraFanartToAdd().isEmpty()) {
            QDir(QFileInfo(movie->files().first()).absolutePath()).mkdir("extrafanart");
        }
        for (QByteArray img : movie->images().extraFanartToAdd()) {
            int num = 1;
            while (QFileInfo(dir.absolutePath() + "/" + QString("fanart%1.jpg").arg(num)).exists()) {
                ++num;
            }
            saveFile(dir.absolutePath() + "/" + QString("fanart%1.jpg").arg(num), img);
        }
    }

    for (const Actor &actor : movie->actors()) {
        if (!actor.image.isNull()) {
            QDir dir;
            dir.mkdir(fi.absolutePath() + "/" + ".actors");
            QString actorName = actor.name;
            actorName = actorName.replace(" ", "_");
            saveFile(fi.absolutePath() + "/" + ".actors" + "/" + actorName + ".jpg", actor.image);
        }
    }

    for (Subtitle *subtitle : movie->subtitles()) {
        if (subtitle->changed()) {
            QString subFileName = fi.completeBaseName();
            if (!subtitle->language().isEmpty()) {
                subFileName.append("." + subtitle->language());
            }
            if (subtitle->forced()) {
                subFileName.append(".forced");
            }

            QStringList newFiles;
            for (const QString &subFile : subtitle->files()) {
                QFileInfo subFi(fi.absolutePath() + "/" + subFile);
                QString newFileName = subFileName + "." + subFi.suffix();
                QFile f(fi.absolutePath() + "/" + subFile);
                if (f.rename(fi.absolutePath() + "/" + newFileName)) {
                    newFiles << newFileName;
                } else {
                    qWarning() << "Could not rename" << subFi.absoluteFilePath() << "to"
                               << fi.absolutePath() + "/" + newFileName;
                    newFiles << subFi.fileName();
                }
            }
            subtitle->setFiles(newFiles);
        }
    }

    Manager::instance()->database()->update(movie);

    return true;
}

/**
 * @brief Tries to find an nfo file for the movie
 * @param movie Movie
 * @return Path to nfo file, if none found returns an empty string
 */
QString XbmcXml::nfoFilePath(Movie *movie)
{
    QString nfoFile;
    if (movie->files().empty()) {
        qWarning() << "Movie has no files";
        return nfoFile;
    }
    QFileInfo fi(movie->files().at(0));
    if (!fi.isFile()) {
        qWarning() << "First file of the movie is not readable" << movie->files().at(0);
        return nfoFile;
    }

    for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::MovieNfo)) {
        QString file = dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, movie->files().count() > 1);
        QFileInfo nfoFi(fi.absolutePath() + "/" + file);
        if (nfoFi.exists()) {
            nfoFile = fi.absolutePath() + "/" + file;
            break;
        }
    }

    return nfoFile;
}

QString XbmcXml::nfoFilePath(TvShowEpisode *episode)
{
    QString nfoFile;
    if (episode->files().empty()) {
        qWarning() << "Episode has no files";
        return nfoFile;
    }
    QFileInfo fi(episode->files().at(0));
    if (!fi.isFile()) {
        qWarning() << "First file of the episode is not readable" << episode->files().at(0);
        return nfoFile;
    }

    for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::TvShowEpisodeNfo)) {
        QString file = dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, episode->files().count() > 1);
        QFileInfo nfoFi(fi.absolutePath() + "/" + file);
        if (nfoFi.exists()) {
            nfoFile = fi.absolutePath() + "/" + file;
            break;
        }
    }

    return nfoFile;
}

QString XbmcXml::nfoFilePath(TvShow *show)
{
    QString nfoFile;
    if (show->dir().isEmpty()) {
        qWarning() << "Show dir is empty";
        return nfoFile;
    }

    for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::TvShowNfo)) {
        QFile file(show->dir() + "/" + dataFile.saveFileName(""));
        if (file.exists()) {
            nfoFile = file.fileName();
            break;
        }
    }

    return nfoFile;
}

/**
 * @brief Tries to find an nfo file for the concert
 * @param concert Concert
 * @return Path to nfo file, if none found returns an empty string
 */
QString XbmcXml::nfoFilePath(Concert *concert)
{
    QString nfoFile;
    if (concert->files().empty()) {
        qWarning() << "Concert has no files";
        return nfoFile;
    }
    QFileInfo fi(concert->files().at(0));
    if (!fi.isFile()) {
        qWarning() << "First file of the concert is not readable" << concert->files().at(0);
        return nfoFile;
    }

    for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::ConcertNfo)) {
        QString file = dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, concert->files().count() > 1);
        QFileInfo nfoFi(fi.absolutePath() + "/" + file);
        if (nfoFi.exists()) {
            nfoFile = fi.absolutePath() + "/" + file;
            break;
        }
    }

    return nfoFile;
}

/**
 * @brief Loads movie infos (except images)
 * @param movie Movie to load
 * @return Loading success
 */
bool XbmcXml::loadMovie(Movie *movie, QString initialNfoContent)
{
    movie->clear();
    movie->setChanged(false);

    QString nfoContent;
    if (initialNfoContent.isEmpty()) {
        QString nfoFile = nfoFilePath(movie);
        if (nfoFile.isEmpty()) {
            return false;
        }

        QFile file(nfoFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "File" << nfoFile << "could not be opened for reading";
            return false;
        }
        nfoContent = QString::fromUtf8(file.readAll());
        movie->setNfoContent(nfoContent);
        file.close();
    } else {
        nfoContent = initialNfoContent;
    }

    QDomDocument domDoc;
    domDoc.setContent(nfoContent);

    Kodi::MovieXmlReader reader(*movie);
    reader.parseNfoDom(domDoc);

    movie->setStreamDetailsLoaded(loadStreamDetails(movie->streamDetails(), domDoc));

    // Existence of images
    if (initialNfoContent.isEmpty()) {
        for (const auto imageType : Movie::imageTypes()) {
            movie->images().setHasImage(imageType, !imageFileName(movie, imageType).isEmpty());
        }
        movie->images().setHasExtraFanarts(!extraFanartNames(movie).isEmpty());
    }

    return true;
}

/**
 * @brief Loads the stream details from the dom document
 * @param streamDetails StreamDetails object
 * @param domDoc Nfo document
 * @return Infos loaded
 */
bool XbmcXml::loadStreamDetails(StreamDetails *streamDetails, QDomDocument domDoc)
{
    streamDetails->clear();
    if (!domDoc.elementsByTagName("streamdetails").isEmpty()) {
        QDomElement elem = domDoc.elementsByTagName("streamdetails").at(0).toElement();
        loadStreamDetails(streamDetails, elem);
        return true;
    }
    return false;
}

void XbmcXml::loadStreamDetails(StreamDetails *streamDetails, QDomElement elem)
{
    if (!elem.elementsByTagName("video").isEmpty()) {
        QDomElement videoElem = elem.elementsByTagName("video").at(0).toElement();
        QList<StreamDetails::VideoDetails> details = {StreamDetails::VideoDetails::Codec,
            StreamDetails::VideoDetails::Aspect,
            StreamDetails::VideoDetails::Width,
            StreamDetails::VideoDetails::Height,
            StreamDetails::VideoDetails::DurationInSeconds,
            StreamDetails::VideoDetails::ScanType,
            StreamDetails::VideoDetails::StereoMode};
        for (const auto &detail : details) {
            const auto detailStr = StreamDetails::detailToString(detail);
            if (!videoElem.elementsByTagName(detailStr).isEmpty()) {
                streamDetails->setVideoDetail(detail, videoElem.elementsByTagName(detailStr).at(0).toElement().text());
            }
        }
    }
    if (!elem.elementsByTagName("audio").isEmpty()) {
        for (int i = 0, n = elem.elementsByTagName("audio").count(); i < n; ++i) {
            QList<StreamDetails::AudioDetails> details = {StreamDetails::AudioDetails::Codec,
                StreamDetails::AudioDetails::Language,
                StreamDetails::AudioDetails::Channels};
            QDomElement audioElem = elem.elementsByTagName("audio").at(i).toElement();
            for (const auto &detail : details) {
                const auto detailStr = StreamDetails::detailToString(detail);
                if (!audioElem.elementsByTagName(detailStr).isEmpty()) {
                    streamDetails->setAudioDetail(
                        i, detail, audioElem.elementsByTagName(detailStr).at(0).toElement().text());
                }
            }
        }
    }
    if (!elem.elementsByTagName("subtitle").isEmpty()) {
        for (int i = 0, n = elem.elementsByTagName("subtitle").count(); i < n; ++i) {
            QList<StreamDetails::SubtitleDetails> details = {StreamDetails::SubtitleDetails::Language};
            QDomElement subtitleElem = elem.elementsByTagName("subtitle").at(i).toElement();
            if (!subtitleElem.elementsByTagName("file").isEmpty()) {
                continue;
            }
            for (const auto &detail : details) {
                const auto detailStr = StreamDetails::detailToString(detail);
                if (!subtitleElem.elementsByTagName(detailStr).isEmpty()) {
                    streamDetails->setSubtitleDetail(
                        i, detail, subtitleElem.elementsByTagName(detailStr).at(0).toElement().text());
                }
            }
        }
    }
}

/**
 * @brief Writes streamdetails to xml stream
 * @param xml XML Stream
 * @param streamDetails Stream Details object
 */
void XbmcXml::writeStreamDetails(QXmlStreamWriter &xml, StreamDetails *streamDetails)
{
    if (streamDetails->videoDetails().isEmpty() && streamDetails->audioDetails().isEmpty()
        && streamDetails->subtitleDetails().isEmpty()) {
        return;
    }

    xml.writeStartElement("fileinfo");
    xml.writeStartElement("streamdetails");

    xml.writeStartElement("video");
    QMapIterator<StreamDetails::VideoDetails, QString> itVideo(streamDetails->videoDetails());
    while (itVideo.hasNext()) {
        itVideo.next();
        if (itVideo.key() == StreamDetails::VideoDetails::Width && itVideo.value().toInt() == 0) {
            continue;
        }
        if (itVideo.key() == StreamDetails::VideoDetails::Height && itVideo.value().toInt() == 0) {
            continue;
        }
        if (itVideo.key() == StreamDetails::VideoDetails::DurationInSeconds && itVideo.value().toInt() == 0) {
            continue;
        }
        if (itVideo.value().isEmpty()) {
            continue;
        }

        QString value = itVideo.value();

        if (itVideo.key() == StreamDetails::VideoDetails::Aspect) {
            value = value.replace(",", ".");
        }

        xml.writeTextElement(StreamDetails::detailToString(itVideo.key()), value);
    }
    xml.writeEndElement();

    for (int i = 0, n = streamDetails->audioDetails().count(); i < n; ++i) {
        xml.writeStartElement("audio");
        QMapIterator<StreamDetails::AudioDetails, QString> itAudio(streamDetails->audioDetails().at(i));
        while (itAudio.hasNext()) {
            itAudio.next();
            if (itAudio.value() == "") {
                continue;
            }
            xml.writeTextElement(StreamDetails::detailToString(itAudio.key()), itAudio.value());
        }
        xml.writeEndElement();
    }

    for (int i = 0, n = streamDetails->subtitleDetails().count(); i < n; ++i) {
        xml.writeStartElement("subtitle");
        QMapIterator<StreamDetails::SubtitleDetails, QString> itSubtitle(streamDetails->subtitleDetails().at(i));
        while (itSubtitle.hasNext()) {
            itSubtitle.next();
            if (itSubtitle.value() == "") {
                continue;
            }
            xml.writeTextElement(StreamDetails::detailToString(itSubtitle.key()), itSubtitle.value());
        }
        xml.writeEndElement();
    }

    xml.writeEndElement();
    xml.writeEndElement();
}

void XbmcXml::writeStreamDetails(QDomDocument &doc, const StreamDetails *streamDetails, QList<Subtitle *> subtitles)
{
    if (streamDetails->videoDetails().isEmpty() && streamDetails->audioDetails().isEmpty()
        && streamDetails->subtitleDetails().isEmpty() && subtitles.isEmpty()) {
        return;
    }

    removeChildNodes(doc, "fileinfo");
    QDomElement elemFi = doc.createElement("fileinfo");
    QDomElement elemSd = doc.createElement("streamdetails");

    QDomElement elemVideo = doc.createElement("video");
    QMapIterator<StreamDetails::VideoDetails, QString> itVideo(streamDetails->videoDetails());
    while (itVideo.hasNext()) {
        itVideo.next();
        if (itVideo.key() == StreamDetails::VideoDetails::Width && itVideo.value().toInt() == 0) {
            continue;
        }
        if (itVideo.key() == StreamDetails::VideoDetails::Height && itVideo.value().toInt() == 0) {
            continue;
        }
        if (itVideo.key() == StreamDetails::VideoDetails::DurationInSeconds && itVideo.value().toInt() == 0) {
            continue;
        }
        if (itVideo.value().isEmpty()) {
            continue;
        }

        QString value = itVideo.value();

        if (itVideo.key() == StreamDetails::VideoDetails::Aspect) {
            value = value.replace(",", ".");
        }

        QDomElement elem = doc.createElement(StreamDetails::detailToString(itVideo.key()));
        elem.appendChild(doc.createTextNode(value));
        elemVideo.appendChild(elem);
    }
    elemSd.appendChild(elemVideo);

    for (int i = 0, n = streamDetails->audioDetails().count(); i < n; ++i) {
        QDomElement elemAudio = doc.createElement("audio");
        QMapIterator<StreamDetails::AudioDetails, QString> itAudio(streamDetails->audioDetails().at(i));
        while (itAudio.hasNext()) {
            itAudio.next();
            if (itAudio.value().isEmpty()) {
                continue;
            }

            QDomElement elem = doc.createElement(StreamDetails::detailToString(itAudio.key()));
            elem.appendChild(doc.createTextNode(itAudio.value()));
            elemAudio.appendChild(elem);
        }
        elemSd.appendChild(elemAudio);
    }

    for (int i = 0, n = streamDetails->subtitleDetails().count(); i < n; ++i) {
        QDomElement elemSubtitle = doc.createElement("subtitle");
        QMapIterator<StreamDetails::SubtitleDetails, QString> itSubtitle(streamDetails->subtitleDetails().at(i));
        while (itSubtitle.hasNext()) {
            itSubtitle.next();
            if (itSubtitle.value().isEmpty()) {
                continue;
            }

            QDomElement elem = doc.createElement(StreamDetails::detailToString(itSubtitle.key()));
            elem.appendChild(doc.createTextNode(itSubtitle.value()));
            elemSubtitle.appendChild(elem);
        }
        elemSd.appendChild(elemSubtitle);
    }

    foreach (Subtitle *subtitle, subtitles) {
        QDomElement elemSubtitle = doc.createElement("subtitle");
        QDomElement elem = doc.createElement("language");
        elem.appendChild(doc.createTextNode(subtitle->language()));
        elemSubtitle.appendChild(elem);

        QDomElement elem2 = doc.createElement("file");
        elem2.appendChild(doc.createTextNode(subtitle->files().first()));
        elemSubtitle.appendChild(elem2);

        elemSd.appendChild(elemSubtitle);
    }

    elemFi.appendChild(elemSd);
    appendXmlNode(doc, elemFi);
}

/**
 * @brief Get the path to the actor image
 * @param movie
 * @param actor Actor
 * @return Path to actor image
 */
QString XbmcXml::actorImageName(Movie *movie, Actor actor)
{
    if (movie->files().isEmpty()) {
        return QString();
    }
    QFileInfo fi(movie->files().at(0));
    QString actorName = actor.name;
    actorName = actorName.replace(" ", "_");
    QString path = fi.absolutePath() + "/" + ".actors" + "/" + actorName + ".jpg";
    fi.setFile(path);
    if (fi.isFile()) {
        return path;
    }
    return QString();
}

QByteArray XbmcXml::getConcertXml(Concert *concert)
{
    Kodi::ConcertXmlWriter writer(*concert);
    return writer.getConcertXml();
}

/**
 * @brief Saves a concert (including images)
 * @param concert Concert to save
 * @return Saving success
 * @see XbmcXml::writeConcertXml
 */
bool XbmcXml::saveConcert(Concert *concert)
{
    qDebug() << "Entered, concert=" << concert->name();
    QByteArray xmlContent = getConcertXml(concert);

    if (concert->files().empty()) {
        qWarning() << "Concert has no files";
        return false;
    }

    concert->setNfoContent(xmlContent);
    Manager::instance()->database()->update(concert);

    bool saved = false;
    QFileInfo fi(concert->files().at(0));
    for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::ConcertNfo)) {
        QString saveFileName =
            dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, concert->files().count() > 1);
        QString saveFilePath = fi.absolutePath() + "/" + saveFileName;
        QDir saveFileDir = QFileInfo(saveFilePath).dir();
        if (!saveFileDir.exists()) {
            saveFileDir.mkpath(".");
        }
        QFile file(saveFilePath);
        qDebug() << "Saving to" << file.fileName();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "File could not be openend";
        } else {
            file.write(xmlContent);
            file.close();
            saved = true;
        }
    }
    if (!saved) {
        return false;
    }

    for (const auto imageType : Concert::imageTypes()) {
        DataFileType dataFileType = DataFile::dataFileTypeForImageType(imageType);
        if (concert->imageHasChanged(imageType) && !concert->image(imageType).isNull()) {
            for (DataFile dataFile : Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName =
                    dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, concert->files().count() > 1);
                if (imageType == ImageType::ConcertPoster
                    && (concert->discType() == DiscType::BluRay || concert->discType() == DiscType::Dvd)) {
                    saveFileName = "poster.jpg";
                }
                if (imageType == ImageType::ConcertBackdrop
                    && (concert->discType() == DiscType::BluRay || concert->discType() == DiscType::Dvd)) {
                    saveFileName = "fanart.jpg";
                }
                QString path = getPath(concert);
                saveFile(path + "/" + saveFileName, concert->image(imageType));
            }
        }
        if (concert->imagesToRemove().contains(imageType)) {
            for (DataFile dataFile : Settings::instance()->dataFiles(imageType)) {
                QString saveFileName =
                    dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, concert->files().count() > 1);
                if (imageType == ImageType::ConcertPoster
                    && (concert->discType() == DiscType::BluRay || concert->discType() == DiscType::Dvd)) {
                    saveFileName = "poster.jpg";
                }
                if (imageType == ImageType::ConcertBackdrop
                    && (concert->discType() == DiscType::BluRay || concert->discType() == DiscType::Dvd)) {
                    saveFileName = "fanart.jpg";
                }
                QString path = getPath(concert);
                QFile(path + "/" + saveFileName).remove();
            }
        }
    }

    if (concert->inSeparateFolder() && !concert->files().isEmpty()) {
        for (const QString &file : concert->extraFanartsToRemove()) {
            QFile::remove(file);
        }
        QDir dir(QFileInfo(concert->files().first()).absolutePath() + "/extrafanart");
        if (!dir.exists() && !concert->extraFanartImagesToAdd().isEmpty()) {
            QDir(QFileInfo(concert->files().first()).absolutePath()).mkdir("extrafanart");
        }
        for (QByteArray img : concert->extraFanartImagesToAdd()) {
            int num = 1;
            while (QFileInfo(dir.absolutePath() + "/" + QString("fanart%1.jpg").arg(num)).exists()) {
                ++num;
            }
            saveFile(dir.absolutePath() + "/" + QString("fanart%1.jpg").arg(num), img);
        }
    }

    return true;
}

/**
 * @brief Loads concert infos (except images)
 * @param concert Concert to load
 * @return Loading success
 */
bool XbmcXml::loadConcert(Concert *concert, QString initialNfoContent)
{
    concert->clear();
    concert->setChanged(false);

    QString nfoContent;
    if (initialNfoContent.isEmpty()) {
        QString nfoFile = nfoFilePath(concert);
        if (nfoFile.isEmpty()) {
            return false;
        }

        QFile file(nfoFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "File" << nfoFile << "could not be opened for reading";
            return false;
        }
        nfoContent = QString::fromUtf8(file.readAll());
        concert->setNfoContent(nfoContent);
        file.close();
    } else {
        nfoContent = initialNfoContent;
    }

    QDomDocument domDoc;
    domDoc.setContent(nfoContent);

    Kodi::ConcertXmlReader reader(*concert);
    reader.parseNfoDom(domDoc);

    concert->setStreamDetailsLoaded(loadStreamDetails(concert->streamDetails(), domDoc));

    // Existence of images
    if (initialNfoContent.isEmpty()) {
        for (const auto &imageType : Concert::imageTypes()) {
            concert->setHasImage(imageType, !imageFileName(concert, imageType).isEmpty());
        }
        concert->setHasExtraFanarts(!extraFanartNames(concert).isEmpty());
    }

    return true;
}

/**
 * @brief Get path to actor image
 * @param show
 * @param actor
 * @return Path to actor image
 */
QString XbmcXml::actorImageName(TvShow *show, Actor actor)
{
    if (show->dir().isEmpty()) {
        return QString();
    }
    QString actorName = actor.name;
    actorName = actorName.replace(" ", "_");
    QString fileName = show->dir() + "/" + ".actors" + "/" + actorName + ".jpg";
    QFileInfo fi(fileName);
    if (fi.isFile()) {
        return fileName;
    }
    return QString();
}

QString XbmcXml::actorImageName(TvShowEpisode *episode, Actor actor)
{
    if (episode->files().isEmpty()) {
        return QString();
    }
    QFileInfo fi(episode->files().at(0));
    QString actorName = actor.name;
    actorName = actorName.replace(" ", "_");
    QString path = fi.absolutePath() + "/" + ".actors" + "/" + actorName + ".jpg";
    fi.setFile(path);
    if (fi.isFile()) {
        return path;
    }
    return QString();
}

/**
 * @brief Loads tv show information
 * @param show Show to load
 * @return Loading success
 */
bool XbmcXml::loadTvShow(TvShow *show, QString initialNfoContent)
{
    show->clear();
    show->setChanged(false);

    QString nfoContent;
    if (initialNfoContent.isEmpty()) {
        if (show->dir().isEmpty()) {
            return false;
        }

        QString nfoFile;
        for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::TvShowNfo)) {
            QString file = dataFile.saveFileName("");
            QFileInfo nfoFi(show->dir() + "/" + file);
            if (nfoFi.exists()) {
                nfoFile = show->dir() + "/" + file;
                break;
            }
        }
        QFile file(nfoFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Nfo file could not be opened for reading" << nfoFile;
            return false;
        }
        nfoContent = QString::fromUtf8(file.readAll());
        show->setNfoContent(nfoContent);
        file.close();
    } else {
        nfoContent = initialNfoContent;
    }

    QDomDocument domDoc;
    domDoc.setContent(nfoContent);

    Kodi::TvShowXmlReader reader(*show);
    reader.parseNfoDom(domDoc);

    return true;
}

/**
 * @brief Loads tv show episode information
 * @param episode Episode to load infos for
 * @return Loading success
 */
bool XbmcXml::loadTvShowEpisode(TvShowEpisode *episode, QString initialNfoContent)
{
    if (!episode) {
        qWarning() << "Passed an empty (null) episode to loadTvShowEpisode";
        return false;
    }
    episode->clear();
    episode->setChanged(false);

    QString nfoContent;
    if (initialNfoContent.isEmpty()) {
        QString nfoFile = nfoFilePath(episode);
        if (nfoFile.isEmpty()) {
            return false;
        }

        QFile file(nfoFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "File" << nfoFile << "could not be opened for reading";
            return false;
        }
        nfoContent = QString::fromUtf8(file.readAll());
        episode->setNfoContent(nfoContent);
        file.close();
    } else {
        nfoContent = initialNfoContent;
    }

    QString def;
    QStringList baseNfoContent;
    for (const QString &line : nfoContent.split("\n")) {
        if (!line.startsWith("<?xml")) {
            baseNfoContent << line;
        } else {
            def = line;
        }
    }
    QString nfoContentWithRoot = QString("%1\n<root>%2</root>").arg(def).arg(baseNfoContent.join("\n"));
    QDomDocument domDoc;
    domDoc.setContent(nfoContentWithRoot);

    QDomNodeList episodeDetailsList = domDoc.elementsByTagName("episodedetails");
    if (episodeDetailsList.isEmpty()) {
        return false;
    }

    QDomElement episodeDetails;
    if (episodeDetailsList.count() > 1) {
        bool found = false;
        for (int i = 0, n = episodeDetailsList.count(); i < n; ++i) {
            episodeDetails = episodeDetailsList.at(i).toElement();
            if (!episodeDetails.elementsByTagName("season").isEmpty()
                && episodeDetails.elementsByTagName("season").at(0).toElement().text().toInt()
                       == episode->season().toInt()
                && !episodeDetails.elementsByTagName("episode").isEmpty()
                && episodeDetails.elementsByTagName("episode").at(0).toElement().text().toInt()
                       == episode->episode().toInt()) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }

    } else {
        episodeDetails = episodeDetailsList.at(0).toElement();
    }

    // todo: move above code into reader as well
    Kodi::EpisodeXmlReader reader(*episode);
    reader.parseNfoDom(domDoc, episodeDetails);

    if (episodeDetails.elementsByTagName("streamdetails").count() > 0) {
        loadStreamDetails(
            episode->streamDetails(), episodeDetails.elementsByTagName("streamdetails").at(0).toElement());
        episode->setStreamDetailsLoaded(true);
    } else {
        episode->setStreamDetailsLoaded(false);
    }

    return true;
}

/**
 * @brief Saves a tv show
 * @param show Show to save
 * @return Saving success
 * @see XbmcXml::writeTvShowXml
 */
bool XbmcXml::saveTvShow(TvShow *show)
{
    QByteArray xmlContent = getTvShowXml(show);

    if (show->dir().isEmpty()) {
        return false;
    }

    show->setNfoContent(xmlContent);
    Manager::instance()->database()->update(show);

    foreach (DataFile dataFile, Settings::instance()->dataFiles(DataFileType::TvShowNfo)) {
        QString saveFilePath = show->dir() + "/" + dataFile.saveFileName("");
        QDir saveFileDir = QFileInfo(saveFilePath).dir();
        if (!saveFileDir.exists()) {
            saveFileDir.mkpath(".");
        }
        QFile file(saveFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "Nfo file could not be openend for writing" << file.fileName();
            return false;
        }
        file.write(xmlContent);
        file.close();
    }

    for (const auto imageType : TvShow::imageTypes()) {
        DataFileType dataFileType = DataFile::dataFileTypeForImageType(imageType);
        if (show->imageHasChanged(imageType) && !show->image(imageType).isNull()) {
            for (auto dataFile : Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName = dataFile.saveFileName("");
                saveFile(show->dir() + "/" + saveFileName, show->image(imageType));
            }
        }
        if (show->imagesToRemove().contains(imageType)) {
            for (auto dataFile : Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName = dataFile.saveFileName("");
                QFile(show->dir() + "/" + saveFileName).remove();
            }
        }
    }

    for (const auto imageType : TvShow::seasonImageTypes()) {
        DataFileType dataFileType = DataFile::dataFileTypeForImageType(imageType);
        for (const auto &season : show->seasons()) {
            if (show->seasonImageHasChanged(season, imageType) && !show->seasonImage(season, imageType).isNull()) {
                for (DataFile dataFile : Settings::instance()->dataFiles(dataFileType)) {
                    QString saveFileName = dataFile.saveFileName("", season);
                    saveFile(show->dir() + "/" + saveFileName, show->seasonImage(season, imageType));
                }
            }
            if (show->imagesToRemove().contains(imageType)
                && show->imagesToRemove().value(imageType).contains(season)) {
                for (DataFile dataFile : Settings::instance()->dataFiles(dataFileType)) {
                    QString saveFileName = dataFile.saveFileName("", season);
                    QFile(show->dir() + "/" + saveFileName).remove();
                }
            }
        }
    }

    if (!show->dir().isEmpty()) {
        foreach (const QString &file, show->extraFanartsToRemove())
            QFile::remove(file);
        QDir dir(show->dir() + "/extrafanart");
        if (!dir.exists() && !show->extraFanartImagesToAdd().isEmpty()) {
            QDir(show->dir()).mkdir("extrafanart");
        }
        foreach (QByteArray img, show->extraFanartImagesToAdd()) {
            int num = 1;
            while (QFileInfo(dir.absolutePath() + "/" + QString("fanart%1.jpg").arg(num)).exists()) {
                ++num;
            }
            saveFile(dir.absolutePath() + "/" + QString("fanart%1.jpg").arg(num), img);
        }
    }

    foreach (const Actor &actor, show->actors()) {
        if (!actor.image.isNull()) {
            QDir dir;
            dir.mkdir(show->dir() + "/" + ".actors");
            QString actorName = actor.name;
            actorName = actorName.replace(" ", "_");
            saveFile(show->dir() + "/" + ".actors" + "/" + actorName + ".jpg", actor.image);
        }
    }

    return true;
}

/**
 * @brief Saves a tv show episode
 * @param episode Episode to save
 * @return Saving success
 * @see XbmcXml::writeTvShowEpisodeXml
 */
bool XbmcXml::saveTvShowEpisode(TvShowEpisode *episode)
{
    qDebug() << "Entered, episode=" << episode->name();

    // Multi-Episode handling
    QList<TvShowEpisode *> episodes;
    foreach (TvShowEpisode *subEpisode, episode->tvShow()->episodes()) {
        if (subEpisode->isDummy()) {
            continue;
        }
        if (episode->files() == subEpisode->files()) {
            episodes.append(subEpisode);
        }
    }

    QByteArray xmlContent;
    QXmlStreamWriter xml(&xmlContent);
    xml.setAutoFormatting(true);
    xml.writeStartDocument("1.0", true);
    for (TvShowEpisode *subEpisode : episodes) {
        writeTvShowEpisodeXml(xml, subEpisode);
        subEpisode->setChanged(false);
        subEpisode->setSyncNeeded(true);
    }
    xml.writeEndDocument();

    if (episode->files().isEmpty()) {
        qWarning() << "Episode has no files";
        return false;
    }

    for (TvShowEpisode *subEpisode : episodes) {
        subEpisode->setNfoContent(xmlContent);
        Manager::instance()->database()->update(subEpisode);
    }

    QFileInfo fi(episode->files().at(0));
    for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::TvShowEpisodeNfo)) {
        QString saveFileName =
            dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, episode->files().count() > 1);
        QString saveFilePath = fi.absolutePath() + "/" + saveFileName;
        QDir saveFileDir = QFileInfo(saveFilePath).dir();
        if (!saveFileDir.exists()) {
            saveFileDir.mkpath(".");
        }
        QFile file(saveFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "Nfo file could not be opened for writing" << saveFileName;
            return false;
        }
        file.write(xmlContent);
        file.close();
    }

    fi.setFile(episode->files().at(0));
    if (episode->thumbnailImageChanged() && !episode->thumbnailImage().isNull()) {
        if (Helper::instance()->isBluRay(episode->files().at(0)) || Helper::instance()->isDvd(episode->files().at(0))) {
            QDir dir = fi.dir();
            dir.cdUp();
            saveFile(dir.absolutePath() + "/thumb.jpg", episode->thumbnailImage());
        } else if (Helper::instance()->isDvd(episode->files().at(0), true)) {
            saveFile(fi.dir().absolutePath() + "/thumb.jpg", episode->thumbnailImage());
        } else {
            foreach (DataFile dataFile, Settings::instance()->dataFiles(DataFileType::TvShowEpisodeThumb)) {
                QString saveFileName =
                    dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, episode->files().count() > 1);
                saveFile(fi.absolutePath() + "/" + saveFileName, episode->thumbnailImage());
            }
        }
    }

    fi.setFile(episode->files().at(0));
    if (episode->imagesToRemove().contains(ImageType::TvShowEpisodeThumb)) {
        if (Helper::instance()->isBluRay(episode->files().at(0)) || Helper::instance()->isDvd(episode->files().at(0))) {
            QDir dir = fi.dir();
            dir.cdUp();
            QFile(dir.absolutePath() + "/thumb.jpg").remove();
        } else if (Helper::instance()->isDvd(episode->files().at(0), true)) {
            QFile(fi.dir().absolutePath() + "/thumb.jpg").remove();
        } else {
            for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::TvShowEpisodeThumb)) {
                QString saveFileName =
                    dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, episode->files().count() > 1);
                QFile(fi.absolutePath() + "/" + saveFileName).remove();
            }
        }
    }

    fi.setFile(episode->files().at(0));
    for (const Actor &actor : episode->actors()) {
        if (!actor.image.isNull()) {
            QDir dir;
            dir.mkdir(fi.absolutePath() + "/" + ".actors");
            QString actorName = actor.name;
            actorName = actorName.replace(" ", "_");
            saveFile(fi.absolutePath() + "/" + ".actors" + "/" + actorName + ".jpg", actor.image);
        }
    }

    return true;
}

QByteArray XbmcXml::getTvShowXml(TvShow *show)
{
    Kodi::TvShowXmlWriter writer(*show);
    return writer.getTvShowXml();
}

/**
 * @brief Writes tv show episode elements to an xml stream
 * @param xml XML stream
 * @param episode Episode to save
 */
void XbmcXml::writeTvShowEpisodeXml(QXmlStreamWriter &xml, TvShowEpisode *episode)
{
    qDebug() << "Entered, episode=" << episode->name();
    xml.writeStartElement("episodedetails");
    xml.writeTextElement("imdbid", episode->imdbId().toString());
    xml.writeTextElement("title", episode->name());
    xml.writeTextElement("showtitle", episode->showTitle());
    xml.writeTextElement("rating", QString("%1").arg(episode->rating()));
    xml.writeTextElement("votes", QString("%1").arg(episode->votes()));
    xml.writeTextElement("top250", QString("%1").arg(episode->top250()));
    xml.writeTextElement("season", episode->season().toString());
    xml.writeTextElement("episode", episode->episode().toString());
    if (episode->displaySeason() != SeasonNumber::NoSeason) {
        xml.writeTextElement("displayseason", episode->displaySeason().toString());
    }
    if (episode->displayEpisode() != EpisodeNumber::NoEpisode) {
        xml.writeTextElement("displayepisode", episode->displayEpisode().toString());
    }
    xml.writeTextElement("plot", episode->overview());
    xml.writeTextElement("outline", episode->overview());
    xml.writeTextElement("mpaa", episode->certification().toString());
    xml.writeTextElement("playcount", QString("%1").arg(episode->playCount()));
    xml.writeTextElement("lastplayed", episode->lastPlayed().toString("yyyy-MM-dd HH:mm:ss"));
    xml.writeTextElement("aired", episode->firstAired().toString("yyyy-MM-dd"));
    xml.writeTextElement("studio", episode->network());
    if (!episode->epBookmark().isNull() && QTime(0, 0, 0).secsTo(episode->epBookmark()) > 0) {
        xml.writeTextElement("epbookmark", QString("%1").arg(QTime(0, 0, 0).secsTo(episode->epBookmark())));
    }
    for (const QString &writer : episode->writers()) {
        xml.writeTextElement("credits", writer);
    }

    for (const QString &director : episode->directors()) {
        xml.writeTextElement("director", director);
    }
    if (Settings::instance()->advanced()->writeThumbUrlsToNfo() && !episode->thumbnail().isEmpty()) {
        xml.writeTextElement("thumb", episode->thumbnail().toString());
    }

    for (const Actor &actor : episode->actors()) {
        xml.writeStartElement("actor");
        xml.writeTextElement("name", actor.name);
        xml.writeTextElement("role", actor.role);
        if (!actor.thumb.isEmpty() && Settings::instance()->advanced()->writeThumbUrlsToNfo()) {
            xml.writeTextElement("thumb", actor.thumb);
        }
        xml.writeEndElement();
    }

    XbmcXml::writeStreamDetails(xml, episode->streamDetails());

    xml.writeEndElement();
}

QStringList XbmcXml::extraFanartNames(Movie *movie)
{
    if (movie->files().isEmpty() || !movie->inSeparateFolder()) {
        return QStringList();
    }
    QFileInfo fi(movie->files().first());
    QDir dir(fi.absolutePath() + "/extrafanart");
    QStringList filters = {"*.jpg", "*.jpeg", "*.JPEG", "*.Jpeg", "*.JPeg"};
    QStringList files;
    for (const QString &file : dir.entryList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        files << QDir::toNativeSeparators(dir.path() + "/" + file);
    }
    return files;
}

QStringList XbmcXml::extraFanartNames(Concert *concert)
{
    if (concert->files().isEmpty() || !concert->inSeparateFolder()) {
        return QStringList();
    }
    QFileInfo fi(concert->files().first());
    QDir dir(fi.absolutePath() + "/extrafanart");
    QStringList filters = {"*.jpg", "*.jpeg", "*.JPEG", "*.Jpeg", "*.JPeg"};
    QStringList files;
    for (const QString &file : dir.entryList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        files << QDir::toNativeSeparators(dir.path() + "/" + file);
    }
    return files;
}

QStringList XbmcXml::extraFanartNames(TvShow *show)
{
    if (show->dir().isEmpty()) {
        return QStringList();
    }
    QDir dir(show->dir() + "/extrafanart");
    QStringList filters = {"*.jpg", "*.jpeg", "*.JPEG", "*.Jpeg", "*.JPeg"};
    QStringList files;
    for (const QString &file : dir.entryList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        files << QDir::toNativeSeparators(dir.path() + "/" + file);
    }
    return files;
}

QStringList XbmcXml::extraFanartNames(Artist *artist)
{
    QDir dir(artist->path() + "/extrafanart");
    QStringList filters = {"*.jpg", "*.jpeg", "*.JPEG", "*.Jpeg", "*.JPeg"};
    QStringList files;
    for (const QString &file : dir.entryList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        files << QDir::toNativeSeparators(dir.path() + "/" + file);
    }
    return files;
}

QImage XbmcXml::movieSetPoster(QString setName)
{
    for (DataFile dataFile : Settings::instance()->dataFiles(DataFileType::MovieSetPoster)) {
        QString fileName = movieSetFileName(setName, &dataFile);
        QFileInfo fi(fileName);
        if (fi.exists()) {
            return QImage(fi.absoluteFilePath());
        }
    }
    return QImage();
}

QImage XbmcXml::movieSetBackdrop(QString setName)
{
    foreach (DataFile dataFile, Settings::instance()->dataFiles(DataFileType::MovieSetBackdrop)) {
        QString fileName = movieSetFileName(setName, &dataFile);
        QFileInfo fi(fileName);
        if (fi.exists()) {
            return QImage(fi.absoluteFilePath());
        }
    }
    return QImage();
}

/**
 * @brief Save movie set poster
 * @param setName
 * @param poster
 */
void XbmcXml::saveMovieSetPoster(QString setName, QImage poster)
{
    foreach (DataFile dataFile, Settings::instance()->dataFiles(DataFileType::MovieSetPoster)) {
        QString fileName = movieSetFileName(setName, &dataFile);
        if (!fileName.isEmpty()) {
            poster.save(fileName, "jpg", 100);
        }
    }
}

/**
 * @brief Save movie set backdrop
 * @param setName
 * @param backdrop
 */
void XbmcXml::saveMovieSetBackdrop(QString setName, QImage backdrop)
{
    foreach (DataFile dataFile, Settings::instance()->dataFiles(DataFileType::MovieSetBackdrop)) {
        QString fileName = movieSetFileName(setName, &dataFile);
        if (!fileName.isEmpty()) {
            backdrop.save(fileName, "jpg", 100);
        }
    }
}

bool XbmcXml::saveFile(QString filename, QByteArray data)
{
    QDir saveFileDir = QFileInfo(filename).dir();
    if (!saveFileDir.exists()) {
        saveFileDir.mkpath(".");
    }
    QFile file(filename);

    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
        return true;
    }
    return false;
}

QString XbmcXml::getPath(const Movie *movie)
{
    if (movie->files().isEmpty()) {
        return QString();
    }
    QFileInfo fi(movie->files().first());
    if (movie->discType() == DiscType::BluRay) {
        QDir dir = fi.dir();
        if (QString::compare(dir.dirName(), "BDMV", Qt::CaseInsensitive) == 0) {
            dir.cdUp();
        }
        return dir.absolutePath();
    } else if (movie->discType() == DiscType::Dvd) {
        QDir dir = fi.dir();
        if (QString::compare(dir.dirName(), "VIDEO_TS", Qt::CaseInsensitive) == 0) {
            dir.cdUp();
        }
        return dir.absolutePath();
    }
    return fi.absolutePath();
}

QString XbmcXml::getPath(const Concert *concert)
{
    if (concert->files().isEmpty()) {
        return QString();
    }
    QFileInfo fi(concert->files().first());
    if (concert->discType() == DiscType::BluRay) {
        QDir dir = fi.dir();
        if (QString::compare(dir.dirName(), "BDMV", Qt::CaseInsensitive) == 0) {
            dir.cdUp();
        }
        return dir.absolutePath();
    } else if (concert->discType() == DiscType::Dvd) {
        QDir dir = fi.dir();
        if (QString::compare(dir.dirName(), "VIDEO_TS", Qt::CaseInsensitive) == 0) {
            dir.cdUp();
        }
        return dir.absolutePath();
    }
    return fi.absolutePath();
}

QString XbmcXml::movieSetFileName(QString setName, DataFile *dataFile)
{
    if (Settings::instance()->movieSetArtworkType() == MovieSetArtworkType::SingleArtworkFolder) {
        QDir dir(Settings::instance()->movieSetArtworkDirectory());
        QString fileName = dataFile->saveFileName(setName);
        return dir.absolutePath() + "/" + fileName;
    } else if (Settings::instance()->movieSetArtworkType() == MovieSetArtworkType::SingleSetFolder) {
        foreach (Movie *movie, Manager::instance()->movieModel()->movies()) {
            if (movie->set() == setName && !movie->files().isEmpty()) {
                QFileInfo fi(movie->files().first());
                QDir dir = fi.dir();
                if (movie->inSeparateFolder()) {
                    dir.cdUp();
                }
                if (movie->discType() == DiscType::Dvd || movie->discType() == DiscType::BluRay) {
                    dir.cdUp();
                }
                return dir.absolutePath() + "/" + dataFile->saveFileName(setName);
            }
        }
    }

    return QString();
}

QString XbmcXml::imageFileName(const Movie *movie, ImageType type, QList<DataFile> dataFiles, bool constructName)
{
    DataFileType fileType = [type]() {
        switch (type) {
        case ImageType::MoviePoster: return DataFileType::MoviePoster;
        case ImageType::MovieBackdrop: return DataFileType::MovieBackdrop;
        case ImageType::MovieLogo: return DataFileType::MovieLogo;
        case ImageType::MovieBanner: return DataFileType::MovieBanner;
        case ImageType::MovieThumb: return DataFileType::MovieThumb;
        case ImageType::MovieClearArt: return DataFileType::MovieClearArt;
        case ImageType::MovieCdArt: return DataFileType::MovieCdArt;
        default: return DataFileType::NoType;
        }
    }();

    if (fileType == DataFileType::NoType) {
        return "";
    }

    if (movie->files().empty()) {
        qWarning() << "Movie has no files";
        return "";
    }

    if (!constructName) {
        dataFiles = Settings::instance()->dataFiles(fileType);
    }

    QString fileName;
    QFileInfo fi(movie->files().at(0));
    for (DataFile dataFile : dataFiles) {
        QString file = dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, movie->files().count() > 1);
        if (movie->discType() == DiscType::BluRay || movie->discType() == DiscType::Dvd) {
            if (type == ImageType::MoviePoster) {
                file = "poster.jpg";
            } else if (type == ImageType::MovieBackdrop) {
                file = "fanart.jpg";
            }
        }
        QString path = getPath(movie);
        QFileInfo pFi(path + "/" + file);
        if (pFi.isFile() || constructName) {
            fileName = path + "/" + file;
            break;
        }
    }

    return fileName;
}

QString XbmcXml::imageFileName(const Concert *concert, ImageType type, QList<DataFile> dataFiles, bool constructName)
{
    DataFileType fileType;
    switch (type) {
    case ImageType::ConcertPoster: fileType = DataFileType::ConcertPoster; break;
    case ImageType::ConcertBackdrop: fileType = DataFileType::ConcertBackdrop; break;
    case ImageType::ConcertLogo: fileType = DataFileType::ConcertLogo; break;
    case ImageType::ConcertClearArt: fileType = DataFileType::ConcertClearArt; break;
    case ImageType::ConcertCdArt: fileType = DataFileType::ConcertCdArt; break;
    default: return "";
    }

    if (concert->files().empty()) {
        qWarning() << "Concert has no files";
        return "";
    }

    if (!constructName) {
        dataFiles = Settings::instance()->dataFiles(fileType);
    }

    QString fileName;
    QFileInfo fi(concert->files().at(0));
    for (DataFile dataFile : dataFiles) {
        QString file = dataFile.saveFileName(fi.fileName(), SeasonNumber::NoSeason, concert->files().count() > 1);
        if (concert->discType() == DiscType::BluRay || concert->discType() == DiscType::Dvd) {
            if (type == ImageType::ConcertPoster) {
                file = "poster.jpg";
            }
            if (type == ImageType::ConcertBackdrop) {
                file = "fanart.jpg";
            }
        }
        QString path = getPath(concert);
        QFileInfo pFi(path + "/" + file);
        if (pFi.isFile() || constructName) {
            fileName = path + "/" + file;
            break;
        }
    }

    return fileName;
}

QString XbmcXml::imageFileName(const TvShow *show,
    ImageType type,
    SeasonNumber season,
    QList<DataFile> dataFiles,
    bool constructName)
{
    DataFileType fileType;
    switch (type) {
    case ImageType::TvShowPoster: fileType = DataFileType::TvShowPoster; break;
    case ImageType::TvShowBackdrop: fileType = DataFileType::TvShowBackdrop; break;
    case ImageType::TvShowLogos: fileType = DataFileType::TvShowLogo; break;
    case ImageType::TvShowBanner: fileType = DataFileType::TvShowBanner; break;
    case ImageType::TvShowThumb: fileType = DataFileType::TvShowThumb; break;
    case ImageType::TvShowClearArt: fileType = DataFileType::TvShowClearArt; break;
    case ImageType::TvShowCharacterArt: fileType = DataFileType::TvShowCharacterArt; break;
    case ImageType::TvShowSeasonPoster: fileType = DataFileType::TvShowSeasonPoster; break;
    case ImageType::TvShowSeasonBackdrop: fileType = DataFileType::TvShowSeasonBackdrop; break;
    case ImageType::TvShowSeasonBanner: fileType = DataFileType::TvShowSeasonBanner; break;
    case ImageType::TvShowSeasonThumb: fileType = DataFileType::TvShowSeasonThumb; break;
    default: return "";
    }

    if (show->dir().isEmpty()) {
        return QString();
    }

    if (!constructName) {
        dataFiles = Settings::instance()->dataFiles(fileType);
    }

    QString fileName;
    for (DataFile dataFile : dataFiles) {
        QString loadFileName = dataFile.saveFileName("", season);
        QFileInfo fi(show->dir() + "/" + loadFileName);
        if (fi.isFile() || constructName) {
            fileName = show->dir() + "/" + loadFileName;
            break;
        }
    }
    return fileName;
}

QString saveDataFiles(QString basePath, QString fileName, const QList<DataFile> &dataFiles, bool constructName)
{
    for (DataFile dataFile : dataFiles) {
        QString file = dataFile.saveFileName(fileName);
        QFileInfo pFi(basePath + "/" + file);
        if (pFi.isFile() || constructName) {
            return basePath + "/" + file;
        }
    }
    return QStringLiteral();
}

QString
XbmcXml::imageFileName(const TvShowEpisode *episode, ImageType type, QList<DataFile> dataFiles, bool constructName)
{
    DataFileType fileType;
    switch (type) {
    case ImageType::TvShowEpisodeThumb: fileType = DataFileType::TvShowEpisodeThumb; break;
    default: return "";
    }

    if (episode->files().isEmpty()) {
        return "";
    }
    QFileInfo fi(episode->files().at(0));

    if (Helper::instance()->isBluRay(episode->files().at(0)) || Helper::instance()->isDvd(episode->files().at(0))) {
        QDir dir = fi.dir();
        dir.cdUp();
        fi.setFile(dir.absolutePath() + "/thumb.jpg");
        return fi.exists() ? fi.absoluteFilePath() : "";
    }

    if (Helper::instance()->isDvd(episode->files().at(0), true)) {
        fi.setFile(fi.dir().absolutePath() + "/thumb.jpg");
        return fi.exists() ? fi.absoluteFilePath() : "";
    }

    if (!constructName) {
        dataFiles = Settings::instance()->dataFiles(fileType);
    }

    return saveDataFiles(fi.absolutePath(), fi.fileName(), dataFiles, constructName);
}

bool XbmcXml::loadArtist(Artist *artist, QString initialNfoContent)
{
    artist->clear();
    artist->setHasChanged(false);

    QString nfoContent;
    if (initialNfoContent.isEmpty()) {
        QString nfoFile = nfoFilePath(artist);
        if (nfoFile.isEmpty()) {
            return false;
        }

        QFile file(nfoFile);
        if (!file.exists()) {
            return false;
        }
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "File" << nfoFile << "could not be opened for reading";
            return false;
        }
        nfoContent = QString::fromUtf8(file.readAll());
        artist->setNfoContent(nfoContent);
        file.close();
    } else {
        nfoContent = initialNfoContent;
    }

    QDomDocument domDoc;
    domDoc.setContent(nfoContent);

    Kodi::ArtistXmlReader reader(*artist);
    reader.parseNfoDom(domDoc);

    return true;
}

bool XbmcXml::loadAlbum(Album *album, QString initialNfoContent)
{
    album->clear();
    album->setHasChanged(false);

    QString nfoContent;
    if (initialNfoContent.isEmpty()) {
        QString nfoFile = nfoFilePath(album);
        if (nfoFile.isEmpty()) {
            return false;
        }

        QFile file(nfoFile);
        if (!file.exists()) {
            return false;
        }
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "File" << nfoFile << "could not be opened for reading";
            return false;
        }
        nfoContent = QString::fromUtf8(file.readAll());
        album->setNfoContent(nfoContent);
        file.close();
    } else {
        nfoContent = initialNfoContent;
    }

    QDomDocument domDoc;
    domDoc.setContent(nfoContent);

    if (!domDoc.elementsByTagName("musicBrainzReleaseGroupID").isEmpty()) {
        album->setMbReleaseGroupId(domDoc.elementsByTagName("musicBrainzReleaseGroupID").at(0).toElement().text());
    }
    if (!domDoc.elementsByTagName("musicBrainzAlbumID").isEmpty()) {
        album->setMbAlbumId(domDoc.elementsByTagName("musicBrainzAlbumID").at(0).toElement().text());
    }
    if (!domDoc.elementsByTagName("allmusicid").isEmpty()) {
        album->setAllMusicId(domDoc.elementsByTagName("allmusicid").at(0).toElement().text());
    }
    if (!domDoc.elementsByTagName("title").isEmpty()) {
        album->setTitle(domDoc.elementsByTagName("title").at(0).toElement().text());
    }
    if (!domDoc.elementsByTagName("artist").isEmpty()) {
        album->setArtist(domDoc.elementsByTagName("artist").at(0).toElement().text());
    }
    if (!domDoc.elementsByTagName("genre").isEmpty()) {
        album->setGenres(
            domDoc.elementsByTagName("genre").at(0).toElement().text().split(" / ", QString::SkipEmptyParts));
    }
    for (int i = 0, n = domDoc.elementsByTagName("style").size(); i < n; i++) {
        album->addStyle(domDoc.elementsByTagName("style").at(i).toElement().text());
    }
    for (int i = 0, n = domDoc.elementsByTagName("mood").size(); i < n; i++) {
        album->addMood(domDoc.elementsByTagName("mood").at(i).toElement().text());
    }
    if (!domDoc.elementsByTagName("review").isEmpty()) {
        album->setReview(domDoc.elementsByTagName("review").at(0).toElement().text());
    }
    if (!domDoc.elementsByTagName("label").isEmpty()) {
        album->setLabel(domDoc.elementsByTagName("label").at(0).toElement().text());
    }
    if (!domDoc.elementsByTagName("releasedate").isEmpty()) {
        album->setReleaseDate(domDoc.elementsByTagName("releasedate").at(0).toElement().text());
    }
    if (!domDoc.elementsByTagName("year").isEmpty()) {
        album->setYear(domDoc.elementsByTagName("year").at(0).toElement().text().toInt());
    }
    if (!domDoc.elementsByTagName("rating").isEmpty()) {
        album->setRating(domDoc.elementsByTagName("rating").at(0).toElement().text().replace(",", ".").toDouble());
    }
    for (int i = 0, n = domDoc.elementsByTagName("thumb").size(); i < n; i++) {
        Poster p;
        p.originalUrl = QUrl(domDoc.elementsByTagName("thumb").at(i).toElement().text());
        if (!domDoc.elementsByTagName("thumb").at(i).toElement().attribute("preview").isEmpty()) {
            p.thumbUrl = QUrl(domDoc.elementsByTagName("thumb").at(i).toElement().attribute("preview"));
        } else {
            p.thumbUrl = p.originalUrl;
        }
        album->addImage(ImageType::AlbumThumb, p);
    }

    album->setHasChanged(false);

    return true;
}

QString XbmcXml::imageFileName(const Artist *artist, ImageType type, QList<DataFile> dataFiles, bool constructName)
{
    DataFileType fileType;
    switch (type) {
    case ImageType::ArtistThumb: fileType = DataFileType::ArtistThumb; break;
    case ImageType::ArtistFanart: fileType = DataFileType::ArtistFanart; break;
    case ImageType::ArtistLogo: fileType = DataFileType::ArtistLogo; break;
    default: return "";
    }

    if (artist->path().isEmpty()) {
        return QString();
    }

    if (!constructName) {
        dataFiles = Settings::instance()->dataFiles(fileType);
    }

    return saveDataFiles(artist->path(), "", dataFiles, constructName);
}

QString XbmcXml::imageFileName(const Album *album, ImageType type, QList<DataFile> dataFiles, bool constructName)
{
    DataFileType fileType;
    switch (type) {
    case ImageType::AlbumThumb: fileType = DataFileType::AlbumThumb; break;
    case ImageType::AlbumCdArt: fileType = DataFileType::AlbumCdArt; break;
    default: return "";
    }

    if (album->path().isEmpty()) {
        return QString();
    }

    if (!constructName) {
        dataFiles = Settings::instance()->dataFiles(fileType);
    }

    return saveDataFiles(album->path(), "", dataFiles, constructName);
}

QString XbmcXml::nfoFilePath(Artist *artist)
{
    if (artist->path().isEmpty()) {
        return QString();
    }

    return artist->path() + "/artist.nfo";
}

QString XbmcXml::nfoFilePath(Album *album)
{
    if (album->path().isEmpty()) {
        return QString();
    }

    return album->path() + "/album.nfo";
}

bool XbmcXml::saveArtist(Artist *artist)
{
    QByteArray xmlContent = getArtistXml(artist);

    if (artist->path().isEmpty()) {
        return false;
    }

    artist->setNfoContent(xmlContent);
    Manager::instance()->database()->update(artist);

    QString fileName = nfoFilePath(artist);
    if (fileName.isEmpty()) {
        return false;
    }

    QDir saveFileDir = QFileInfo(fileName).dir();
    if (!saveFileDir.exists()) {
        saveFileDir.mkpath(".");
    }
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "File could not be openend";
        return false;
    }
    file.write(xmlContent);
    file.close();

    for (const auto imageType : Artist::imageTypes()) {
        DataFileType dataFileType = DataFile::dataFileTypeForImageType(imageType);

        if (artist->imagesToRemove().contains(imageType)) {
            foreach (DataFile dataFile, Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName = dataFile.saveFileName(QString());
                if (!saveFileName.isEmpty()) {
                    QFile(artist->path() + "/" + saveFileName).remove();
                }
            }
        }

        if (!artist->rawImage(imageType).isNull()) {
            foreach (DataFile dataFile, Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName = dataFile.saveFileName(QString());
                saveFile(artist->path() + "/" + saveFileName, artist->rawImage(imageType));
            }
        }
    }

    foreach (const QString &file, artist->extraFanartsToRemove())
        QFile::remove(file);
    QDir dir(artist->path() + "/extrafanart");
    if (!dir.exists() && !artist->extraFanartImagesToAdd().isEmpty()) {
        QDir(artist->path()).mkdir("extrafanart");
    }
    foreach (QByteArray img, artist->extraFanartImagesToAdd()) {
        int num = 1;
        while (QFileInfo(dir.absolutePath() + "/" + QString("fanart%1.jpg").arg(num)).exists()) {
            ++num;
        }
        saveFile(dir.absolutePath() + "/" + QString("fanart%1.jpg").arg(num), img);
    }

    return true;
}

bool XbmcXml::saveAlbum(Album *album)
{
    QByteArray xmlContent = getAlbumXml(album);

    if (album->path().isEmpty()) {
        return false;
    }

    album->setNfoContent(xmlContent);
    Manager::instance()->database()->update(album);

    QString fileName = nfoFilePath(album);
    if (fileName.isEmpty()) {
        return false;
    }

    QDir saveFileDir = QFileInfo(fileName).dir();
    if (!saveFileDir.exists()) {
        saveFileDir.mkpath(".");
    }
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "File could not be openend";
        return false;
    }
    file.write(xmlContent);
    file.close();

    for (const auto imageType : Album::imageTypes()) {
        DataFileType dataFileType = DataFile::dataFileTypeForImageType(imageType);

        if (album->imagesToRemove().contains(imageType)) {
            foreach (DataFile dataFile, Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName = dataFile.saveFileName(QString());
                if (!saveFileName.isEmpty()) {
                    QFile(album->path() + "/" + saveFileName).remove();
                }
            }
        }

        if (!album->rawImage(imageType).isNull()) {
            foreach (DataFile dataFile, Settings::instance()->dataFiles(dataFileType)) {
                QString saveFileName = dataFile.saveFileName(QString());
                saveFile(album->path() + "/" + saveFileName, album->rawImage(imageType));
            }
        }
    }

    if (album->bookletModel()->hasChanged()) {
        QDir dir(album->path() + "/booklet");
        if (!dir.exists()) {
            QDir(album->path()).mkdir("booklet");
        }

        // @todo: get filename from settings
        foreach (Image *image, album->bookletModel()->images()) {
            if (image->deletion() && !image->fileName().isEmpty()) {
                QFile::remove(image->fileName());
            } else if (!image->deletion()) {
                image->load();
            }
        }
        int bookletNum = 1;
        foreach (Image *image, album->bookletModel()->images()) {
            if (!image->deletion()) {
                QString fileName =
                    album->path() + "/booklet/booklet" + QString("%1").arg(bookletNum, 2, 10, QChar('0')) + ".jpg";
                QFile file(fileName);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(image->rawData());
                    file.close();
                }
                bookletNum++;
            }
        }
    }

    return true;
}

QByteArray XbmcXml::getArtistXml(Artist *artist)
{
    Kodi::ArtistXmlWriter writer(*artist);
    return writer.getArtistXml();
}

QByteArray XbmcXml::getAlbumXml(Album *album)
{
    QDomDocument doc;
    doc.setContent(album->nfoContent());
    if (album->nfoContent().isEmpty()) {
        QDomNode node = doc.createProcessingInstruction("xml", R"(version="1.0" encoding="UTF-8" standalone="yes")");
        doc.insertBefore(node, doc.firstChild());
        doc.appendChild(doc.createElement("album"));
    }

    QDomElement albumElem = doc.elementsByTagName("album").at(0).toElement();

    if (!album->mbReleaseGroupId().isEmpty()) {
        setTextValue(doc, "musicBrainzReleaseGroupID", album->mbReleaseGroupId());
    } else {
        removeChildNodes(doc, "musicBrainzReleaseGroupID");
    }
    if (!album->mbAlbumId().isEmpty()) {
        setTextValue(doc, "musicBrainzAlbumID", album->mbAlbumId());
    } else {
        removeChildNodes(doc, "musicBrainzAlbumID");
    }
    if (!album->allMusicId().isEmpty()) {
        setTextValue(doc, "allmusicid", album->allMusicId());
    } else {
        removeChildNodes(doc, "allmusicid");
    }
    setTextValue(doc, "title", album->title());
    setTextValue(doc, "artist", album->artist());
    setTextValue(doc, "genre", album->genres().join(" / "));
    setListValue(doc, "style", album->styles());
    setListValue(doc, "mood", album->moods());
    setTextValue(doc, "review", album->review());
    setTextValue(doc, "label", album->label());
    setTextValue(doc, "releasedate", album->releaseDate());
    if (album->rating() > 0) {
        setTextValue(doc, "rating", QString("%1").arg(album->rating()));
    } else {
        removeChildNodes(doc, "rating");
    }
    if (album->year() > 0) {
        setTextValue(doc, "year", QString("%1").arg(album->year()));
    } else {
        removeChildNodes(doc, "year");
    }

    if (Settings::instance()->advanced()->writeThumbUrlsToNfo()) {
        removeChildNodes(doc, "thumb");

        foreach (const Poster &poster, album->images(ImageType::AlbumThumb)) {
            QDomElement elem = doc.createElement("thumb");
            elem.setAttribute("preview", poster.thumbUrl.toString());
            elem.appendChild(doc.createTextNode(poster.originalUrl.toString()));
            appendXmlNode(doc, elem);
        }
    }

    return doc.toByteArray(4);
}

QDomElement XbmcXml::setTextValue(QDomDocument &doc, const QString &name, const QString &value)
{
    if (!doc.elementsByTagName(name).isEmpty()) {
        if (!doc.elementsByTagName(name).at(0).firstChild().isText()) {
            QDomText t = doc.createTextNode(value);
            doc.elementsByTagName(name).at(0).appendChild(t);
            return doc.elementsByTagName(name).at(0).toElement();
        } else {
            doc.elementsByTagName(name).at(0).firstChild().setNodeValue(value);
            return doc.elementsByTagName(name).at(0).toElement();
        }
    } else {
        return addTextValue(doc, name, value);
    }
}

void XbmcXml::setListValue(QDomDocument &doc, const QString &name, const QStringList &values)
{
    QDomNode rootNode = doc.firstChild();
    while ((rootNode.nodeName() == "xml" || rootNode.isComment()) && !rootNode.isNull()) {
        rootNode = rootNode.nextSibling();
    }
    QDomNodeList childNodes = rootNode.childNodes();
    QList<QDomNode> nodesToRemove;
    for (int i = 0, n = childNodes.count(); i < n; ++i) {
        if (childNodes.at(i).nodeName() == name) {
            nodesToRemove.append(childNodes.at(i));
        }
    }
    foreach (QDomNode node, nodesToRemove)
        rootNode.removeChild(node);
    foreach (const QString &style, values)
        addTextValue(doc, name, style);
}

QDomElement XbmcXml::addTextValue(QDomDocument &doc, const QString &name, const QString &value)
{
    QDomElement elem = doc.createElement(name);
    elem.appendChild(doc.createTextNode(value));
    appendXmlNode(doc, elem);
    return elem;
}

void XbmcXml::appendXmlNode(QDomDocument &doc, QDomNode &node)
{
    QDomNode rootNode = doc.firstChild();
    while ((rootNode.nodeName() == "xml" || rootNode.isComment()) && !rootNode.isNull()) {
        rootNode = rootNode.nextSibling();
    }
    rootNode.appendChild(node);
}

void XbmcXml::removeChildNodes(QDomDocument &doc, const QString &name)
{
    QDomNode rootNode = doc.firstChild();
    while ((rootNode.nodeName() == "xml" || rootNode.isComment()) && !rootNode.isNull()) {
        rootNode = rootNode.nextSibling();
    }
    QDomNodeList childNodes = rootNode.childNodes();
    QList<QDomNode> nodesToRemove;
    for (int i = 0, n = childNodes.count(); i < n; ++i) {
        if (childNodes.at(i).nodeName() == name) {
            nodesToRemove.append(childNodes.at(i));
        }
    }
    foreach (QDomNode node, nodesToRemove) {
        rootNode.removeChild(node);
    }
}

void XbmcXml::loadBooklets(Album *album)
{
    // @todo: get filename from settings
    if (!album->bookletModel()->images().isEmpty()) {
        return;
    }

    QDir dir(album->path() + "/booklet");
    QStringList filters{"*.jpg", "*.jpeg", "*.JPEG", "*.Jpeg", "*.JPeg"};
    for (const QString &file : dir.entryList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        auto img = new Image;
        img->setFileName(QDir::toNativeSeparators(dir.path() + "/" + file));
        album->bookletModel()->addImage(img);
    }
    album->bookletModel()->setHasChanged(false);
}
