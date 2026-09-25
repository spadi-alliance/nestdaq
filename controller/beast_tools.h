#pragma once

/**
 * @file beast_tools.h
 * @brief Boost.Beast helpers for static file serving and HTTP responses.
 */

#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace beast = boost::beast;                 // from <boost/beast.hpp>
namespace http = beast::http;                   // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;         // from <boost/beast/websocket.hpp>
namespace net = boost::asio;                    // from <boost/asio.hpp>
using tcp = net::ip::tcp;                       // from <boost/asio/ip/tcp.hpp>

/** Report a Boost.Beast failure to stderr. */
void fail(beast::error_code ec, char const* what);

/**
 * @brief Produce an HTTP response for a static-file request.
 *
 * The response type depends on the request, so the caller supplies a generic
 * lambda that receives the concrete response object.
 */
template<class Body, class Allocator, class Send>
void handleRequest(beast::string_view doc_root, http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send);

// Return a reasonable mime type based on the extension of a file.
beast::string_view mimeType(beast::string_view path);

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string pathCat(beast::string_view base, beast::string_view path);

template<class Body, class Allocator, class Send>
// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved,cppcoreguidelines-missing-std-forward)
void handleRequest(beast::string_view doc_root, http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send)
{
    // Returns a bad request response
    auto const kBadRequest =
        [&req](beast::string_view why)
    {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    auto const kNotFound =
        [&req](beast::string_view target)
    {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const kServerError =
        [&req](beast::string_view what)
    {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

    // Make sure we can handle the method
    if( req.method() != http::verb::get &&
            req.method() != http::verb::head) {
        return send(kBadRequest("Unknown HTTP-method"));
    }

    // Request path must be absolute and not contain "..".
    if( req.target().empty() ||
            req.target()[0] != '/' ||
            req.target().find("..") != beast::string_view::npos) {
        return send(kBadRequest("Illegal request-target"));
    }

    // Build the path to the requested file
    std::string path = pathCat(doc_root, req.target());
    if(req.target().back() == '/') {
        path.append("index.html");
    }

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.data(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if(ec == beast::errc::no_such_file_or_directory) {
        return send(kNotFound(req.target()));
    }

    // Handle an unknown error
    if(ec) {
        return send(kServerError(ec.message()));
    }

    // Cache the size since we need it after the move
    auto const kSize = body.size();

    // Respond to HEAD request
    if(req.method() == http::verb::head) {
        http::response<http::empty_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mimeType(path));
        res.content_length(kSize);
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    }

    // Respond to GET request
    http::response<http::file_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mimeType(path));
    res.content_length(kSize);
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
}
