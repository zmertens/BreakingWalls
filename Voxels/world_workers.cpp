#include "world.h"

#include "chunk_worker.h"

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// Worker threads
// ---------------------------------------------------------------------------

bool world::worker_run(worker *w) const noexcept
{
    while (true)
    {
        std::unique_lock<std::mutex> my_lock(w->mtx);
        w->cnd.wait(my_lock, [w]
                    { return w->state == WorkerState::BUSY || w->should_stop; });
        if (w->should_stop)
        {
            break;
        }
        worker_item *item = &w->item;
        my_lock.unlock();

        if (item->load)
        {
            this->load_chunk(item);
        }
        chunk::compute(item);

        std::lock_guard<std::mutex> done_lock(w->mtx);
        w->state = WorkerState::DONE;
    }
    return true;
}

void world::init_worker_threads() noexcept
{
    chunk_workers.reserve(CHUNK_WORKERS_TOTAL);
    for (int i = 0; i < CHUNK_WORKERS_TOTAL; i++)
    {
        auto w = std::make_unique<worker>();
        w->index       = i;
        w->state       = WorkerState::IDLE;
        w->should_stop = false;
        chunk_workers.emplace_back(std::move(w));
        worker *worker_ptr = chunk_workers.back().get();
        worker_ptr->thrd = std::thread([this, worker_ptr]()
                                       { this->worker_run(worker_ptr); });
    }
}

void world::cleanup_worker_threads() noexcept
{
    for (auto &&w : chunk_workers)
    {
        std::lock_guard<std::mutex> lock(w->mtx);
        w->should_stop = true;
        w->cnd.notify_one();
    }
    for (auto &&w : chunk_workers)
    {
        // A worker still holds pointers into its job until the thread exits.
        if (w->thrd.joinable())
        {
            w->thrd.join();
        }
        SDL_free(w->item.data);
        for (int a = 0; a < 3; ++a)
        {
            for (int b = 0; b < 3; ++b)
            {
                delete w->item.block_maps[a][b];
                delete w->item.light_maps[a][b];
            }
        }
        w->item = {};
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "worker thread %d finished!", w->index);
    }
    chunk_workers.clear();
}
