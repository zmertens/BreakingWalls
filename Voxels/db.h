#ifndef DB_H
#define DB_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

class craft_map;

class db final
{
public:
	static constexpr std::size_t MAX_SIGN_LENGTH = 16;

	struct sign
	{
		int x;
		int y;
		int z;
		int face;
		char text[MAX_SIGN_LENGTH];
	};

	struct sign_list
	{
		std::size_t capacity{};
		std::size_t size{};
		sign *data{};
	};

	static void sign_list_alloc(sign_list *list, std::size_t capacity);
	static void sign_list_free(sign_list *list);
	static void sign_list_add(sign_list *list, int x, int y, int z, int face, std::string_view text);
	static int sign_list_remove(sign_list *list, int x, int y, int z, int face);
	static int sign_list_remove_all(sign_list *list, int x, int y, int z);

	struct heightmap_record
	{
		int id{};
		std::string name;
		int side{};
		int max_height{};
		std::vector<std::uint8_t> data; // one byte of elevation per cell
	};

	static int save_heightmap(const std::string &name, int side, int max_height, const std::vector<std::uint8_t> &data);
	static std::vector<heightmap_record> load_heightmaps();
	static void delete_heightmap(int id);

	static void enable();
	static void disable();
	static bool is_enabled();
	static int init(const char *path);
	static void close();
	static void commit();
	static void flush();
	static void save_state(float x, float y, float z, float rx, float ry);
	static int load_state(float *x, float *y, float *z, float *rx, float *ry);
	static void insert_block(int p, int q, int x, int y, int z, int w);
	static void insert_blocks(const std::vector<std::tuple<int, int, int, int, int, int>> &blocks);
	static void insert_light(int p, int q, int x, int y, int z, int w);
	static void insert_sign(int p, int q, int x, int y, int z, int face, const char *text);
	static void delete_sign(int x, int y, int z, int face);
	static void delete_signs(int x, int y, int z);
	static void delete_all_signs();
	static void load_blocks(craft_map *map, int p, int q);
	static void load_lights(craft_map *map, int p, int q);
	static void load_signs(sign_list *list, int p, int q);
	static int get_key(int p, int q);
	static void set_key(int p, int q, int key);
	static std::vector<std::tuple<int, int, int, int>> query_blocks_near_chunks(int center_p, int center_q, int radius);
	static void worker_start(const char *path);
	static void worker_stop();

	static void insert_preview_blocks(int preview_id, const std::vector<std::tuple<int, int, int, int, int, int>> &blocks);
	static void load_preview_blocks(craft_map *map, int p, int q, int preview_id);
	static int get_latest_preview_id();
	static void commit_latest_preview_to_main();
	static void flush_all_preview_blocks();
	static std::vector<std::tuple<int, int, int, int, int, int>> get_all_preview_blocks(int preview_id);

	db() = delete;

private:
	static void sign_list_grow(sign_list *list);
	static void sign_list_add_impl(sign_list *list, const sign *entry);
	static int sign_list_remove_impl(sign_list *list, int x, int y, int z, int face);
	static int sign_list_remove_all_impl(sign_list *list, int x, int y, int z);
	static void commit_impl();
	static void flush_impl();
	static void insert_block_impl(int p, int q, int x, int y, int z, int w);
	static void insert_blocks_impl(int *blocks);
	static void insert_light_impl(int p, int q, int x, int y, int z, int w);
	static void set_key_impl(int p, int q, int key);
	static int worker_run(void *arg);
};

#endif // DB_H
