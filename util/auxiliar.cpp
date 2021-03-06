/*
 * Copyright (c) 2014 Ahmed Samy  <f.fallen45@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "auxiliar.h"

#include <string.h>

#include <memory>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include <boost/filesystem.hpp>

UrlData parseUrl(const std::string &str)
{
	/// TODO: Regex could be quicker?
	std::string protocol, host, port;

	const std::string protocol_end("://");
	auto protocolIter = std::search(str.begin(), str.end(), protocol_end.begin(), protocol_end.end());
	if (protocolIter == str.end()) {
		std::cerr << str << ": unable to find start of protocol" << std::endl;
		return std::make_tuple("", "", "");
	}

	protocol.reserve(std::distance(str.begin(), protocolIter));		// reserve for "http"/"https" etc.
	std::transform(
		str.begin(), protocolIter,
		std::back_inserter(protocol), std::ptr_fun<int, int>(tolower)
	);
	std::advance(protocolIter, protocol_end.length());	// eat "://"

	auto hostIter = std::find(protocolIter, str.end(), ':');	// could be protocol://host:port/
	if (hostIter == str.end())
		hostIter = std::find(protocolIter, str.end(), '/');		// Nope, protocol://host/?query

	host.reserve(std::distance(protocolIter, hostIter));
	std::transform(
		protocolIter, hostIter,
		std::back_inserter(host), std::ptr_fun<int, int>(tolower)
	);

	auto portIter = std::find(hostIter, str.end(), ':');
	if (portIter == str.end())								// Port optional
		return std::make_tuple(protocol, host, protocol);	// No port, it's according to the protocol then

	auto queryBegin = std::find(portIter, str.end(), '/');	// Can we find a "/" of where the query would begin?
	if (queryBegin != str.end())
		port.assign(portIter + 1, queryBegin);
	else
		port.assign(portIter + 1, str.end());

	return std::make_tuple(protocol, host, port);
}

/**
 * bytesToHumanReadable() from:
 *  http://stackoverflow.com/a/3758880/1551592 (Java version)
 */
std::string bytesToHumanReadable(uint32_t bytes, bool si)
{
	uint32_t u = si ? 1000 : 1024;
	if (bytes < u)
		return std::to_string(bytes) + " B";

	size_t exp = static_cast<size_t>(std::log(bytes) / std::log(u));
	const char *e = si ? "kMGTPE" : "KMGTPE";
	std::ostringstream os;
	os << static_cast<double>(bytes / std::pow(u, exp)) << " ";
	os << e[exp - 1] << (si ? "" : "i") << "B";

	return os.str();
}

std::string ip2str(uint32_t ip)
{
	char buffer[17];
	sprintf(buffer, "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
	return buffer;
}

uint32_t str2ip(const std::string &ip)
{
	const uint8_t *c = (const uint8_t *)ip.c_str();
//	return c[0] | c[1] << 8 | c[2] << 16 | c[3] << 24;
	return c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3];
}

std::string getcwd()
{
	const size_t chunkSize = 255;
	const int maxChunks = 10240; // 2550 KiBs of current path are more than enough

	char stackBuffer[chunkSize]; // Stack buffer for the "normal" case
	if (getcwd(stackBuffer, sizeof(stackBuffer)))
		return stackBuffer;

	if (errno != ERANGE)
		throw std::runtime_error("Cannot determine the current path.");

	for (int chunks = 2; chunks < maxChunks; chunks++) {
		std::unique_ptr<char> cwd(new char[chunkSize * chunks]);
		if (getcwd(cwd.get(), chunkSize * chunks))
			return cwd.get();

		if (errno != ERANGE)
			throw std::runtime_error("Cannot determine the current path.");
	}

	throw std::runtime_error("Cannot determine the current path; path too long");
}

/** Following function shamelessly copied from:
 * 	http://stackoverflow.com/a/7214192/502230
 *  And converted to take just the request instead of the
 *  entire URL, e.g.:
 *  std::string str = "?what=0xbaadf00d&something=otherthing";
 *  std::string enc = urlencode(str);
*/
std::string urlencode(const std::string &value)
{
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex << std::uppercase;

	for (std::string::const_iterator i = value.begin(); i != value.end(); ++i) {
		std::string::value_type c = *i;
		// Keep alphanumeric and other accepted characters intact
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			escaped << c;
		else // Any other characters are percent-encoded
			escaped << '%' << std::setw(2) << static_cast<int>((unsigned char)c);
	}

	return escaped.str();
}

bool validatePath(const std::string &base, const std::string &path)
{
	boost::filesystem::path p1(path);
	boost::filesystem::path p2(base);

	return p1.parent_path() == p2.parent_path();
}

bool nodeExists(const std::string &node)
{
#ifdef _WIN32
	return PathFileExists(node.c_str());
#else
	struct stat st;
	return stat(node.c_str(), &st) == 0;
#endif
}

