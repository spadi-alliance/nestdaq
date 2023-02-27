#include <iostream>

#include <fairmq/FairMQLogger.h>

#include "controller/beast_tools.h"


//_____________________________________________________________________________
void fail(beast::error_code ec, char const* what)
{
    LOG(warn) << "boost::beast fail(): what = " << what << ": ec.message() = " << ec.message();
}

//_____________________________________________________________________________
beast::string_view mime_type(beast::string_view path)
{
  auto const ext = [&path]
  {
      auto const pos = path.rfind(".");
      if(pos == beast::string_view::npos)
          return beast::string_view{};
      return path.substr(pos);
  }();

  if(beast::iequals(ext, ".htm"))  return "text/html";
  if(beast::iequals(ext, ".html")) return "text/html";
  if(beast::iequals(ext, ".php"))  return "text/html";
  if(beast::iequals(ext, ".css"))  return "text/css";
  if(beast::iequals(ext, ".txt"))  return "text/plain";
  if(beast::iequals(ext, ".js"))   return "application/javascript";
  if(beast::iequals(ext, ".json")) return "application/json";
  if(beast::iequals(ext, ".xml"))  return "application/xml";
  if(beast::iequals(ext, ".swf"))  return "application/x-shockwave-flash";
  if(beast::iequals(ext, ".flv"))  return "video/x-flv";
  if(beast::iequals(ext, ".png"))  return "image/png";
  if(beast::iequals(ext, ".jpe"))  return "image/jpeg";
  if(beast::iequals(ext, ".jpeg")) return "image/jpeg";
  if(beast::iequals(ext, ".jpg"))  return "image/jpeg";
  if(beast::iequals(ext, ".gif"))  return "image/gif";
  if(beast::iequals(ext, ".bmp"))  return "image/bmp";
  if(beast::iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
  if(beast::iequals(ext, ".tiff")) return "image/tiff";
  if(beast::iequals(ext, ".tif"))  return "image/tiff";
  if(beast::iequals(ext, ".svg"))  return "image/svg+xml";
  if(beast::iequals(ext, ".svgz")) return "image/svg+xml";
  return "application/text";

}

//_____________________________________________________________________________
std::string path_cat(beast::string_view base, beast::string_view path)
{
    if(base.empty())
        return std::string(path);
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr path_separator = '\\';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    for(auto& c : result)
        if(c == '/')
            c = path_separator;
#else
    char constexpr path_separator = '/';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
#endif
    return result;
}