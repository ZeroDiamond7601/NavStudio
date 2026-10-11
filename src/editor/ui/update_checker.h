#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

class UpdateChecker {
public:
    enum class State {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Error
    };

    struct ReleaseInfo {
        std::string tagName;
        std::string version;
        std::string releaseName;
        std::string htmlUrl;
        std::string publishedAt;
        std::string body;
    };

    UpdateChecker();
    ~UpdateChecker();

    // Trigger update check. If isManual is true, bypasses cooldown and shows modal.
    // If isManual is false, checks if 24 hours (86400s) elapsed since lastCheckTime.
    void CheckForUpdates(bool isManual, int64_t* lastCheckTime = nullptr, bool checkCooldown = true);

    // Render Dear ImGui update modal
    void RenderModal();

    // Accessors
    State GetState() const;
    bool IsChecking() const;
    bool HasUpdate() const;
    const ReleaseInfo& GetLatestRelease() const;
    const std::string& GetErrorMessage() const;

    bool HasPendingNotification() const { return m_pendingNotification.load(); }
    void ClearNotification() { m_pendingNotification.store(false); }

    bool IsModalOpen() const { return m_showModal; }
    void OpenModal() { m_showModal = true; }
    void CloseModal() { m_showModal = false; }

    static bool IsVersionNewer(const std::string& remoteVersion, const std::string& currentVersion);
    static std::string CleanVersionString(const std::string& ver);
    static void OpenUrlInBrowser(const std::string& url);

private:
    void WorkerThread(bool isManual, int64_t* lastCheckTime);
    static bool FetchLatestReleaseJson(std::string& outJson, std::string& outError);
    static bool ParseReleaseJson(const std::string& json, ReleaseInfo& outRelease);
    static std::string ExtractJsonString(const std::string& json, const std::string& key);

    std::atomic<State> m_state{State::Idle};
    mutable std::mutex m_mutex;
    std::thread m_worker;
    ReleaseInfo m_latestRelease;
    std::string m_errorMessage;
    std::string m_currentVersion;

    bool m_showModal{false};
    bool m_isManualCheck{false};
    std::atomic<bool> m_pendingNotification{false};
};

#endif // UPDATE_CHECKER_H
