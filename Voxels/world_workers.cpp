#include "world.h"

#include "geometries.h"
#include "worker_traits.h"

#include <SDL3/SDL.h>

#include <functional>
#include <vector>

bool world::worker_run(worker *w, const std::function<void(worker_item *)> &terrain) const noexcept
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
        terrain(item);

        std::lock_guard<std::mutex> done_lock(w->mtx);
        w->state = WorkerState::DONE;
    }
    return true;
}

void world::init_worker_threads() noexcept
{
    for (int i = 0; i < chunk_thread_pool.get_thread_count(); i++)
    {
        auto w = std::make_unique<worker>();
        w->index = i;
        w->state = WorkerState::IDLE;
        w->should_stop = false;
        chunk_workers.emplace_back(std::move(w));
        worker *worker_ptr = chunk_workers.back().get();
        worker_ptr->thrd = std::thread([this, worker_ptr]()
                                       { this->worker_run(static_cast<worker *>(worker_ptr), [this](worker_item *item)
                                                          { this->terrain(item); }); });
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
