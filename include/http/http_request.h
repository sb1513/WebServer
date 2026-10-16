#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <cstddef>
#include <string>
#include <unordered_map>

enum class ParseResult {
    INCOMPLETE,
    COMPLETE,
    BAD_REQUEST,
    PAYLOAD_TOO_LARGE,
    UNSUPPORTED
};

enum class ParseState {
    REQUEST_LINE,
    HEADERS,
    BODY,
    FINISHED,
    ERROR
};

class HttpRequest {
public:
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    void reset();
};

class HttpRequestParser {
public:
    HttpRequestParser();//构造函数

    ParseResult append(const char* data, size_t length);
    ParseResult parse();

    const HttpRequest& request() const;
    const std::string& errorMessage() const;

private:
    ParseResult parseRequestLine(const std::string& line);
    ParseResult parseHeaderLine(const std::string& line);
    ParseResult finishHeaders();
    ParseResult fail(ParseResult result, const std::string& message);

    HttpRequest request_;
    ParseState state_;
    std::string buffer_;
    size_t cursor_;
    size_t contentLength_;
    size_t maxBodySize_;
    std::string errorMessage_;
    bool hasContentLength_;
};

#endif
