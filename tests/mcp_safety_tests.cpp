#include <ai/McpServer.h>
#include <3rdparty/libhv/all/client/requests.h>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

static bool isInvalidParams(const picojson::value& value)
{
    return value.is<picojson::object>() && value.contains("code") &&
           value.get("code").get<double>() == osgVerse::InvalidParams;
}

int main()
{
    osg::ref_ptr<osgVerse::McpServer> pagination = new osgVerse::McpServer;
    picojson::array items;
    items.push_back(picojson::value("zero"));
    items.push_back(picojson::value("one"));
    CHECK(isInvalidParams(pagination->paginateResults(items, "cursor_bad", 1, "items")));
    CHECK(isInvalidParams(pagination->paginateResults(items, "cursor_-1", 1, "items")));
    CHECK(isInvalidParams(pagination->paginateResults(
        items, "cursor_999999999999999999999999", 1, "items")));
    CHECK(isInvalidParams(pagination->paginateResults(items, "cursor_3", 1, "items")));
    CHECK(pagination->paginateResults(items, "cursor_1", 1, "items")
              .get("items").get<picojson::array>()[0].get<std::string>() == "one");

    std::promise<void> continued;
    std::future<void> continuedFuture = continued.get_future();
    {
        osgVerse::ThreadPool pool(1);
        pool.enqueue([] { throw std::runtime_error("expected worker test exception"); });
        pool.enqueue([&continued] { continued.set_value(); });
        CHECK(continuedFuture.wait_for(std::chrono::seconds(2)) ==
              std::future_status::ready);
    }

    osg::ref_ptr<osgVerse::McpServer> lifecycle = new osgVerse::McpServer;
    lifecycle->start("127.0.0.1", 19876);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    requests::Request request(new HttpRequest);
    request->method = HTTP_GET;
    request->url = "http://127.0.0.1:19876/sse";
    request->timeout = 1;
    requests::request(request);
    std::this_thread::sleep_for(std::chrono::seconds(6));
    lifecycle->stop();
    std::cout << "[OK] MCP cursor, worker, and SSE lifecycle safety\n";
    return 0;
}
