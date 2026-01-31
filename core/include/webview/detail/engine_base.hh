/*
 * MIT License
 *
 * Copyright (c) 2017 Serge Zaitsev
 * Copyright (c) 2022 Steffen André Langnes
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef WEBVIEW_ENGINE_BASE_HH
#define WEBVIEW_ENGINE_BASE_HH

#if defined(__cplusplus) && !defined(WEBVIEW_HEADER)

#include "../errors.hh"
#include "../types.hh"
#include "../user_script.hh"
#include "../utility/json.hh"

#include <atomic>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <string>
#include <utility>

namespace webview {
namespace detail {

// The engine_base class provides a common interface for all backends.
// It also implements common functionality such as bindings and user scripts.
class engine_base {
public:
  engine_base(bool owns_window) : m_owns_window{owns_window} {}

  virtual ~engine_base() = default;

  // Run the main loop.
  noresult run() { return run_impl(); }

  // Terminate the main loop.
  noresult terminate() { return terminate_impl(); }

  // Dispatch a function to the main loop.
  noresult dispatch(dispatch_fn_t f) { return dispatch_impl(f); }

  // Get the window handle.
  result<void *> window() { return window_impl(); }

  // Get the widget handle.
  result<void *> widget() { return widget_impl(); }

  // Get the browser controller handle.
  result<void *> browser_controller() { return browser_controller_impl(); }

  // Set the window title.
  noresult set_title(const std::string &title) { return set_title_impl(title); }

  // Set the window size.
  noresult set_size(int width, int height, webview_hint_t hints) {
    // If the window is not shown yet, we can defer the size setting until
    // the window is shown.
    if (m_default_size_guard) {
      m_default_width = width;
      m_default_height = height;
      m_default_hints = hints;
      return {};
    }
    return set_size_impl(width, height, hints);
  }

  // Navigate to a URL.
  noresult navigate(const std::string &url) { return navigate_impl(url); }

  // Set the HTML content.
  noresult set_html(const std::string &html) { return set_html_impl(html); }

  // Inject a script to be executed on page load.
  noresult init(const std::string &js) {
    auto script = add_user_script_impl(js);
    m_user_scripts.emplace_back(std::move(script));
    return {};
  }

  // Evaluate a script.
  noresult eval(const std::string &js) { return eval_impl(js); }

  // Bind a function to a name.
  noresult bind(const std::string &name, binding_t f, void *arg) {
    if (m_bindings.count(name) > 0) {
      return error_info{WEBVIEW_ERROR_DUPLICATE};
    }
    m_bindings.emplace(name, binding_ctx_t{f, arg});
    auto js = "(function() { var name = '" + name + "';" + R"(
      var RPC = window._rpc = (window._rpc || {nextSeq: 1});
      window[name] = function() {
        var seq = RPC.nextSeq++;
        var promise = new Promise(function(resolve, reject) {
          RPC[seq] = {
            resolve: resolve,
            reject: reject,
          };
        });
        window.external.invoke(JSON.stringify({
          id: seq,
          method: name,
          params: Array.prototype.slice.call(arguments),
        }));
        return promise;
      }
    })())";
    return init(js);
  }

  // Unbind a function.
  noresult unbind(const std::string &name) {
    auto found = m_bindings.find(name);
    if (found == m_bindings.end()) {
      return error_info{WEBVIEW_ERROR_NOT_FOUND};
    }
    m_bindings.erase(found);
    auto js = "delete window['" + name + "'];";
    return init(js);
  }

  // Resolve a promise.
  noresult resolve(const std::string &seq, int status,
                   const std::string &result) {
    auto js = "window._rpc[" + seq + "]." +
              (status == 0 ? "resolve" : "reject") + "(" + result +
              "); delete window._rpc[" + seq + "]";
    return eval(js);
  }

protected:
  virtual noresult run_impl() = 0;
  virtual noresult terminate_impl() = 0;
  virtual noresult dispatch_impl(dispatch_fn_t f) = 0;
  virtual result<void *> window_impl() = 0;
  virtual result<void *> widget_impl() = 0;
  virtual result<void *> browser_controller_impl() = 0;
  virtual noresult set_title_impl(const std::string &title) = 0;
  virtual noresult set_size_impl(int width, int height,
                                 webview_hint_t hints) = 0;
  virtual noresult navigate_impl(const std::string &url) = 0;
  virtual noresult set_html_impl(const std::string &html) = 0;
  virtual noresult eval_impl(const std::string &js) = 0;
  virtual user_script add_user_script_impl(const std::string &js) = 0;
  virtual void
  remove_all_user_scripts_impl(const std::list<user_script> &scripts) = 0;
  virtual bool are_user_scripts_equal_impl(const user_script &first,
                                           const user_script &second) = 0;

  // Run the event loop while the condition is true.
  // This is used to pump the event loop while waiting for a result.
  // The condition is checked before each iteration.
  virtual void run_event_loop_while(std::function<bool()> fn) = 0;

  bool owns_window() const { return m_owns_window; }

  void on_window_created() {
    // Re-add user scripts if the window was recreated.
    // This is needed for GTK because the webview is destroyed when the window
    // is closed.
    if (!m_user_scripts.empty()) {
      remove_all_user_scripts_impl(m_user_scripts);
      for (auto &script : m_user_scripts) {
        script = add_user_script_impl(script.get_source());
      }
    }
  }

  void on_window_destroyed(bool widget_destroyed = false) {
    if (widget_destroyed) {
      // If the widget was destroyed, we need to clear the user scripts
      // because the underlying native objects are gone.
      // We don't need to remove them from the backend because the backend
      // handles that automatically when the webview is destroyed.
      m_user_scripts.clear();
    }
  }

  void on_message(const std::string &msg) {
    auto seq = json_parse(msg, "id", 0);
    auto name = json_parse(msg, "method", 0);
    auto args = json_parse(msg, "params", 0);
    auto found = m_bindings.find(name);
    if (found == m_bindings.end()) {
      return;
    }
    found->second.fn(seq, args, found->second.arg);
  }

  void add_init_script(const std::string &js) {
    auto script = add_user_script_impl(js);
    m_user_scripts.emplace_back(std::move(script));
  }

  void set_default_size_guard(bool enable) { m_default_size_guard = enable; }

  void dispatch_size_default() {
    if (m_default_width > 0 && m_default_height > 0) {
      set_size_impl(m_default_width, m_default_height, m_default_hints);
    }
  }

private:
  struct binding_ctx_t {
    binding_t fn;
    void *arg;
  };

  bool m_owns_window;
  std::map<std::string, binding_ctx_t> m_bindings;
  std::list<user_script> m_user_scripts;
  bool m_default_size_guard{false};
  int m_default_width{0};
  int m_default_height{0};
  webview_hint_t m_default_hints{WEBVIEW_HINT_NONE};
};

} // namespace detail
} // namespace webview

#endif // defined(__cplusplus) && !defined(WEBVIEW_HEADER)
#endif // WEBVIEW_ENGINE_BASE_HH
