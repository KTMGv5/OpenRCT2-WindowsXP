/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifndef DISABLE_HTTP

    #include <functional>
    #include <future>
    #include <map>
    #include <memory>
    #include <string>
    #include <thread>

namespace OpenRCT2::Http
{
    enum class Status
    {
        invalid = 0,
        error = 1,
        ok = 200,
        notFound = 404
    };

    enum class Method
    {
        get,
        post,
        put
    };

    struct Response
    {
        Status status{};
        std::string content_type;
        std::string body;
        std::map<std::string, std::string> header = {};
        std::string error;
    };

    struct Request
    {
        std::string url;
        std::map<std::string, std::string> header;
        Method method = Method::get;
        std::string body;
        bool forceIPv4{};
    };

    Response Do(const Request& req);

    [[nodiscard]] inline std::future<void> DoAsync(const Request& req, std::function<void(Response& res)> fn)
    {
        auto ptask = std::make_shared<std::packaged_task<void()>>([=]() {
            Response res{};
            try
            {
                res = Do(req);
            }
            catch (const std::exception& e)
            {
                res.status = Status::error;
                res.error = e.what();
            }
            try
            {
                fn(res);
            }
            catch (...)
            {
            }
        });

        std::future<void> fut = ptask->get_future();
        std::thread([ptask]() { (*ptask)(); }).detach();
        return fut;
    }
} // namespace OpenRCT2::Http

#endif // DISABLE_HTTP
