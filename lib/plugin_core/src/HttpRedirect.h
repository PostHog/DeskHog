#pragma once

// What a request does with a 3xx answer. The firmware follows redirects itself
// instead of leaving it to the Arduino HTTPClient, which fails on a `Location`
// that is only a path and never changes between http and https.
//
//   - A GET or HEAD follows 301, 302, 303, 307 and 308.
//   - Any other method follows 302 and 303 only, as a GET without its body
//     (what browsers do). 301, 307 and 308 would mean sending the body again
//     somewhere else: the plugin gets that answer and decides.
//   - `Location` may be a whole address, "//host/path", "/path" or a path
//     next to the one that answered.
//   - http may be sent on to https, https never back to http.
//   - At most kMaxRedirects in a row. After that the plugin gets the 3xx.
//   - `Authorization` and `Cookie` headers are not sent along to another host.
//
// No Arduino in here, so the tests cover it.

#include <string>

namespace deskhog {

const int kMaxRedirects = 5;

/**
 * Where a 3xx answer sends the request next.
 *
 * @param status    The answer's HTTP status
 * @param method    The request's method. Changed to GET where the redirect asks for it
 * @param url       The address that answered. Changed to the next one
 * @param location  The answer's `Location` header
 * @return false if this answer is not followed: it is the plugin's answer
 */
bool followRedirect(int status, std::string& method, std::string& url, const std::string& location);

/** "https://example.com:8443" of "https://Example.com:8443/a?b", in lower case. "" if it is not http(s). */
std::string urlOrigin(const std::string& url);

/** Is this a header that only the host it was meant for may see? */
bool isCredentialHeader(const std::string& name);

}  // namespace deskhog
