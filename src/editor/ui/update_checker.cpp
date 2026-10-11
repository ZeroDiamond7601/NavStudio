#include "editor/ui/update_checker.h"
#include "editor/version.h"
#include "imgui.h"

#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#else
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#endif

UpdateChecker::UpdateChecker()
    : m_currentVersion(NAVSTUDIO_VERSION_STRING) {
}

UpdateChecker::~UpdateChecker() {
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

UpdateChecker::State UpdateChecker::GetState() const {
    return m_state.load();
}

bool UpdateChecker::IsChecking() const {
    return m_state.load() == State::Checking;
}

bool UpdateChecker::HasUpdate() const {
    return m_state.load() == State::UpdateAvailable;
}

const UpdateChecker::ReleaseInfo& UpdateChecker::GetLatestRelease() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestRelease;
}

const std::string& UpdateChecker::GetErrorMessage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_errorMessage;
}

void UpdateChecker::CheckForUpdates(bool isManual, int64_t* lastCheckTime, bool checkCooldown) {
    if (m_state.load() == State::Checking) {
        if (isManual) {
            m_showModal = true;
        }
        return;
    }

    if (!isManual && checkCooldown && lastCheckTime) {
        int64_t nowSec = static_cast<int64_t>(std::time(nullptr));
        int64_t elapsed = nowSec - *lastCheckTime;
        const int64_t kCooldownSeconds = 24 * 60 * 60; // 24 hours
        if (*lastCheckTime > 0 && elapsed < kCooldownSeconds && elapsed >= 0) {
            return;
        }
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }

    m_isManualCheck = isManual;
    if (isManual) {
        m_showModal = true;
    }
    m_state.store(State::Checking);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_errorMessage.clear();
    }

    m_worker = std::thread(&UpdateChecker::WorkerThread, this, isManual, lastCheckTime);
}

void UpdateChecker::WorkerThread(bool isManual, int64_t* lastCheckTime) {
    std::string json;
    std::string err;
    bool fetchSuccess = FetchLatestReleaseJson(json, err);

    if (lastCheckTime) {
        *lastCheckTime = static_cast<int64_t>(std::time(nullptr));
    }

    if (!fetchSuccess) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_errorMessage = err;
        m_state.store(State::Error);
        return;
    }

    ReleaseInfo rel;
    if (!ParseReleaseJson(json, rel)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_errorMessage = "Failed to parse release information from GitHub response.";
        m_state.store(State::Error);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestRelease = rel;
    }

    bool newer = IsVersionNewer(rel.version, m_currentVersion);
    if (newer) {
        m_state.store(State::UpdateAvailable);
        if (!isManual) {
            m_pendingNotification.store(true);
        }
    } else {
        m_state.store(State::UpToDate);
    }
}

bool UpdateChecker::FetchLatestReleaseJson(std::string& outJson, std::string& outError) {
#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(
        L"NavStudio-UpdateChecker/1.6",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        outError = "Failed to initialize WinHTTP session (Error: " + std::to_string(GetLastError()) + ")";
        return false;
    }

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        L"api.github.com",
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );
    if (!hConnect) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hSession);
        outError = "Failed to connect to api.github.com (Error: " + std::to_string(err) + ")";
        return false;
    }

    LPCWSTR acceptTypes[] = { L"application/vnd.github.v3+json", L"application/json", NULL };
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        L"/repos/ZeroDiamond7601/NavStudio/releases/latest",
        NULL,
        WINHTTP_NO_REFERER,
        acceptTypes,
        WINHTTP_FLAG_SECURE
    );
    if (!hRequest) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Failed to create HTTPS request (Error: " + std::to_string(err) + ")";
        return false;
    }

    LPCWSTR headers = L"User-Agent: NavStudio-UpdateChecker\r\n";
    BOOL sent = WinHttpSendRequest(
        hRequest,
        headers,
        static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );

    if (!sent || !WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Failed to communicate with GitHub API (Error: " + std::to_string(err) + ")";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        if (statusCode == 403) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            outError = "GitHub API rate limit exceeded (HTTP 403). Please try again later.";
            return false;
        } else if (statusCode == 404) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            outError = "No releases found on GitHub repository (HTTP 404).";
            return false;
        } else if (statusCode != 200) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            outError = "GitHub API returned HTTP status " + std::to_string(statusCode);
            return false;
        }
    }

    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1, 0);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            responseBody.append(buffer.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (responseBody.empty()) {
        outError = "Received empty response from GitHub API.";
        return false;
    }

    outJson = responseBody;
    return true;
#else
    std::string cmd = "curl -s -H \"User-Agent: NavStudio-UpdateChecker\" -H \"Accept: application/vnd.github.v3+json\" --max-time 8 \"https://api.github.com/repos/ZeroDiamond7601/NavStudio/releases/latest\"";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        outError = "Failed to run curl command on Linux.";
        return false;
    }

    char buffer[1024];
    std::string responseBody;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        responseBody += buffer;
    }
    int rc = pclose(pipe);
    if (rc != 0 || responseBody.empty()) {
        outError = "Network error while querying GitHub via curl.";
        return false;
    }

    if (responseBody.find("\"message\": \"API rate limit exceeded") != std::string::npos) {
        outError = "GitHub API rate limit exceeded (HTTP 403). Please try again later.";
        return false;
    }

    outJson = responseBody;
    return true;
#endif
}

std::string UpdateChecker::ExtractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos + pattern.length());
    if (colonPos == std::string::npos) return "";

    size_t quoteStart = json.find('"', colonPos + 1);
    if (quoteStart == std::string::npos) return "";

    std::string result;
    bool escaped = false;
    for (size_t i = quoteStart + 1; i < json.length(); ++i) {
        char c = json[i];
        if (escaped) {
            if (c == 'n') result += '\n';
            else if (c == 'r') result += '\r';
            else if (c == 't') result += '\t';
            else if (c == '"') result += '"';
            else if (c == '\\') result += '\\';
            else if (c == '/') result += '/';
            else result += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

std::string UpdateChecker::CleanVersionString(const std::string& ver) {
    std::string clean = ver;
    while (!clean.empty() && (clean.front() == ' ' || clean.front() == 'v' || clean.front() == 'V')) {
        clean.erase(clean.begin());
    }
    while (!clean.empty() && clean.back() == ' ') {
        clean.pop_back();
    }
    return clean;
}

static std::vector<int> ParseVersionComponents(const std::string& ver) {
    std::vector<int> components;
    size_t i = 0;
    while (i < ver.length()) {
        while (i < ver.length() && !isdigit(static_cast<unsigned char>(ver[i]))) {
            ++i;
        }
        if (i >= ver.length()) break;
        int val = 0;
        while (i < ver.length() && isdigit(static_cast<unsigned char>(ver[i]))) {
            val = val * 10 + (ver[i] - '0');
            ++i;
        }
        components.push_back(val);
        if (i < ver.length() && ver[i] == '.') {
            ++i;
        } else {
            break;
        }
    }
    return components;
}

bool UpdateChecker::IsVersionNewer(const std::string& remoteVersion, const std::string& currentVersion) {
    std::vector<int> rComp = ParseVersionComponents(remoteVersion);
    std::vector<int> cComp = ParseVersionComponents(currentVersion);

    size_t count = std::max(rComp.size(), cComp.size());
    for (size_t i = 0; i < count; ++i) {
        int r = (i < rComp.size()) ? rComp[i] : 0;
        int c = (i < cComp.size()) ? cComp[i] : 0;
        if (r > c) return true;
        if (r < c) return false;
    }
    return false;
}

bool UpdateChecker::ParseReleaseJson(const std::string& json, ReleaseInfo& outRelease) {
    outRelease.tagName = ExtractJsonString(json, "tag_name");
    outRelease.releaseName = ExtractJsonString(json, "name");
    outRelease.htmlUrl = ExtractJsonString(json, "html_url");
    outRelease.publishedAt = ExtractJsonString(json, "published_at");
    outRelease.body = ExtractJsonString(json, "body");

    if (outRelease.tagName.empty()) {
        return false;
    }

    outRelease.version = CleanVersionString(outRelease.tagName);
    if (outRelease.releaseName.empty()) {
        outRelease.releaseName = outRelease.tagName;
    }
    if (outRelease.htmlUrl.empty()) {
        outRelease.htmlUrl = "https://github.com/ZeroDiamond7601/NavStudio/releases";
    }

    return true;
}

void UpdateChecker::OpenUrlInBrowser(const std::string& url) {
    if (url.empty()) return;
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
    int ret = system(cmd.c_str());
    (void)ret;
#endif
}

void UpdateChecker::RenderModal() {
    if (!m_showModal) return;

    ImGui::OpenPopup("Check for Updates##UpdateModal");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Check for Updates##UpdateModal", &m_showModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        State curState = m_state.load();
        ReleaseInfo rel;
        std::string err;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            rel = m_latestRelease;
            err = m_errorMessage;
        }

        if (curState == State::Checking) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Checking for updates on GitHub...");
            ImGui::Spacing();
            ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(480, 6), "");
            ImGui::Spacing();
            ImGui::TextDisabled("Connecting to api.github.com/repos/ZeroDiamond7601/NavStudio...");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Cancel", ImVec2(100, 26))) {
                m_showModal = false;
                ImGui::CloseCurrentPopup();
            }
        } else if (curState == State::UpToDate) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "NavStudio is up to date!");
            ImGui::Spacing();
            ImGui::Text("Installed Version:  %s", m_currentVersion.c_str());
            ImGui::Text("Latest Release:     %s", rel.version.empty() ? m_currentVersion.c_str() : rel.version.c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("You are running the most recent version of NavStudio.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Check Again", ImVec2(120, 26))) {
                CheckForUpdates(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("View Releases on GitHub", ImVec2(180, 26))) {
                OpenUrlInBrowser("https://github.com/ZeroDiamond7601/NavStudio/releases");
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(100, 26))) {
                m_showModal = false;
                ImGui::CloseCurrentPopup();
            }
        } else if (curState == State::UpdateAvailable) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "A new version of NavStudio is available!");
            ImGui::Spacing();

            ImGui::Text("Current Version: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", m_currentVersion.c_str());

            ImGui::Text("New Version:     ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "%s (%s)", rel.version.c_str(), rel.releaseName.c_str());

            if (!rel.publishedAt.empty()) {
                std::string pubDate = rel.publishedAt;
                size_t tPos = pubDate.find('T');
                if (tPos != std::string::npos) pubDate = pubDate.substr(0, tPos);
                ImGui::TextDisabled("Release Date: %s", pubDate.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Release Notes & Changelog:");
            ImGui::BeginChild("ChangelogBox", ImVec2(480, 160), true);
            if (!rel.body.empty()) {
                ImGui::TextWrapped("%s", rel.body.c_str());
            } else {
                ImGui::TextDisabled("No release notes provided for this release.");
            }
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.92f, 1.0f));
            if (ImGui::Button("Download Update", ImVec2(140, 28))) {
                OpenUrlInBrowser(rel.htmlUrl);
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ImGui::Button("Copy Link", ImVec2(90, 28))) {
                ImGui::SetClipboardText(rel.htmlUrl.c_str());
            }

            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(90, 28))) {
                m_showModal = false;
                ImGui::CloseCurrentPopup();
            }
        } else if (curState == State::Error) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Failed to check for updates");
            ImGui::Spacing();
            ImGui::TextWrapped("%s", err.empty() ? "An unexpected network error occurred." : err.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Retry", ImVec2(100, 26))) {
                CheckForUpdates(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Open GitHub Releases", ImVec2(170, 26))) {
                OpenUrlInBrowser("https://github.com/ZeroDiamond7601/NavStudio/releases");
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(100, 26))) {
                m_showModal = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::Spacing();
            ImGui::Text("Check GitHub for the latest NavStudio release.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Check for Updates Now", ImVec2(180, 26))) {
                CheckForUpdates(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(100, 26))) {
                m_showModal = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}
