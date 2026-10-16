#include "http/http_request.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace {

const size_t MAX_HEADER_SIZE = 16 * 1024;
const size_t MAX_BODY_SIZE = 1024 * 1024;

std::string trim(const std::string& value) {
    size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }

    size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::string toLower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );
    return value;
}

bool isTokenChar(unsigned char c) {
    if (std::isalnum(c)) {
        return true;
    }

    const std::string symbols = "!#$%&'*+-.^_`|~";
    return symbols.find(static_cast<char>(c)) != std::string::npos;
}

bool isValidMethod(const std::string& method) {
    if (method.empty()) {
        return false;
    }

    for (unsigned char c : method) {
        if (!isTokenChar(c)) {
            return false;
        }
    }

    return true;
}

bool isValidTarget(const std::string& target) {
    if (target.empty() || target[0] != '/') {
        return false;
    }

    for (unsigned char c : target) {
        if (c <= 32 || c == 127 || c == '#') {
            return false;
        }
    }

    return true;
}

bool isValidHeaderValue(const std::string& value) {
    for (unsigned char c : value) {
        if (c == '\r' || c == '\n' || c == 127) {
            return false;
        }
        if (c < 32 && c != '\t') {
            return false;
        }
    }

    return true;
}

}  // namespace

void HttpRequest::reset() {
    method.clear();
    path.clear();
    version.clear();
    headers.clear();
    body.clear();
}

HttpRequestParser::HttpRequestParser()
    : state_(ParseState::REQUEST_LINE),
      cursor_(0),
      contentLength_(0),
      maxBodySize_(MAX_BODY_SIZE),
      hasContentLength_(false) {
}

ParseResult HttpRequestParser::fail(
    ParseResult result,
    const std::string& message
) {
    state_ = ParseState::ERROR;
    errorMessage_ = message;
    return result;
}

ParseResult HttpRequestParser::append(
    const char* data,
    size_t length
) {
    if (state_ == ParseState::ERROR) {
        return ParseResult::BAD_REQUEST;
    }

    if (state_ == ParseState::FINISHED) {
        return ParseResult::COMPLETE;
    }

    if (length > MAX_HEADER_SIZE + maxBodySize_ ||
        buffer_.size() > MAX_HEADER_SIZE + maxBodySize_ - length) {
        return fail(
            ParseResult::PAYLOAD_TOO_LARGE,
            "Request exceeds configured size limit"
        );
    }

    buffer_.append(data, length);
    return parse();
}

ParseResult HttpRequestParser::parseRequestLine(
    const std::string& line
) {
    std::istringstream stream(line);
    std::string method;
    std::string target;
    std::string version;
    std::string extra;

    if (!(stream >> method >> target >> version) ||
        (stream >> extra)) {
        return fail(
            ParseResult::BAD_REQUEST,
            "Malformed request line"
        );
    }

    if (!isValidMethod(method) || !isValidTarget(target)) {
        return fail(
            ParseResult::BAD_REQUEST,
            "Invalid method or request target"
        );
    }

    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        return fail(
            ParseResult::UNSUPPORTED,
            "Unsupported HTTP version"
        );
    }

    if (method != "GET" && method != "POST" &&
        method != "HEAD") {
        return fail(
            ParseResult::UNSUPPORTED,
            "Unsupported HTTP method"
        );
    }

    request_.method = method;
    request_.path = target;
    request_.version = version;

    size_t queryPos = request_.path.find('?');
    if (queryPos != std::string::npos) {
        request_.path = request_.path.substr(0, queryPos);
    }

    return ParseResult::INCOMPLETE;
}

ParseResult HttpRequestParser::parseHeaderLine(
    const std::string& line
) {
    size_t colon = line.find(':');

    if (colon == std::string::npos || colon == 0) {
        return fail(
            ParseResult::BAD_REQUEST,
            "Malformed header"
        );
    }

    std::string name = line.substr(0, colon);
    std::string value = trim(line.substr(colon + 1));

    for (unsigned char c : name) {
        if (!isTokenChar(c)) {
            return fail(
                ParseResult::BAD_REQUEST,
                "Invalid header name"
            );
        }
    }

    if (!isValidHeaderValue(value)) {
        return fail(
            ParseResult::BAD_REQUEST,
            "Invalid header value"
        );
    }

    name = toLower(name);

    auto it = request_.headers.find(name);

    if (name == "content-length") {
        if (value.empty()) {
            return fail(
                ParseResult::BAD_REQUEST,
                "Empty Content-Length"
            );
        }

        size_t length = 0;

        for (unsigned char c : value) {
            if (!std::isdigit(c)) {
                return fail(
                    ParseResult::BAD_REQUEST,
                    "Invalid Content-Length"
                );
            }

            size_t digit = c - '0';

            if (length >
                (std::numeric_limits<size_t>::max() - digit) / 10) {
                return fail(
                    ParseResult::BAD_REQUEST,
                    "Content-Length overflow"
                );
            }

            length = length * 10 + digit;
        }

        if (hasContentLength_ && length != contentLength_) {
            return fail(
                ParseResult::BAD_REQUEST,
                "Conflicting Content-Length headers"
            );
        }

        hasContentLength_ = true;
        contentLength_ = length;

        if (contentLength_ > maxBodySize_) {
            return fail(
                ParseResult::PAYLOAD_TOO_LARGE,
                "Request body too large"
            );
        }
    }

    if (name == "transfer-encoding") {
        return fail(
            ParseResult::UNSUPPORTED,
            "Transfer-Encoding is not supported"
        );
    }

    if (it != request_.headers.end()) {
        if (name == "content-length") {
            return ParseResult::INCOMPLETE;
        }

        it->second += ", " + value;
    } else {
        request_.headers.emplace(name, value);
    }

    return ParseResult::INCOMPLETE;
}

ParseResult HttpRequestParser::finishHeaders() {
    if (request_.version == "HTTP/1.1" &&
        request_.headers.find("host") == request_.headers.end()) {
        return fail(
            ParseResult::BAD_REQUEST,
            "Missing Host header"
        );
    }

    if (contentLength_ == 0) {
        state_ = ParseState::FINISHED;
        return ParseResult::COMPLETE;
    }

    state_ = ParseState::BODY;
    return ParseResult::INCOMPLETE;
}

ParseResult HttpRequestParser::parse() {
    if (state_ == ParseState::ERROR) {
        return ParseResult::BAD_REQUEST;
    }

    while (true) {
        if (state_ == ParseState::REQUEST_LINE ||
            state_ == ParseState::HEADERS) {
            size_t lineEnd = buffer_.find("\r\n", cursor_);

            if (lineEnd == std::string::npos) {
                if (buffer_.size() > MAX_HEADER_SIZE) {
                    return fail(
                        ParseResult::PAYLOAD_TOO_LARGE,
                        "Request headers too large"
                    );
                }

                return ParseResult::INCOMPLETE;
            }

            if (lineEnd + 2 > MAX_HEADER_SIZE) {
                return fail(
                    ParseResult::PAYLOAD_TOO_LARGE,
                    "Request headers too large"
                );
            }

            std::string line =
                buffer_.substr(cursor_, lineEnd - cursor_);

            cursor_ = lineEnd + 2;

            if (state_ == ParseState::REQUEST_LINE) {
                ParseResult result = parseRequestLine(line);

                if (result != ParseResult::INCOMPLETE) {
                    return result;
                }

                state_ = ParseState::HEADERS;
            } else {
                if (line.empty()) {
                    ParseResult result = finishHeaders();

                    if (result != ParseResult::INCOMPLETE) {
                        return result;
                    }
                } else {
                    ParseResult result = parseHeaderLine(line);

                    if (result != ParseResult::INCOMPLETE) {
                        return result;
                    }
                }
            }

            continue;
        }

        if (state_ == ParseState::BODY) {
            size_t available = buffer_.size() - cursor_;

            if (available < contentLength_) {
                return ParseResult::INCOMPLETE;
            }

            request_.body.assign(
                buffer_.data() + cursor_,
                contentLength_
            );

            cursor_ += contentLength_;
            state_ = ParseState::FINISHED;

            return ParseResult::COMPLETE;
        }

        if (state_ == ParseState::FINISHED) {
            return ParseResult::COMPLETE;
        }

        return ParseResult::BAD_REQUEST;
    }
}

const HttpRequest& HttpRequestParser::request() const {
    return request_;
}

const std::string& HttpRequestParser::errorMessage() const {
    return errorMessage_;
}
