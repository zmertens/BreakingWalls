#ifndef SIGN_DATA_H
#define SIGN_DATA_H

#include <cstddef>
#include <string_view>
#include <vector>

/// Sign value types and helpers operating on a chunk's sign storage.
/// Persistence lives in db_manager.
class sign_data final
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

	// Mutating operations take the owning vector (a std::span cannot grow or shrink storage);
	// read-only consumers should accept std::span<const sign>.
	static void sign_list_add(std::vector<sign> &signs, int x, int y, int z, int face, std::string_view text);
	static int sign_list_remove(std::vector<sign> &signs, int x, int y, int z, int face);
	static int sign_list_remove_all(std::vector<sign> &signs, int x, int y, int z);

	sign_data() = delete;

private:
	static int sign_list_remove_if(std::vector<sign> &signs, int x, int y, int z, const int *face);
};

#endif // SIGN_DATA_H
