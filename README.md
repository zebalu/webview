# webview

A tiny cross-platform webview library for C/C++/Go to build modern cross-platform
GUIs.

The goal of the project is to create a common HTML5 UI abstraction layer for the
most widely used platforms.

It supports two-way JavaScript bindings (to call C/C++/Go from JS and to call
JS from C/C++/Go).

It uses Cocoa/WebKit on macOS, gtk-webkit2 on Linux and Edge on Windows 10.

## Webview for C/C++

### API

```c
// Creates a new webview instance. If debug is non-zero - developer tools will
// be enabled (if the platform supports them). Window parameter can be a
// pointer to the native window handle. If it's NULL - then a new window
// is created.
webview_t webview_create(int debug, void *window);

// Creates a new webview instance with headless mode option.
// If headless is non-zero, the webview window will not be shown.
webview_t webview_create_headless(int debug, void *window, int headless);

// Destroys a webview and closes the native window.
void webview_destroy(webview_t w);

// Runs the main loop until it's terminated. After this function exits - you
// must destroy the webview.
void webview_run(webview_t w);

// Stops the main loop. It is safe to call this function from another other
// background thread.
void webview_terminate(webview_t w);

// Posts a function to be executed on the main thread. You normally do not need
// to call this function, unless you want to tweak the native window.
void webview_dispatch(webview_t w, void (*fn)(webview_t w, void *arg), void *arg);

// Returns a native window handle pointer. When using GTK backend the pointer
// is GtkWindow pointer, when using Cocoa backend the pointer is NSWindow
// pointer, when using Win32 backend the pointer is HWND pointer.
void *webview_get_window(webview_t w);

// Updates the title of the native window. Must be called from the UI thread.
void webview_set_title(webview_t w, const char *title);

// Updates native window size. See WEBVIEW_HINT constants.
void webview_set_size(webview_t w, int width, int height, int hints);

// Navigates webview to the given URL. URL may be a data URI, i.e.
// "data:text/html,<html>...</html>". It is often handy to embed your HTML
// into a header file (see echo-html.c) and use it as a data URI.
void webview_navigate(webview_t w, const char *url);

// Set webview HTML directly.
void webview_set_html(webview_t w, const char *html);

// Injects JavaScript code at the initialization of the new page. Every time
// the webview will open a the new page - this initialization code will be
// executed. It is guaranteed that code is executed before window.onload.
void webview_init(webview_t w, const char *js);

// Evaluates arbitrary JavaScript code. Evaluation happens asynchronously, also
// the result of the expression is ignored. Use RPC bindings if you want to
// receive notifications about the results of the evaluation.
void webview_eval(webview_t w, const char *js);

// Binds a native C callback so that it will appear under the given name as a
// global JavaScript function. Internally it uses webview_init(). Callback
// receives a request string and a user-provided argument pointer. Request
// string is a JSON array of all the arguments passed to the JavaScript
// function.
void webview_bind(webview_t w, const char *name, void (*fn)(const char *seq, const char *req, void *arg), void *arg);

// Removes a native C callback that was previously bound by webview_bind.
void webview_unbind(webview_t w, const char *name);

// Allows to return a value from the native binding. Original request pointer
// must be provided to help internal RPC engine match requests with responses.
// If status is zero - result is expected to be a valid JSON result value.
// If status is not zero - result is an error JSON object.
void webview_return(webview_t w, const char *seq, int status, const char *result);
```

### Example

```c
#include "webview.h"
#include <stddef.h>

int main() {
  webview_t w = webview_create(0, NULL);
  webview_set_title(w, "Webview Example");
  webview_set_size(w, 480, 320, WEBVIEW_HINT_NONE);
  webview_set_html(w, "Thanks for using webview!");
  webview_run(w);
  webview_destroy(w);
  return 0;
}
```

### Headless Example

```c
#include "webview.h"
#include <stdio.h>
#include <stdlib.h>

void on_result(const char *seq, const char *req, void *arg) {
    printf("Result from JS: %s\n", req);
    webview_terminate((webview_t)arg);
}

int main() {
    // Create a headless webview
    webview_t w = webview_create_headless(0, NULL, 1);
    
    // HTML content with a function to test
    const char *html = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>test</title>"
        "<script>"
        "function testFunction(a, b) {"
        "    return a*b;"
        "}"
        "</script>"
        "</head>"
        "<body>"
        "</body>"
        "</html>";

    webview_set_html(w, html);

    // Bind a C function to receive the result
    webview_bind(w, "reportResult", on_result, w);

    // Evaluate the function and send the result back to C
    // We wrap the call in a timeout to ensure the page is loaded
    webview_eval(w, "setTimeout(function() { reportResult(testFunction(2, 3)); }, 100);");

    webview_run(w);
    webview_destroy(w);
    return 0;
}
```

## Notes

* `webview_create` and `webview_create_headless` return `NULL` on failure.
* `webview_bind` callback receives a JSON array of arguments.
* `webview_return` expects a valid JSON string as a result.

## License

Code is distributed under MIT license, feel free to use it in your proprietary
projects as well.
