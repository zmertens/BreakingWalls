#include "db_manager.h"

#include "craft_map.h"

#include <sqlite/sqlite3.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <utility>

namespace
{
    constexpr auto SCHEMA =
        "create table if not exists state ("
        "   x float not null, y float not null, z float not null, rx float not null, ry float not null"
        ");"
        "create table if not exists block ("
        "    p int not null, q int not null, x int not null, y int not null, z int not null, w int not null"
        ");"
        "create table if not exists heightmap ("
        "    id integer primary key autoincrement,"
        "    name text not null,"
        "    side int not null,"
        "    max_height int not null,"
        "    data blob not null"
        ");"
        "create table if not exists preview_blocks ("
        "    preview_id int not null, p int not null, q int not null,"
        "    x int not null, y int not null, z int not null, w int not null,"
        "    timestamp datetime default current_timestamp"
        ");"
        "create table if not exists light ("
        "    p int not null, q int not null, x int not null, y int not null, z int not null, w int not null"
        ");"
        "create table if not exists key (p int not null, q int not null, key int not null);"
        "create table if not exists sign ("
        "    p int not null, q int not null, x int not null, y int not null, z int not null,"
        "    face int not null, text text not null"
        ");"
        "create unique index if not exists block_pqxyz_idx on block (p, q, x, y, z);"
        "create index if not exists block_pqw_idx on block (p, q, w);"
        "create index if not exists preview_blocks_id_idx on preview_blocks (preview_id);"
        "create index if not exists preview_blocks_pqxyz_idx on preview_blocks (p, q, x, y, z);"
        "create unique index if not exists light_pqxyz_idx on light (p, q, x, y, z);"
        "create unique index if not exists key_pq_idx on key (p, q);"
        "create unique index if not exists sign_xyzface_idx on sign (x, y, z, face);"
        "create index if not exists sign_pq_idx on sign (p, q);";

    constexpr auto FLUSH_SQL =
        "delete from state;"
        "delete from block;"
        "delete from light;"
        "delete from key;"
        "delete from sign;";

    // Prepared statement that finalizes itself.
    class statement
    {
    public:
        statement() = default;
        statement(const statement &) = delete;
        statement &operator=(const statement &) = delete;
        ~statement() { finalize(); }

        void finalize() noexcept
        {
            sqlite3_finalize(m_stmt);
            m_stmt = nullptr;
        }

        bool prepare(sqlite3 *conn, const char *sql)
        {
            if (sqlite3_prepare_v2(conn, sql, -1, &m_stmt, nullptr) != SQLITE_OK)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SQLite prepare failed: %s\n", sqlite3_errmsg(conn));
                return false;
            }
            return true;
        }

        // Resets the statement and binds consecutive integer parameters starting at 1.
        template <typename... Ints>
        sqlite3_stmt *bind(const Ints... values)
        {
            sqlite3_reset(m_stmt);
            int index = 1;
            (sqlite3_bind_int(m_stmt, index++, values), ...);
            return m_stmt;
        }

        // Runs a statement that returns no rows.
        void run()
        {
            if (const int rc = sqlite3_step(m_stmt); rc != SQLITE_DONE && rc != SQLITE_ROW)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SQLite step failed: %s\n",
                             sqlite3_errmsg(sqlite3_db_handle(m_stmt)));
            }
            sqlite3_reset(m_stmt);
        }

        sqlite3_stmt *get() const noexcept { return m_stmt; }

    private:
        sqlite3_stmt *m_stmt{};
    };

    struct job
    {
        enum class kind
        {
            BLOCK,
            BLOCKS,
            LIGHT,
            KEY,
            COMMIT,
            FLUSH,
            EXIT
        };

        explicit job(const kind t = kind::COMMIT, const int p_ = 0, const int q_ = 0, const int x_ = 0,
                     const int y_ = 0, const int z_ = 0, const int w_ = 0, const int key_ = 0)
            : type{t}, p{p_}, q{q_}, x{x_}, y{y_}, z{z_}, w{w_}, key{key_}
        {
        }

        kind type;
        int p, q, x, y, z, w, key;
        std::vector<db_manager::block_record> blocks;
        std::shared_ptr<std::promise<void>> done;
    };

    void exec(sqlite3 *conn, const char *sql)
    {
        char *error = nullptr;
        if (sqlite3_exec(conn, sql, nullptr, nullptr, &error) != SQLITE_OK)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SQLite exec failed: %s\n", error ? error : "unknown");
        }
        sqlite3_free(error);
    }
} // namespace

struct db_manager::impl
{
    sqlite3 *conn{};
    std::atomic<bool> opened{false};

    // Serializes every use of the connection and its statements.
    std::mutex db_mtx;

    std::mutex queue_mtx;
    std::condition_variable queue_cv;
    std::deque<job> queue;
    bool accepting{false};
    std::thread worker;

    statement insert_block, insert_light, insert_sign, delete_sign, delete_signs;
    statement load_blocks, load_lights, load_signs, get_key, set_key, query_near;
    statement insert_preview, load_preview, delete_previews, latest_preview, move_preview;

    bool prepare_all()
    {
        return insert_block.prepare(conn, "insert or replace into block (p, q, x, y, z, w) values (?, ?, ?, ?, ?, ?);") &&
               insert_light.prepare(conn, "insert or replace into light (p, q, x, y, z, w) values (?, ?, ?, ?, ?, ?);") &&
               insert_sign.prepare(conn, "insert or replace into sign (p, q, x, y, z, face, text) values (?, ?, ?, ?, ?, ?, ?);") &&
               delete_sign.prepare(conn, "delete from sign where x = ? and y = ? and z = ? and face = ?;") &&
               delete_signs.prepare(conn, "delete from sign where x = ? and y = ? and z = ?;") &&
               load_blocks.prepare(conn, "select x, y, z, w from block where p = ? and q = ?;") &&
               load_lights.prepare(conn, "select x, y, z, w from light where p = ? and q = ?;") &&
               load_signs.prepare(conn, "select x, y, z, face, text from sign where p = ? and q = ?;") &&
               get_key.prepare(conn, "select key from key where p = ? and q = ?;") &&
               set_key.prepare(conn, "insert or replace into key (p, q, key) values (?, ?, ?);") &&
               query_near.prepare(conn, "select x, y, z, w from block where p >= ? and p <= ? and q >= ? and q <= ? and w > 0;") &&
               insert_preview.prepare(conn, "insert into preview_blocks (preview_id, p, q, x, y, z, w) values (?, ?, ?, ?, ?, ?, ?);") &&
               load_preview.prepare(conn, "select x, y, z, w from preview_blocks where p = ? and q = ? and preview_id = ?;") &&
               delete_previews.prepare(conn, "delete from preview_blocks;") &&
               latest_preview.prepare(conn, "select max(preview_id) from preview_blocks;") &&
               move_preview.prepare(conn, "insert or replace into block (p, q, x, y, z, w) "
                                          "select p, q, x, y, z, w from preview_blocks where preview_id = ?;");
    }

    bool enqueue(job j)
    {
        {
            std::lock_guard lock(queue_mtx);
            if (!accepting)
            {
                return false;
            }
            queue.push_back(std::move(j));
        }
        queue_cv.notify_one();
        return true;
    }

    void apply(const job &j)
    {
        switch (j.type)
        {
        case job::kind::BLOCK:
            insert_block.bind(j.p, j.q, j.x, j.y, j.z, j.w);
            insert_block.run();
            break;
        case job::kind::BLOCKS:
            for (const auto &[p, q, x, y, z, w] : j.blocks)
            {
                insert_block.bind(p, q, x, y, z, w);
                insert_block.run();
            }
            break;
        case job::kind::LIGHT:
            insert_light.bind(j.p, j.q, j.x, j.y, j.z, j.w);
            insert_light.run();
            break;
        case job::kind::KEY:
            set_key.bind(j.p, j.q, j.key);
            set_key.run();
            break;
        case job::kind::COMMIT:
            exec(conn, "commit; begin;");
            break;
        case job::kind::FLUSH:
            exec(conn, FLUSH_SQL);
            exec(conn, "commit; begin;");
            break;
        case job::kind::EXIT:
            break;
        }
    }

    void run_worker()
    {
        for (;;)
        {
            job j;
            {
                std::unique_lock lock(queue_mtx);
                queue_cv.wait(lock, [this]
                              { return !queue.empty(); });
                j = std::move(queue.front());
                queue.pop_front();
            }
            if (j.type == job::kind::EXIT)
            {
                return;
            }
            {
                std::lock_guard lock(db_mtx);
                apply(j);
            }
            if (j.done)
            {
                j.done->set_value();
            }
        }
    }

    int latest_preview_id_locked()
    {
        latest_preview.bind();
        int id = 0;
        if (sqlite3_step(latest_preview.get()) == SQLITE_ROW)
        {
            id = sqlite3_column_int(latest_preview.get(), 0);
        }
        sqlite3_reset(latest_preview.get());
        return id;
    }

    static void load_into(statement &stmt, craft_map *map, const int a, const int b)
    {
        stmt.bind(a, b);
        while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        {
            map->set(sqlite3_column_int(stmt.get(), 0), sqlite3_column_int(stmt.get(), 1),
                     sqlite3_column_int(stmt.get(), 2), sqlite3_column_int(stmt.get(), 3));
        }
        sqlite3_reset(stmt.get());
    }

    // Stops the worker (after it drains the queue), commits, finalizes statements and closes. Idempotent.
    void teardown()
    {
        if (opened.exchange(false))
        {
            {
                std::lock_guard lock(queue_mtx);
                accepting = false;
                queue.push_back(job{job::kind::EXIT});
            }
            queue_cv.notify_one();
            worker.join();

            std::lock_guard lock(db_mtx);
            exec(conn, "commit;");
        }
        for (auto *stmt : {&insert_block, &insert_light, &insert_sign, &delete_sign, &delete_signs,
                           &load_blocks, &load_lights, &load_signs, &get_key, &set_key, &query_near,
                           &insert_preview, &load_preview, &delete_previews, &latest_preview, &move_preview})
        {
            stmt->finalize();
        }
        sqlite3_close(conn);
        conn = nullptr;
    }
};

db_manager::db_manager() : m_impl{std::make_unique<impl>()} {}

db_manager::~db_manager() { close(); }

bool db_manager::open(const char *path)
{
    auto &d = *m_impl;
    if (d.opened)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Database is already open\n");
        return false;
    }

    if (sqlite3_open(path, &d.conn) != SQLITE_OK)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open database: %s\n", sqlite3_errmsg(d.conn));
        sqlite3_close(d.conn);
        d.conn = nullptr;
        return false;
    }

    char *error = nullptr;
    if (sqlite3_exec(d.conn, SCHEMA, nullptr, nullptr, &error) != SQLITE_OK || !d.prepare_all())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize database: %s\n",
                     error ? error : sqlite3_errmsg(d.conn));
        sqlite3_free(error);
        close();
        return false;
    }

    exec(d.conn, "begin;");
    d.queue.clear();
    d.accepting = true;
    d.opened = true;
    d.worker = std::thread([&d]
                           { d.run_worker(); });
    return true;
}

void db_manager::close() { m_impl->teardown(); }

bool db_manager::is_open() const noexcept { return m_impl->opened; }

void db_manager::commit()
{
    m_impl->enqueue(job{job::kind::COMMIT});
}

void db_manager::flush()
{
    job flush_job{job::kind::FLUSH};
    flush_job.done = std::make_shared<std::promise<void>>();
    auto finished = flush_job.done->get_future();
    if (m_impl->enqueue(std::move(flush_job)))
    {
        // A fresh world must never load stale chunks, so wait for the worker.
        finished.wait();
    }
}

void db_manager::save_state(const float x, const float y, const float z, const float rx, const float ry)
{
    if (!is_open())
    {
        return;
    }
    std::lock_guard lock(m_impl->db_mtx);
    exec(m_impl->conn, "delete from state;");
    statement stmt;
    if (!stmt.prepare(m_impl->conn, "insert into state (x, y, z, rx, ry) values (?, ?, ?, ?, ?);"))
    {
        return;
    }
    sqlite3_bind_double(stmt.get(), 1, x);
    sqlite3_bind_double(stmt.get(), 2, y);
    sqlite3_bind_double(stmt.get(), 3, z);
    sqlite3_bind_double(stmt.get(), 4, rx);
    sqlite3_bind_double(stmt.get(), 5, ry);
    stmt.run();
}

std::optional<db_manager::player_state> db_manager::load_state()
{
    if (!is_open())
    {
        return std::nullopt;
    }
    std::lock_guard lock(m_impl->db_mtx);
    statement stmt;
    if (!stmt.prepare(m_impl->conn, "select x, y, z, rx, ry from state;") || sqlite3_step(stmt.get()) != SQLITE_ROW)
    {
        return std::nullopt;
    }
    return player_state{static_cast<float>(sqlite3_column_double(stmt.get(), 0)),
                        static_cast<float>(sqlite3_column_double(stmt.get(), 1)),
                        static_cast<float>(sqlite3_column_double(stmt.get(), 2)),
                        static_cast<float>(sqlite3_column_double(stmt.get(), 3)),
                        static_cast<float>(sqlite3_column_double(stmt.get(), 4))};
}

void db_manager::insert_block(const int p, const int q, const int x, const int y, const int z, const int w)
{
    m_impl->enqueue(job{job::kind::BLOCK, p, q, x, y, z, w});
}

void db_manager::insert_blocks(const std::vector<block_record> &blocks)
{
    if (!blocks.empty())
    {
        job batch{job::kind::BLOCKS};
        batch.blocks = blocks;
        m_impl->enqueue(std::move(batch));
    }
}

void db_manager::insert_light(const int p, const int q, const int x, const int y, const int z, const int w)
{
    m_impl->enqueue(job{job::kind::LIGHT, p, q, x, y, z, w});
}

void db_manager::set_key(const int p, const int q, const int key)
{
    m_impl->enqueue(job{job::kind::KEY, p, q, 0, 0, 0, 0, key});
}

int db_manager::get_key(const int p, const int q)
{
    if (!is_open())
    {
        return 0;
    }
    auto &d = *m_impl;
    std::lock_guard lock(d.db_mtx);
    d.get_key.bind(p, q);
    const int key = sqlite3_step(d.get_key.get()) == SQLITE_ROW ? sqlite3_column_int(d.get_key.get(), 0) : 0;
    sqlite3_reset(d.get_key.get());
    return key;
}

void db_manager::insert_sign(const int p, const int q, const int x, const int y, const int z, const int face, const char *text)
{
    if (!is_open())
    {
        return;
    }
    auto &d = *m_impl;
    std::lock_guard lock(d.db_mtx);
    sqlite3_bind_text(d.insert_sign.bind(p, q, x, y, z, face), 7, text ? text : "", -1, SQLITE_TRANSIENT);
    d.insert_sign.run();
}

void db_manager::delete_sign(const int x, const int y, const int z, const int face)
{
    if (!is_open())
    {
        return;
    }
    auto &d = *m_impl;
    std::lock_guard lock(d.db_mtx);
    d.delete_sign.bind(x, y, z, face);
    d.delete_sign.run();
}

void db_manager::delete_signs(const int x, const int y, const int z)
{
    if (!is_open())
    {
        return;
    }
    auto &d = *m_impl;
    std::lock_guard lock(d.db_mtx);
    d.delete_signs.bind(x, y, z);
    d.delete_signs.run();
}

void db_manager::delete_all_signs()
{
    if (!is_open())
    {
        return;
    }
    std::lock_guard lock(m_impl->db_mtx);
    exec(m_impl->conn, "delete from sign;");
}

void db_manager::load_blocks(craft_map *map, const int p, const int q)
{
    if (!is_open())
    {
        return;
    }
    std::lock_guard lock(m_impl->db_mtx);
    impl::load_into(m_impl->load_blocks, map, p, q);
}

void db_manager::load_lights(craft_map *map, const int p, const int q)
{
    if (!is_open())
    {
        return;
    }
    std::lock_guard lock(m_impl->db_mtx);
    impl::load_into(m_impl->load_lights, map, p, q);
}

void db_manager::load_signs(db::sign_list *list, const int p, const int q)
{
    if (!is_open())
    {
        return;
    }
    auto &d = *m_impl;
    std::lock_guard lock(d.db_mtx);
    d.load_signs.bind(p, q);
    auto *stmt = d.load_signs.get();
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        db::sign_list_add(list, sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1),
                          sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3), text ? text : "");
    }
    sqlite3_reset(stmt);
}

std::vector<std::tuple<int, int, int, int>> db_manager::query_blocks_near_chunks(const int center_p, const int center_q, const int radius)
{
    std::vector<std::tuple<int, int, int, int>> blocks;
    if (!is_open())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Database not open, cannot query blocks\n");
        return blocks;
    }
    auto &d = *m_impl;
    std::lock_guard lock(d.db_mtx);
    d.query_near.bind(center_p - radius, center_p + radius, center_q - radius, center_q + radius);
    auto *stmt = d.query_near.get();
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        blocks.emplace_back(sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1),
                            sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3));
    }
    sqlite3_reset(stmt);
    return blocks;
}

void db_manager::insert_preview_blocks(const int preview_id, const std::vector<block_record> &blocks)
{
    if (!is_open() || blocks.empty())
    {
        return;
    }
    auto &d = *m_impl;
    // Runs inside the worker's open transaction; the periodic commit persists it.
    std::lock_guard lock(d.db_mtx);
    for (const auto &[p, q, x, y, z, w] : blocks)
    {
        d.insert_preview.bind(preview_id, p, q, x, y, z, w);
        d.insert_preview.run();
    }
}

void db_manager::load_preview_blocks(craft_map *map, const int p, const int q, const int preview_id)
{
    if (!is_open())
    {
        return;
    }
    auto &d = *m_impl;
    std::lock_guard lock(d.db_mtx);
    d.load_preview.bind(p, q, preview_id);
    auto *stmt = d.load_preview.get();
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        map->set(sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1),
                 sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3));
    }
    sqlite3_reset(stmt);
}

int db_manager::get_latest_preview_id()
{
    if (!is_open())
    {
        return 0;
    }
    std::lock_guard lock(m_impl->db_mtx);
    return m_impl->latest_preview_id_locked();
}

void db_manager::commit_latest_preview_to_main()
{
    if (!is_open())
    {
        return;
    }
    auto &d = *m_impl;
    std::lock_guard lock(d.db_mtx);
    const int latest = d.latest_preview_id_locked();
    if (latest <= 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No preview blocks to commit\n");
        return;
    }
    d.move_preview.bind(latest);
    d.move_preview.run();
    d.delete_previews.bind();
    d.delete_previews.run();
}

void db_manager::flush_all_preview_blocks()
{
    if (!is_open())
    {
        return;
    }
    auto &d = *m_impl;
    {
        std::lock_guard lock(d.db_mtx);
        d.delete_previews.bind();
        d.delete_previews.run();
    }
    SDL_Log("Flushed all preview blocks\n");
}

std::vector<db_manager::block_record> db_manager::get_all_preview_blocks(const int preview_id)
{
    std::vector<block_record> blocks;
    if (!is_open())
    {
        return blocks;
    }
    std::lock_guard lock(m_impl->db_mtx);
    statement stmt;
    if (!stmt.prepare(m_impl->conn, "select p, q, x, y, z, w from preview_blocks where preview_id = ?;"))
    {
        return blocks;
    }
    sqlite3_bind_int(stmt.get(), 1, preview_id);
    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
    {
        blocks.emplace_back(sqlite3_column_int(stmt.get(), 0), sqlite3_column_int(stmt.get(), 1),
                            sqlite3_column_int(stmt.get(), 2), sqlite3_column_int(stmt.get(), 3),
                            sqlite3_column_int(stmt.get(), 4), sqlite3_column_int(stmt.get(), 5));
    }
    return blocks;
}

int db_manager::save_heightmap(const std::string &name, const int side, const int max_height,
                               const std::vector<std::uint8_t> &data)
{
    if (!is_open())
    {
        return 0;
    }
    int id = 0;
    {
        std::lock_guard lock(m_impl->db_mtx);
        statement stmt;
        if (stmt.prepare(m_impl->conn, "insert into heightmap (name, side, max_height, data) values (?, ?, ?, ?);"))
        {
            sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt.get(), 2, side);
            sqlite3_bind_int(stmt.get(), 3, max_height);
            sqlite3_bind_blob(stmt.get(), 4, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
            if (sqlite3_step(stmt.get()) == SQLITE_DONE)
            {
                id = static_cast<int>(sqlite3_last_insert_rowid(m_impl->conn));
            }
        }
    }
    commit();
    return id;
}

std::vector<db_manager::heightmap_record> db_manager::load_heightmaps()
{
    std::vector<heightmap_record> records;
    if (!is_open())
    {
        return records;
    }
    std::lock_guard lock(m_impl->db_mtx);
    statement stmt;
    if (!stmt.prepare(m_impl->conn, "select id, name, side, max_height, data from heightmap order by id;"))
    {
        return records;
    }
    auto *s = stmt.get();
    while (sqlite3_step(s) == SQLITE_ROW)
    {
        heightmap_record record;
        record.id = sqlite3_column_int(s, 0);
        const auto *name = reinterpret_cast<const char *>(sqlite3_column_text(s, 1));
        record.name = name ? name : "";
        record.side = sqlite3_column_int(s, 2);
        record.max_height = sqlite3_column_int(s, 3);
        const auto *bytes = static_cast<const std::uint8_t *>(sqlite3_column_blob(s, 4));
        const auto size = static_cast<std::size_t>(sqlite3_column_bytes(s, 4));
        if (bytes && record.side > 0 && size == static_cast<std::size_t>(record.side) * static_cast<std::size_t>(record.side))
        {
            record.data.assign(bytes, bytes + size);
            records.push_back(std::move(record));
        }
    }
    return records;
}

void db_manager::delete_heightmap(const int id)
{
    if (!is_open() || id <= 0)
    {
        return;
    }
    {
        std::lock_guard lock(m_impl->db_mtx);
        statement stmt;
        if (stmt.prepare(m_impl->conn, "delete from heightmap where id = ?;"))
        {
            sqlite3_bind_int(stmt.get(), 1, id);
            stmt.run();
        }
    }
    commit();
}
