#ifndef DB_H
#define DB_H

#include <cstddef>
#include <string_view>

/// Sign value types and the in-memory sign list used by chunks.
/// Persistence lives in db_manager.
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

	db() = delete;

private:
	static void sign_list_grow(sign_list *list);
	static int sign_list_remove_if(sign_list *list, int x, int y, int z, const int *face);
};

#endif // DB_H
