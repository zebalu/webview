#include "webview/webview.h"

#include <iostream>

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE /*hInst*/, HINSTANCE /*hPrevInst*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
#else
int main() {
#endif
  try {
    webview::webview w(false, nullptr, false);
    w.set_title("Basic Example");
    w.set_size(480, 320, WEBVIEW_HINT_NONE);
    w.set_html("<p style='background-color: white;'>Thanks for using webview!</p>");
    w.run();
  } catch (const webview::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }

  return 0;
}
