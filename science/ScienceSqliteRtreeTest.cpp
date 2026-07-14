#include <iostream>

#include <sqlite3.h>

int main()
{
    sqlite3* database = nullptr;
    if (sqlite3_open(":memory:", &database) != SQLITE_OK)
    {
        std::cerr << "could not open in-memory SQLite database\n";
        if (database) sqlite3_close(database);
        return 1;
    }

    char* error = nullptr;
    const int result = sqlite3_exec(
        database,
        "CREATE VIRTUAL TABLE tile_rtree USING "
        "rtree(id,min_lon,max_lon,min_lat,max_lat)",
        nullptr, nullptr, &error);
    if (result != SQLITE_OK)
    {
        std::cerr << "embedded SQLite cannot open the ScienceEarth index: "
                  << (error ? error : sqlite3_errmsg(database)) << "\n";
        sqlite3_free(error);
        sqlite3_close(database);
        return 1;
    }

    sqlite3_close(database);
    return 0;
}
