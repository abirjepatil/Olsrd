// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/view_cache_helper.h"

#include "base/string_util.h"
#include "net/base/escape.h"
#include "net/base/io_buffer.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_cache.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/url_request/url_request_context.h"

#define VIEW_CACHE_HEAD \
  "<html><body><table>"

#define VIEW_CACHE_TAIL \
  "</table></body></html>"

static void HexDump(const char *buf, size_t buf_len, std::string* result) {
  const size_t kMaxRows = 16;
  int offset = 0;

  const unsigned char *p;
  while (buf_len) {
    StringAppendF(result, "%08x:  ", offset);
    offset += kMaxRows;

    p = (const unsigned char *) buf;

    size_t i;
    size_t row_max = std::min(kMaxRows, buf_len);

    // print hex codes:
    for (i = 0; i < row_max; ++i)
      StringAppendF(result, "%02x  ", *p++);
    for (i = row_max; i < kMaxRows; ++i)
      result->append("    ");

    // print ASCII glyphs if possible:
    p = (const unsigned char *) buf;
    for (i = 0; i < row_max; ++i, ++p) {
      if (*p < 0x7F && *p > 0x1F) {
        AppendEscapedCharForHTML(*p, result);
      } else {
        result->push_back('.');
      }
    }

    result->push_back('\n');

    buf += row_max;
    buf_len -= row_max;
  }
}

static std::string FormatEntryInfo(disk_cache::Entry* entry,
                                   const std::string& url_prefix) {
  std::string key = entry->GetKey();
  GURL url = GURL(url_prefix + key);
  std::string row =
      "<tr><td><a href=\"" + url.spec() + "\">" + EscapeForHTML(key) +
      "</a></td></tr>";
  return row;
}

static std::string FormatEntryDetails(disk_cache::Entry* entry) {
  std::string result = EscapeForHTML(entry->GetKey());

  net::HttpResponseInfo response;
  bool truncated;
  if (net::HttpCache::ReadResponseInfo(entry, &response, &truncated) &&
      response.headers) {
    if (truncated)
      result.append("<pre>RESPONSE_INFO_TRUNCATED</pre>");

    result.append("<hr><pre>");
    result.append(EscapeForHTML(response.headers->GetStatusLine()));
    result.push_back('\n');

    void* iter = NULL;
    std::string name, value;
    while (response.headers->EnumerateHeaderLines(&iter, &name, &value)) {
      result.append(EscapeForHTML(name));
      result.append(": ");
      result.append(EscapeForHTML(value));
      result.push_back('\n');
    }
    result.append("</pre>");
  }

  for (int i = 0; i < 2; ++i) {
    result.append("<hr><pre>");

    int data_size = entry->GetDataSize(i);

    if (data_size) {
      scoped_refptr<net::IOBuffer> buffer = new net::IOBuffer(data_size);
      if (entry->ReadData(i, 0, buffer, data_size, NULL) == data_size)
        HexDump(buffer->data(), data_size, &result);
    }

    result.append("</pre>");
  }

  return result;
}

static disk_cache::Backend* GetDiskCache(URLRequestContext* context) {
  if (!context)
    return NULL;

  if (!context->http_transaction_factory())
    return NULL;

  net::HttpCache* http_cache = context->http_transaction_factory()->GetCache();
  if (!http_cache)
    return NULL;

  return http_cache->GetBackend();
}

static std::string FormatStatistics(disk_cache::Backend* disk_cache) {
  std::vector<std::pair<std::string, std::string> > stats;
  disk_cache->GetStats(&stats);
  std::string result;

  for (size_t index = 0; index < stats.size(); index++) {
    result.append(stats[index].first);
    result.append(": ");
    result.append(stats[index].second);
    result.append("<br/>\n");
  }

  return result;
}

// static
void ViewCacheHelper::GetEntryInfoHTML(const std::string& key,
                                       URLRequestContext* context,
                                       const std::string& url_prefix,
                                       std::string* data) {
  disk_cache::Backend* disk_cache = GetDiskCache(context);
  if (!disk_cache) {
    data->assign("no disk cache");
    return;
  }

  if (key.empty()) {
    data->assign(VIEW_CACHE_HEAD);
    void* iter = NULL;
    disk_cache::Entry* entry;
    while (disk_cache->OpenNextEntry(&iter, &entry)) {
      data->append(FormatEntryInfo(entry, url_prefix));
      entry->Close();
    }
    data->append(VIEW_CACHE_TAIL);
  } else {
    disk_cache::Entry* entry;
    if (disk_cache->OpenEntry(key, &entry)) {
      data->assign(FormatEntryDetails(entry));
      entry->Close();
    } else {
      data->assign("no matching cache entry for: " + key);
    }
  }
}

// static
void ViewCacheHelper::GetStatisticsHTML(URLRequestContext* context,
                                        std::string* data) {
  disk_cache::Backend* disk_cache = GetDiskCache(context);
  if (!disk_cache) {
    data->append("no disk cache");
    return;
  }
  data->append(FormatStatistics(disk_cache));
}
