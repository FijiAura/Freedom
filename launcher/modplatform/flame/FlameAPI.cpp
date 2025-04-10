auto FlameAPI::getModFileChangelog(int modId, int fileId) -> QString {
    return fetchStringFromAPI(
        QString("https://api.curseforge.com/v1/mods/%1/files/%2/changelog").arg(modId).arg(fileId),
        "Flame::FileChangelog"
    );
}

QString FlameAPI::fetchStringFromAPI(const QString& url, const QString& jobName) {
    QEventLoop lock;
    QString result;
    auto* netJob = new NetJob(jobName, APPLICATION->network());
    auto response = std::make_unique<QByteArray>();

    netJob->addNetAction(Net::Download::makeByteArray(url, response.get()));

    QObject::connect(netJob, &NetJob::succeeded, [&result, &response] {
        QJsonParseError parse_error{};
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qWarning() << "Error while parsing JSON response from" << jobName << "at" << parse_error.offset
                       << "reason:" << parse_error.errorString();
            qWarning() << *response;
            return;
        }
        result = Json::ensureString(doc.object(), "data");
    });

    QObject::connect(netJob, &NetJob::finished, [&lock, response = std::move(response), netJob]() mutable {
        netJob->deleteLater();
        lock.quit();
    });

    netJob->start();
    lock.exec();

    return result;
}
