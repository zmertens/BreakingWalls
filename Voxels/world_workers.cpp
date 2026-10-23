#include "world.h"

#include "chunk.h"

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// Worker threads
// ---------------------------------------------------------------------------

bool world::worker_run(chunk::worker *w) const noexcept
{
    while (true)
    {
        std::unique_lock<std::mutex> my_lock(w->mtx);
        w->cnd.wait(my_lock, [w]
                    { return w->state == chunk::WorkerState::BUSY || w->should_stop; });
        if (w->should_stop)
        {
            break;
        }
        chunk::worker_item *item = &w->item;
        my_lock.unlock();

        if (item->load)
        {
            this->load_chunk(item);
        }
        chunk::compute(item);

        std::lock_guard<std::mutex> done_lock(w->mtx);
        w->state = chunk::WorkerState::DONE;
    }
    return true;
}

void world::init_worker_threads() noexcept
{
    for (int i = 0; i < chunk_thread_pool.get_thread_count(); i++)
    {
        auto w = std::make_unique<chunk::worker>();
        w->index = i;
        w->state = chunk::WorkerState::IDLE;
        w->should_stop = false;
        chunk_workers.emplace_back(std::move(w));
        chunk::worker *worker_ptr = chunk_workers.back().get();
        worker_ptr->thrd = std::thread([this, worker_ptr]()
                                       { this->worker_run(static_cast<chunk::worker *>(worker_ptr)); });
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
        w->item = {};
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "worker thread %d finished!", w->index);
    }
    chunk_workers.clear();
    chunk_thread_pool.wait_all();
}
