/*
 * UI Core Demo — all UI defined in app.ui, this file only handles events.
 */
#include <ui_core.h>
#include <windows.h>
#include <string>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <typeinfo>
#include <thread>

#include "../src/ui/markup/markup.h"
#include "../src/ui/controls.h"
#include "../src/ui/ui_context.h"
#include "../src/ui/ui_window.h"

static ui::UiMarkup g_layout;
static UiWindow g_win = UI_INVALID;
static volatile bool g_shutdown = false;

static const char* g_navIds[] = {
    "nav_home", "nav_button", "nav_check", "nav_input",
    "nav_range", "nav_status", "nav_layout", "nav_crop", "nav_settings"
};
static constexpr int kPageCount = 9;

static void switchPage(int page) {
    auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
    if (!stack) return;
    for (int i = 0; i < kPageCount; i++) {
        auto* item = g_layout.FindAs<ui::NavItemWidget>(g_navIds[i]);
        if (item) item->SetSelected(i == page);
    }
    if (stack->ActiveIndex() == page) { if (g_win) ui_window_invalidate(g_win); return; }
    stack->SetActiveIndex(page);
    stack->DoLayout();
    if (g_win) ui_window_invalidate(g_win);
}

static void updateCropInfo() {
    auto* iv = g_layout.FindAs<ui::ImageViewWidget>("cropImage");
    if (!iv || !iv->IsCropMode()) return;
    float cx, cy, cw, ch;
    iv->GetCropRect(cx, cy, cw, ch);
    wchar_t buf[128];
    swprintf(buf, 128, L"Crop: %d x %d  at (%d, %d)", (int)cw, (int)ch, (int)cx, (int)cy);
    g_layout.SetText("crop_info", buf);
    if (g_win) ui_window_invalidate(g_win);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init();

    // Toggle pane
    g_layout.SetHandler("onTogglePane", std::function<void()>([]() {
        auto* sv = g_layout.FindAs<ui::SplitViewWidget>("mainSplit");
        if (sv) { sv->TogglePane(); if (g_win) ui_window_invalidate(g_win); }
    }));

    // Navigation (9 pages)
    for (int i = 0; i < kPageCount; i++) {
        char name[16]; snprintf(name, sizeof(name), "onNav%d", i);
        g_layout.SetHandler(name, std::function<void()>([i](){ switchPage(i); }));
    }

    // Theme toggle
    g_layout.SetHandler("onDarkMode", std::function<void(bool)>([](bool on) {
        ui_theme_set_mode(on ? UI_THEME_DARK : UI_THEME_LIGHT);
    }));

    // Flyout demo
    g_layout.SetHandler("onDismissFlyout", std::function<void()>([]() {
        auto* flyout = g_layout.FindAs<ui::FlyoutWidget>("demoFlyout");
        if (flyout && flyout->IsOpen()) {
            flyout->Hide();
            if (g_win) ui_window_invalidate(g_win);
        }
    }));
    g_layout.SetHandler("onShowFlyout", std::function<void()>([]() {
        auto* flyout = g_layout.FindAs<ui::FlyoutWidget>("demoFlyout");
        auto* btn = g_layout.FindById("flyoutBtn");
        if (flyout && btn) {
            if (flyout->IsOpen()) flyout->Hide();
            else flyout->Show(btn);
            if (g_win) ui_window_invalidate(g_win);
        }
    }));

    // Language switch
    g_layout.SetHandler("onLanguage", std::function<void(int)>([](int index) {
        const wchar_t* langFiles[] = {L"lang/zh-CN.lang", L"lang/en.lang"};
        const wchar_t* prefixes[] = {L"../demo/lang/", L"../../demo/lang/"};
        const wchar_t* names[] = {L"zh-CN.lang", L"en.lang"};

        bool loaded = g_layout.LoadLanguage(langFiles[index]);
        if (!loaded) {
            for (auto& p : prefixes) {
                std::wstring path = std::wstring(p) + names[index];
                if (g_layout.LoadLanguage(path)) { loaded = true; break; }
            }
        }
        if (loaded) {
            g_layout.ApplyLanguage();
            if (g_win) ui_window_invalidate(g_win);
        }
    }));

    // Crop toggle
    g_layout.SetHandler("onCropToggle", std::function<void()>([]() {
        auto* iv = g_layout.FindAs<ui::ImageViewWidget>("cropImage");
        if (!iv || !iv->HasImage()) return;
        iv->SetCropMode(!iv->IsCropMode());
        if (iv->IsCropMode()) {
            iv->onCropChanged = [](float x, float y, float w, float h) { updateCropInfo(); };
            updateCropInfo();
        } else {
            g_layout.SetText("crop_info", L"Crop mode off");
        }
        if (g_win) ui_window_invalidate(g_win);
    }));

    // Crop aspect ratio
    g_layout.SetHandler("onCropRatio", std::function<void(int)>([](int index) {
        auto* iv = g_layout.FindAs<ui::ImageViewWidget>("cropImage");
        if (!iv) return;
        float ratios[] = {0, 1.0f, 4.0f/3.0f, 16.0f/9.0f, 3.0f/2.0f};
        iv->SetCropAspectRatio(ratios[index]);
        if (iv->IsCropMode()) { iv->ResetCrop(); updateCropInfo(); }
        if (g_win) ui_window_invalidate(g_win);
    }));

    // Load .ui file
    bool loaded = g_layout.LoadFile(L"app.ui");
    if (!loaded) loaded = g_layout.LoadFile(L"../demo/app.ui");
    if (!loaded) loaded = g_layout.LoadFile(L"../../demo/app.ui");
    if (!loaded) {
        MessageBoxA(nullptr, g_layout.LastError().c_str(), "Load Error", MB_ICONERROR);
        ui_shutdown();
        return 1;
    }

    // Load default language (Chinese)
    if (!g_layout.LoadLanguage(L"lang/zh-CN.lang"))
        if (!g_layout.LoadLanguage(L"../demo/lang/zh-CN.lang"))
            g_layout.LoadLanguage(L"../../demo/lang/zh-CN.lang");
    g_layout.ApplyLanguage();

    // Create window
    auto& wh = g_layout.Window();
    UiWindowConfig cfg = {0};
    cfg.width = wh.width > 0 ? wh.width : 1060;
    cfg.height = wh.height > 0 ? wh.height : 700;
    cfg.resizable = wh.resizable >= 0 ? wh.resizable : 1;
    cfg.accept_files = 1;
    std::wstring winTitle = L"UI Core Demo";
    if (!wh.title.empty()) winTitle.assign(wh.title.begin(), wh.title.end());
    cfg.title = winTitle.c_str();

    g_win = ui_window_create(&cfg);
    if (g_win == UI_INVALID) { ui_shutdown(); return 1; }

    // File drop → load into crop ImageView
    ui_window_on_drop(g_win, [](UiWindow win, const wchar_t* path, void* ud) {
        auto* iv = g_layout.FindAs<ui::ImageViewWidget>("cropImage");
        if (!iv) return;
        auto& ctx = ui::GetContext();
        auto* w = ctx.GetWindow(win);
        if (!w) return;
        iv->LoadFromFile(path, w->GetRenderer());
        iv->FitToView();
        iv->SetCropMode(false);
        g_layout.SetText("crop_info", std::wstring(L"Loaded: ") + path);
        switchPage(7);  // switch to crop page
        ui_window_invalidate(win);
    }, nullptr);

    // Debug pipe: \\.\pipe\ui_core_debug
    // Send "tree" → get widget tree JSON
    // Send "widget <id>" → get single widget info
    // Send "highlight <id>" → highlight widget
    static HANDLE g_pipe = INVALID_HANDLE_VALUE;
    static HANDLE g_pipeEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    std::thread([&]() {
        while (!g_shutdown) {
            HANDLE pipe = CreateNamedPipeA("\\\\.\\pipe\\ui_core_debug",
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1, 65536, 65536, 0, NULL);
            if (pipe == INVALID_HANDLE_VALUE) break;
            g_pipe = pipe;

            // Overlapped ConnectNamedPipe so we can cancel on shutdown
            OVERLAPPED ov = {}; ov.hEvent = g_pipeEvent;
            ResetEvent(g_pipeEvent);
            BOOL connected = ConnectNamedPipe(pipe, &ov);
            if (!connected) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    WaitForSingleObject(g_pipeEvent, INFINITE);
                    if (g_shutdown) { CancelIo(pipe); CloseHandle(pipe); break; }
                } else if (err != ERROR_PIPE_CONNECTED) {
                    CloseHandle(pipe); continue;
                }
            }
            if (g_shutdown) { CloseHandle(pipe); break; }

            char buf[256] = {};
            DWORD bytesRead = 0;
            // Synchronous read (client already connected)
            if (ReadFile(pipe, buf, sizeof(buf)-1, &bytesRead, NULL) && bytesRead > 0) {
                if (g_shutdown) { CloseHandle(pipe); break; }
                buf[bytesRead] = 0;
                std::string cmd(buf);
                std::string response;

                if (cmd == "tree") {
                    char* json = ui_debug_dump_tree(g_win);
                    if (json) { response = json; ui_debug_free(json); }
                    else response = "{}";
                }
                else if (cmd.substr(0, 6) == "widget") {
                    std::string id = cmd.size() > 7 ? cmd.substr(7) : "";
                    auto& dctx = ui::GetContext();
                    UiWidget w = ui_widget_find_by_id(
                        dctx.handles.Insert(g_layout.Root()), id.c_str());
                    if (w) {
                        char* json = ui_debug_dump_widget(w);
                        if (json) { response = json; ui_debug_free(json); }
                    } else {
                        response = "{\"error\": \"not found: " + id + "\"}";
                    }
                }
                else if (cmd.substr(0, 9) == "highlight") {
                    std::string id = cmd.size() > 10 ? cmd.substr(10) : "";
                    ui_debug_highlight(g_win, id.empty() ? nullptr : id.c_str());
                    ui_window_invalidate(g_win);
                    response = "{\"ok\": true}";
                }
                else if (cmd.substr(0, 3) == "nav") {
                    int page = cmd.size() > 4 ? std::atoi(cmd.c_str() + 4) : -1;
                    if (page >= 0 && page < kPageCount) {
                        switchPage(page);
                        char tmp[64]; snprintf(tmp, sizeof(tmp),
                            "{\"ok\": true, \"page\": %d}", page);
                        response = tmp;
                    } else {
                        response = "{\"error\": \"invalid page. use: nav 0-8\"}";
                    }
                }
                else if (cmd.substr(0, 6) == "scroll") {
                    std::string rest = cmd.size() > 7 ? cmd.substr(7) : "";
                    std::string sid; float sy = -1;
                    auto sp = rest.find(' ');
                    if (sp != std::string::npos) {
                        sid = rest.substr(0, sp);
                        sy = (float)std::atof(rest.c_str() + sp + 1);
                    } else {
                        sid = rest;
                    }
                    auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
                    ui::ScrollViewWidget* sv = nullptr;
                    if (!sid.empty()) {
                        sv = g_layout.FindAs<ui::ScrollViewWidget>(sid);
                    }
                    if (!sv && stack) {
                        int ai = stack->ActiveIndex();
                        auto& kids = stack->Children();
                        if (ai >= 0 && ai < (int)kids.size()) {
                            sv = dynamic_cast<ui::ScrollViewWidget*>(kids[ai].get());
                        }
                    }
                    if (sv) {
                        if (sy >= 0) {
                            sv->SetScrollY(sy);
                            sv->DoLayout();
                            ui_window_invalidate(g_win);
                        }
                        char tmp[128]; snprintf(tmp, sizeof(tmp),
                            "{\"ok\": true, \"scrollY\": %.1f}", sv->ScrollY());
                        response = tmp;
                    } else {
                        response = "{\"error\": \"ScrollView not found\"}";
                    }
                }
                else if (cmd == "flyout") {
                    auto* flyout = g_layout.FindAs<ui::FlyoutWidget>("demoFlyout");
                    auto* btn = g_layout.FindById("flyoutBtn");
                    if (flyout && btn) {
                        if (flyout->IsOpen()) flyout->Hide();
                        else flyout->Show(btn);
                        ui_window_invalidate(g_win);
                        char tmp[256]; snprintf(tmp, sizeof(tmp),
                            "{\"ok\": true, \"open\": %s, \"btnRect\": [%.0f,%.0f,%.0f,%.0f]}",
                            flyout->IsOpen() ? "true" : "false",
                            btn->rect.left, btn->rect.top, btn->rect.right, btn->rect.bottom);
                        response = tmp;
                    } else {
                        response = "{\"error\": \"flyout or anchor not found\"}";
                    }
                }
                else {
                    response = "{\"error\": \"commands: tree, widget <id>, highlight <id>, nav <0-8>, scroll [id] [y], flyout\"}";
                }

                DWORD written;
                WriteFile(pipe, response.c_str(), (DWORD)response.size(), &written, NULL);
                FlushFileBuffers(pipe);
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
        g_pipe = INVALID_HANDLE_VALUE;
    }).detach();

    auto& ctx = ui::GetContext();
    ui_window_set_root(g_win, ctx.handles.Insert(g_layout.Root()));
    ui_window_show(g_win);

    int ret = ui_run();

    // Signal pipe thread to stop and unblock ConnectNamedPipe
    g_shutdown = true;
    SetEvent(g_pipeEvent);
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_pipe, NULL);
    }
    // Brief wait for thread to exit before destroying globals
    Sleep(100);
    CloseHandle(g_pipeEvent);

    ui_shutdown();
    return ret;
}
