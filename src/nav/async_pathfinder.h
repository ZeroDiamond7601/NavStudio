#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "nav_types.h"
#include "nav_path.h"

struct AsyncPathRequest {
    int taskId;
    Vector3 start;
    Vector3 goal;
    int flags;
};

struct AsyncPathResult {
    int taskId;
    int pathId;
    float length;
    bool success;
};

class AsyncPathManager {
public:
    static AsyncPathManager& Get();

    void Initialize();
    void Shutdown();
    void ClearQueue();
    void ClearAndDrain();

    int EnqueueRequest(const Vector3& start, const Vector3& goal, int flags = 0);
    void ProcessCompleted();

private:
    AsyncPathManager();
    ~AsyncPathManager();

    void WorkerLoop();

private:
    std::atomic<bool> m_running;
    std::thread m_workerThread;
    std::mutex m_requestMutex;
    std::mutex m_workerJobMutex;
    std::condition_variable m_cv;
    std::queue<AsyncPathRequest> m_requests;

    std::mutex m_resultMutex;
    std::vector<AsyncPathResult> m_completed;

    std::atomic<int> m_nextTaskId;
    std::atomic<uint32_t> m_generation;
};
