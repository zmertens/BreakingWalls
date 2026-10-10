#ifndef CHUNK_WORKER_H
#define CHUNK_WORKER_H

#include <condition_variable>
#include <mutex>
#include <thread>

#include "chunk.h"

enum class WorkerState : int { IDLE = 0, BUSY = 1, DONE = 2 };

struct worker
{
    int index{};
    WorkerState state{WorkerState::IDLE};
    std::thread thrd;
    std::mutex mtx;
    std::condition_variable cnd;
    worker_item item;
    bool should_stop{};
};

#endif // CHUNK_WORKER_H
