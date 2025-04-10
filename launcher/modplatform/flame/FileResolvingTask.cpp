#include "FileResolvingTask.h"
#include "Json.h"
#include "net/Upload.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <algorithm>
#include <numeric>

namespace Flame {

FileResolvingTask::FileResolvingTask(const shared_qobject_ptr<QNetworkAccessManager>& network, Manifest& toProcess)
    : m_network(network), m_toProcess(toProcess) {}

bool FileResolvingTask::abort() {
    return m_dljob ? m_dljob->abort() : true;
}

void FileResolvingTask::executeTask() {
    setStatus(tr("Resolving mod IDs..."));
    setProgress(0, 3);
    m_dljob = new NetJob("Mod id resolver", m_network);
    result.reset(new QByteArray());

    QJsonObject jsonData;
    jsonData["fileIds"] = buildFileIdsJsonArray();

    QByteArray data = Json::toText(jsonData);
    auto upload = Net::Upload::makeByteArray(QUrl("https://api.curseforge.com/v1/mods/files"), result.get(), data);
    m_dljob->addNetAction(upload);

    connect(m_dljob.get(), &NetJob::finished, this, &FileResolvingTask::netJobFinished);
    m_dljob->start();
}

QJsonArray FileResolvingTask::buildFileIdsJsonArray() const {
    return QJsonArray::fromVariantList(std::accumulate(
        m_toProcess.files.begin(), m_toProcess.files.end(), QVariantList(),
        [](QVariantList& list, const File& file) {
            list.push_back(file.fileId);
            return list;
        }));
}

void FileResolvingTask::netJobFinished() {
    setProgress(1, 3);
    auto job = new NetJob("Modrinth check", m_network);
    blockedProjects.clear();

    QJsonDocument doc;
    try {
        doc = Json::requireDocument(*result);
    } catch (const JSONValidationError& e) {
        qDebug() << "FileResolvingTask: Json Validation error:" << e.what();
        emitFailed(e.what());
        return;
    }

    processCurseForgeResponse(doc, job);

    connect(job, &NetJob::finished, this, &FileResolvingTask::modrinthCheckFinished);
    job->start();
}

void FileResolvingTask::processCurseForgeResponse(const QJsonDocument& doc, NetJob* job) {
    auto array = Json::requireArray(doc.object()["data"]);
    for (const auto& fileValue : array) {
        auto fileObject = Json::requireObject(fileValue);
        auto fileId = Json::requireInteger(fileObject["id"]);
        auto& file = m_toProcess.files[fileId];

        try {
            file.parseFromObject(fileObject);
        } catch (const JSONValidationError& e) {
            qDebug() << "Blocked mod on curseforge" << file.fileName;
            handleBlockedMod(file, job);
        }
    }
}

void FileResolvingTask::handleBlockedMod(File& file, NetJob* job) {
    if (file.hash.isEmpty()) return;

    auto url = QString("https://api.modrinth.com/v2/version_file/%1?algorithm=sha1").arg(file.hash);
    auto output = new QByteArray();
    auto download = Net::Download::makeByteArray(QUrl(url), output);

    QObject::connect(download.get(), &Net::Download::succeeded, [&file]() {
        file.resolved = true;
    });

    job->addNetAction(download);
    blockedProjects.insert(&file, output);
}

void FileResolvingTask::modrinthCheckFinished() {
    setProgress(2, 3);
    qDebug() << "Finished with blocked mods:" << blockedProjects.size();

    processBlockedProjects();

    auto blockedFiles = filterBlockedFiles();
    if (!blockedFiles.isEmpty()) {
        fetchSlugsAndEmitSucceeded(blockedFiles);
    } else {
        emitSucceeded();
    }
}

void FileResolvingTask::processBlockedProjects() {
    for (auto it = blockedProjects.begin(); it != blockedProjects.end(); ++it) {
        auto file = it.key();
        auto bytes = it.value();

        if (!file->resolved) {
            delete bytes;
            continue;
        }

        QJsonDocument doc = QJsonDocument::fromJson(*bytes);
        auto obj = doc.object();
        auto array = Json::requireArray(obj, "files");

        for (const auto& fileValue : array) {
            auto fileObj = Json::requireObject(fileValue);
            if (Json::requireBoolean(fileObj, "primary")) {
                file->url = Json::requireUrl(fileObj, "url");
                qDebug() << "Found alternative on modrinth" << file->fileName;
                break;
            }
        }

        delete bytes;
    }
}

QList<File*> FileResolvingTask::filterBlockedFiles() const {
    QList<File*> blockedFiles;
    std::copy_if(blockedProjects.keys().begin(), blockedProjects.keys().end(),
                 std::back_inserter(blockedFiles), [](File* file) {
                     return !file->resolved;
                 });
    return blockedFiles;
}

void FileResolvingTask::fetchSlugsAndEmitSucceeded(const QList<File*>& blockedFiles) {
    auto slugJob = new NetJob("Slug Job", m_network);
    QVector<QByteArray> slugs(blockedFiles.size());

    for (int i = 0; i < blockedFiles.size(); ++i) {
        auto fileInfo = blockedFiles[i];
        auto url = QString("https://api.curseforge.com/v1/mods/%1").arg(fileInfo->projectId);
        auto download = Net::Download::makeByteArray(url, &slugs[i]);
        slugJob->addNetAction(download);
    }

    connect(slugJob, &NetJob::succeeded, this, [slugs, this, slugJob, blockedFiles]() {
        slugJob->deleteLater();
        updateFileWebsiteUrls(slugs, blockedFiles);
        emitSucceeded();
    });

    slugJob->start();
}

void FileResolvingTask::updateFileWebsiteUrls(const QVector<QByteArray>& slugs, const QList<File*>& blockedFiles) {
    for (int i = 0; i < slugs.size(); ++i) {
        auto json = QJsonDocument::fromJson(slugs[i]);
        auto baseUrl = Json::requireString(Json::requireObject(Json::requireObject(json.object(), "data"), "links"), "websiteUrl");
        auto file = blockedFiles[i];
        file->websiteUrl = QString("%1/download/%2").arg(baseUrl, QString::number(file->fileId));
    }
}

} // namespace Flame
