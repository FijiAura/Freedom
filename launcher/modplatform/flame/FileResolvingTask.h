#pragma once

#include "tasks/Task.h"
#include "net/NetJob.h"
#include "PackManifest.h"
#include <QMap>
#include <QNetworkAccessManager>
#include <memory>

namespace Flame {

class FileResolvingTask : public Task {
    Q_OBJECT

public:
    /**
     * Constructor to initialize the FileResolvingTask.
     * @param network The network access manager to use for network operations.
     * @param toProcess The manifest containing files to process.
     */
    explicit FileResolvingTask(const shared_qobject_ptr<QNetworkAccessManager>& network, Manifest& toProcess);

    /**
     * Virtual destructor to ensure proper cleanup of derived classes.
     */
    virtual ~FileResolvingTask() {}

    /**
     * Checks if the task can be aborted.
     * @return Always returns true.
     */
    bool canAbort() const override { return true; }

    /**
     * Attempts to abort the task.
     * @return True if the task was successfully aborted, false otherwise.
     */
    bool abort() override;

    /**
     * Returns the processed manifest.
     * @return A const reference to the processed manifest.
     */
    const Manifest& getResults() const {
        return m_toProcess;
    }

protected:
    /**
     * Executes the task. This method should contain the main logic of the task.
     */
    virtual void executeTask() override;

protected slots:
    /**
     * Slot to handle the completion of network jobs.
     */
    void netJobFinished();

private:
    shared_qobject_ptr<QNetworkAccessManager> m_network;  // Network access manager
    Manifest m_toProcess;  // Manifest containing files to process
    std::shared_ptr<QByteArray> result;  // Pointer to store the result data
    NetJob::Ptr m_dljob;  // Pointer to the current download job
    QMap<File*, QByteArray*> blockedProjects;  // Map of blocked projects and their data

    /**
     * Handles the completion of Modrinth checks.
     */
    void modrinthCheckFinished();
};

} // namespace Flame
