#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include "db.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

class craft_map;

/// Owns the SQLite connection and a background writer thread.
///
/// Writes to blocks, lights, keys and commits are queued and applied in order by the worker;
/// everything else runs on the caller's thread. All connection access is serialized by one
/// mutex. The owner decides the lifetime: call close() (or destroy it) before SDL shuts down.
/// Every method is a no-op returning a neutral value while the manager is not open.
class db_manager final
{
public:
	using block_record = std::tuple<int, int, int, int, int, int>; // p, q, x, y, z, w

	struct player_state
	{
		float x{}, y{}, z{}, rx{}, ry{};
	};

	struct heightmap_record
	{
		int id{};
		std::string name;
		int side{};
		int max_height{};
		std::vector<std::uint8_t> data; // one byte of elevation per cell
	};

	db_manager();
	~db_manager();
	db_manager(const db_manager &) = delete;
	db_manager &operator=(const db_manager &) = delete;

	/// Opens the connection, creates the schema and starts the worker. Returns false on failure.
	[[nodiscard]] bool open(const char *path);
	/// Drains queued writes, commits and closes. Idempotent.
	void close();
	[[nodiscard]] bool is_open() const noexcept;

	void commit();
	/// Clears world tables; blocks until the worker has applied it.
	void flush();

	void save_state(float x, float y, float z, float rx, float ry);
	[[nodiscard]] std::optional<player_state> load_state();

	void insert_block(int p, int q, int x, int y, int z, int w);
	void insert_blocks(const std::vector<block_record> &blocks);
	void insert_light(int p, int q, int x, int y, int z, int w);
	void set_key(int p, int q, int key);
	[[nodiscard]] int get_key(int p, int q);

	void insert_sign(int p, int q, int x, int y, int z, int face, const char *text);
	void delete_sign(int x, int y, int z, int face);
	void delete_signs(int x, int y, int z);
	void delete_all_signs();

	void load_blocks(craft_map *map, int p, int q);
	void load_lights(craft_map *map, int p, int q);
	void load_signs(db::sign_list *list, int p, int q);
	[[nodiscard]] std::vector<std::tuple<int, int, int, int>> query_blocks_near_chunks(int center_p, int center_q, int radius);

	void insert_preview_blocks(int preview_id, const std::vector<block_record> &blocks);
	void load_preview_blocks(craft_map *map, int p, int q, int preview_id);
	[[nodiscard]] int get_latest_preview_id();
	void commit_latest_preview_to_main();
	void flush_all_preview_blocks();
	[[nodiscard]] std::vector<block_record> get_all_preview_blocks(int preview_id);

	int save_heightmap(const std::string &name, int side, int max_height, const std::vector<std::uint8_t> &data);
	[[nodiscard]] std::vector<heightmap_record> load_heightmaps();
	void delete_heightmap(int id);

private:
	struct impl;
	std::unique_ptr<impl> m_impl;
};

#endif // DB_MANAGER_H
