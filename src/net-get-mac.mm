/*
AC Scene Groups
Copyright (C) 2026 alejoxcode

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "net-get.hpp"

#import <Foundation/Foundation.h>

namespace acsg {

    HttpResponse https_get(const std::string &url, const std::string &userAgent, int timeoutMs)
    {
        HttpResponse result;
        @autoreleasepool {
            NSURL *address = [NSURL URLWithString:@(url.c_str())];
            if (!address) {
                result.error = "invalid URL";
                return result;
            }

            NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
            config.HTTPCookieStorage = nil;
            config.URLCache = nil;
            NSURLSession *session = [NSURLSession sessionWithConfiguration:config];

            NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:address
                                                                   cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                               timeoutInterval:timeoutMs / 1000.0];
            [request setValue:@(userAgent.c_str()) forHTTPHeaderField:@"User-Agent"];
            [request setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];

            __block HttpResponse out;
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            NSURLSessionDataTask *task = [session
                dataTaskWithRequest:request completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                    if (error) {
                        const char *text = error.localizedDescription.UTF8String;
                        out.error = text ? text : "network error";
                    } else {
                        out.status = (int) ((NSHTTPURLResponse *) response).statusCode;
                        if (data.length < 4 * 1024 * 1024)
                            out.body.assign(static_cast<const char *>(data.bytes), data.length);
                    }
                    dispatch_semaphore_signal(done);
                }];
            [task resume];
            /* The request has its own timeout; this is only a safety net. */
            if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW,
                                                            (int64_t) (timeoutMs + 2000) * NSEC_PER_MSEC)) != 0) {
                [task cancel];
                dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
            }
            [session finishTasksAndInvalidate];
            result = out;
        }
        return result;
    }

}  // namespace acsg
