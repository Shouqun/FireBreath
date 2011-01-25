/**********************************************************\
Original Author: Richard Bateman

Created:    Jan 24, 2011
License:    Dual license model; choose one of two:
            New BSD License
            http://www.opensource.org/licenses/bsd-license.php
            - or -
            GNU Lesser General Public License, version 2.1
            http://www.gnu.org/licenses/lgpl-2.1.html

Copyright 2011 Richard Bateman, 
               Firebreath development team
\**********************************************************/

#include "BrowserHost.h"
#include <boost/algorithm/string.hpp>

#include "SimpleStreamHelper.h"

static const int MEGABYTE = 1024 * 1024;

FB::SimpleStreamHelperPtr FB::SimpleStreamHelper::AsyncGet( const FB::BrowserHostPtr& host, const FB::URI& uri,
    const HttpCallback& callback, const bool cache /*= true*/, const size_t bufferSize /*= 256*1024*/ )
{
    FB::SimpleStreamHelperPtr ptr(boost::make_shared<FB::SimpleStreamHelper>(host, callback, bufferSize));
    // This is kinda a weird trick; it's responsible for freeing itself, unless something decides
    // to hold a reference to it.
    ptr->keepReference(ptr);
    FB::BrowserStreamPtr stream(host->createStream(uri.toString(), ptr, true, false));
    return ptr;
}

FB::SimpleStreamHelper::SimpleStreamHelper( const BrowserHostPtr& host, const HttpCallback& callback, const size_t blockSize )
    : host(host), blockSize(blockSize), received(0), callback(callback)
{

}

bool FB::SimpleStreamHelper::onStreamCompleted( FB::StreamCompletedEvent *evt, FB::BrowserStream * )
{
    if (!evt->success) {
        callback(false, FB::HeaderMap(), boost::shared_array<uint8_t>(), received);
        return false;
    }
    if (!data) {
        data = boost::shared_array<uint8_t>(new uint8_t[received]);
        int i = 0;
        for (BlockList::const_iterator it = blocks.begin();
            it != blocks.end(); ++it) {
            size_t offset(i * blockSize);
            size_t len(received - offset);
            if (len > blockSize)
                len = blockSize;

            std::copy(it->get(), it->get()+len, data.get()+offset);
            ++i;
        }
        // Free all the old blocks
        blocks.clear();
    }
    callback(true, parse_http_headers(stream->getHeaders()), data, received);
    return true;
}

bool FB::SimpleStreamHelper::onStreamOpened( FB::StreamOpenedEvent *evt, FB::BrowserStream * )
{
    if (getStream()->getLength() && !boost::algorithm::contains(getStream()->getHeaders(), "gzip")) {
        // Phew! we know the length!
        data = boost::shared_array<uint8_t>(new uint8_t[getStream()->getLength()]);
    }
    return false;
}

bool FB::SimpleStreamHelper::onStreamDataArrived( FB::StreamDataArrivedEvent *evt, FB::BrowserStream * )
{
    received += evt->getLength();
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(evt->getData());
    const uint8_t* endbuf = buf + evt->getLength();
    if (data) {
        // If we know the length, we just copy it right in!
        // Unfortunately, there are cases where we think we know the length but the web server
        // lies to us.  We have to fix that here :-/
        if (stream->getLength() < evt->getDataPosition() + evt->getLength()) {
            received -= evt->getLength();
            FB::StreamDataArrivedEvent tmp(getStream().get(), data.get(), received, 0, 0);
            boost::shared_array<uint8_t> tmpData;
            data.reset();
            onStreamDataArrived(&tmp, getStream().get());
            return onStreamDataArrived(evt, getStream().get());
        }
        std::copy(buf, buf+evt->getLength(), data.get()+evt->getLength());
    } else {
        int len = evt->getLength();
        int offset = evt->getDataPosition();
        while (buf < endbuf) {
            size_t n = offset / blockSize;
            size_t pos = offset % blockSize;
            if (blocks.size() < n+1) {
                blocks.push_back(boost::shared_array<uint8_t>(new uint8_t[blockSize]));
            }
            uint8_t *destBuf = blocks.back().get();
            //if (pos + len > )
            int curLen = len;
            if (pos + len >= blockSize) {
                // If there isn't room in the current block, copy what there is room for
                // and loop
                curLen = blockSize-pos;
            }
            // Copy the bytes that fit in this buffer
            std::copy(buf, buf+curLen, destBuf+offset);
            buf += curLen;
            offset += curLen;
            len -= curLen;
        }
    }
    return false;
}

FB::HeaderMap FB::SimpleStreamHelper::parse_http_headers(const std::string& headers )
{
    FB::HeaderMap res;
    std::vector<std::string> lines;
    boost::split(lines, headers, boost::is_any_of("\r\n"));
    for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        std::string line = boost::trim_copy(*it);
        if (line.empty()) continue;
        size_t loc = line.find(':');
        if (loc == std::string::npos) {
            // Weird; bad header
            continue;
        }
        res.insert(std::make_pair(boost::trim_copy(line.substr(0, loc)),
            boost::trim_copy(line.substr(loc + 1))));
    }
    return res;
}

void FB::SimpleStreamHelper::keepReference( const SimpleStreamHelperPtr& ptr )
{
    self = ptr;
}