#include "HttpRedirect.h"

#include <ctype.h>

namespace deskhog {

namespace {

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return text;
}

/** Length of "http://" or "https://" at the start, or 0. */
size_t schemeLength(const std::string& url) {
    const std::string start = lower(url.substr(0, 8));
    if (start.compare(0, 8, "https://") == 0) return 8;
    if (start.compare(0, 7, "http://") == 0) return 7;
    return 0;
}

/** Where the host (and port) ends: the first '/', '?' or '#' after the scheme. */
size_t originEnd(const std::string& url, size_t scheme) {
    const size_t end = url.find_first_of("/?#", scheme);
    return end == std::string::npos ? url.size() : end;
}

}  // namespace

std::string urlOrigin(const std::string& url) {
    const size_t scheme = schemeLength(url);
    if (scheme == 0) return "";
    return lower(url.substr(0, originEnd(url, scheme)));
}

bool isCredentialHeader(const std::string& name) {
    const std::string header = lower(name);
    return header == "authorization" || header == "cookie" || header == "proxy-authorization";
}

bool followRedirect(int status, std::string& method, std::string& url, const std::string& location) {
    const bool reads = method == "GET" || method == "HEAD";
    const bool seeOther = status == 302 || status == 303;
    const bool sameMethod = status == 301 || status == 307 || status == 308;
    if (!seeOther && !(sameMethod && reads)) return false;

    const size_t scheme = schemeLength(url);
    if (scheme == 0 || location.empty()) return false;
    const size_t host = originEnd(url, scheme);

    std::string next;
    if (schemeLength(location) > 0) {
        next = location;
    } else if (location.find("://") != std::string::npos && location.find("://") < location.find_first_of("/?#")) {
        return false;  // some other scheme: not for this device
    } else if (location.compare(0, 2, "//") == 0) {
        next = url.substr(0, scheme - 2) + location;
    } else if (location[0] == '/') {
        next = url.substr(0, host) + location;
    } else {
        // Next to what answered: everything up to its last '/', leaving out
        // the query. Without a path that is the root.
        const size_t pathEnd = url.find_first_of("?#", host);
        const std::string path = url.substr(host, pathEnd == std::string::npos ? std::string::npos : pathEnd - host);
        const size_t slash = path.rfind('/');
        const std::string folder = slash == std::string::npos ? "/" : path.substr(0, slash + 1);
        next = url.substr(0, host) + folder + location;
    }

    // A '#fragment' is for browsers; servers never see it.
    const size_t fragment = next.find('#');
    if (fragment != std::string::npos) next.erase(fragment);

    // Never from a checked, encrypted connection to an open one.
    if (scheme == 8 && schemeLength(next) != 8) return false;

    if (!reads) method = "GET";
    // "HTTPS://" is the same scheme; the rest of the firmware expects it in lower case.
    const size_t nextScheme = schemeLength(next);
    url = lower(next.substr(0, nextScheme)) + next.substr(nextScheme);
    return true;
}

}  // namespace deskhog
