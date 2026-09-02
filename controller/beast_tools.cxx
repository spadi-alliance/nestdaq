/** @file
 *  @brief Implements Boost.Beast helper functions for HTTP responses.
 */

#include <iostream>

#include <fairlogger/Logger.h>

#include "controller/beast_tools.h"

void fail(beast::error_code ec, char const* what)
{
    LOG(warn) << "boost::beast fail(): what = " << what << ": ec.message() = " << ec.message();
}

beast::string_view mimeType(beast::string_view path)
{
    auto const kExt = [&path]
    {
        auto const kPos = path.rfind(".");
        if(kPos == beast::string_view::npos) {
            return beast::string_view{};
        }
        return path.substr(kPos);
    }();

    if(beast::iequals(kExt, ".htm"))  return "text/html";
    if(beast::iequals(kExt, ".html")) return "text/html";
    if(beast::iequals(kExt, ".php"))  return "text/html";
    if(beast::iequals(kExt, ".css"))  return "text/css";
    if(beast::iequals(kExt, ".txt"))  return "text/plain";
    if(beast::iequals(kExt, ".js"))   return "application/javascript";
    if(beast::iequals(kExt, ".json")) return "application/json";
    if(beast::iequals(kExt, ".xml"))  return "application/xml";
    if(beast::iequals(kExt, ".swf"))  return "application/x-shockwave-flash";
    if(beast::iequals(kExt, ".flv"))  return "video/x-flv";
    if(beast::iequals(kExt, ".png"))  return "image/png";
    if(beast::iequals(kExt, ".jpe"))  return "image/jpeg";
    if(beast::iequals(kExt, ".jpeg")) return "image/jpeg";
    if(beast::iequals(kExt, ".jpg"))  return "image/jpeg";
    if(beast::iequals(kExt, ".gif"))  return "image/gif";
    if(beast::iequals(kExt, ".bmp"))  return "image/bmp";
    if(beast::iequals(kExt, ".ico"))  return "image/vnd.microsoft.icon";
    if(beast::iequals(kExt, ".tiff")) return "image/tiff";
    if(beast::iequals(kExt, ".tif"))  return "image/tiff";
    if(beast::iequals(kExt, ".svg"))  return "image/svg+xml";
    if(beast::iequals(kExt, ".svgz")) return "image/svg+xml";
    return "application/text";

}

std::string pathCat(beast::string_view base, beast::string_view path)
{
    if(base.empty()) {
        return std::string(path);
    }
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr kPathSeparator = '\\';
    if(result.back() == kPathSeparator) {
        result.resize(result.size() - 1);
    }
    result.append(path.data(), path.size());
    for(auto& c : result) {
        if(c == '/') {
            c = kPathSeparator;
        }
    }
#else
    char constexpr kPathSeparator = '/';
    if(result.back() == kPathSeparator) {
        result.resize(result.size() - 1);
    }
    result.append(path.data(), path.size());
#endif
    return result;
}
