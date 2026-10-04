#include "async_pathfinder.h"
#include "nav_file.h"
#include "../amxx/amxx_api.h"

extern NavMesh g_nav;
extern BSPFile g_bsp;

// Forward declaration of callback on main thread
void DispatchAsyncPathResult(const AsyncPathResult& result);

AsyncPathManager& AsyncPathManager::Get() {
    static AsyncPathManager s_instance;
    return s_instance;
}

AsyncPathManager::AsyncPathManager()
    : m_running(false), m_nextTaskId(1), m_generation(0) {
}

AsyncPathManager::~AsyncPathManager() {
    Shutdown();
}

void AsyncPathManager::Initialize() {
    if (m_running) return;

    m_running = true;
    m_workerThread = std::thread(&AsyncPathManager::WorkerLoop, this);
}

void AsyncPathManager::Shutdown() {
    if (!m_running) return;

    m_running = false;
    m_cv.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    ClearQueue();
}

void AsyncPathManager::ClearQueue() {
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        std::queue<AsyncPathRequest> empty;
        std::swap(m_requests, empty);
    }
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_completed.clear();
    }
}

void AsyncPathManager::ClearAndDrain() {
    m_generation.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        std::queue<AsyncPathRequest> empty;
        std::swap(m_requests, empty);
    }
    {
        std::lock_guard<std::mutex> lock(m_workerJobMutex);
    }
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_completed.clear();
    }
}

int AsyncPathManager::EnqueueRequest(const Vector3& start, const Vector3& goal, int flags) {
    if (!m_running || !g_nav.IsLoaded()) return 0;

    int taskId = m_nextTaskId++;
    AsyncPathRequest req;
    req.taskId = taskId;
    req.start = start;
    req.goal = goal;
    req.flags = flags;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requests.push(req);
    }
    m_cv.notify_one();

    return taskId;
}

void AsyncPathManager::WorkerLoop() {
    while (m_running) {
        AsyncPathRequest req;
        uint32_t reqGen = 0;
        {
            std::unique_lock<std::mutex> lock(m_requestMutex);
            m_cv.wait(lock, [this] {
                return !m_running || !m_requests.empty();
            });

            if (!m_running) break;

            req = m_requests.front();
            m_requests.pop();
            reqGen = m_generation.load();
        }

        AsyncPathResult res;
        res.taskId = req.taskId;
        res.pathId = 0;
        res.length = 0.0f;
        res.success = false;

        {
            std::lock_guard<std::mutex> jobLock(m_workerJobMutex);

            if (m_generation.load() == reqGen && g_nav.IsLoaded()) {
                NavPath path;
                bool ok = NavPathFinder::BuildPath(
                    g_nav.GetGrid(), req.start, req.goal, path, req.flags,
                    g_bsp.IsLoaded() ? &g_bsp : nullptr
                );

                if (ok && path.IsValid() && m_generation.load() == reqGen) {
                    res.success = true;
                    res.length = path.GetLength();

                    int pathId = g_nextPathId++;
                    res.pathId = pathId;

                    extern std::mutex g_activePathsMutex;
                    {
                        std::lock_guard<std::mutex> lock(g_activePathsMutex);
                        g_activePaths[pathId] = std::move(path);
                    }
                }
            }
        }

        if (m_generation.load() == reqGen) {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_completed.push_back(res);
        }
    }
}

void AsyncPathManager::ProcessCompleted() {
    std::vector<AsyncPathResult> toProcess;
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        if (m_completed.empty()) return;
        toProcess.swap(m_completed);
    }

    for (const auto& res : toProcess) {
        DispatchAsyncPathResult(res);
    }
}
