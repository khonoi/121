#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <iostream>
#include <chrono>
#include <random>
#include <shlwapi.h>
#include <mmsystem.h>
#include <algorithm> 

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Winmm.lib")

struct Config {
    int boost_sens_key = 6;
    int counter_key = 2; // Phím cần giữ để kích hoạt counter-strafe
    int stop_ms = 100;
    int tap_time_min = 150, tap_time_max = 200;
    int click_hold_time_min = 40, click_hold_time_max = 80;
    
    int scan_width = 8, scan_height = 8;
    int boost_scan_width = 4, boost_scan_height = 4;
    int counter_scan_width = 12, counter_scan_height = 12;

    int red = 240, red_tolerance = 15;
    int green = 240, green_tolerance = 15;
    int blue = 55, blue_tolerance = 45;
    int hide_console = 0;
};

class ScreenCapturer {
private:
    HDC hScreenDC, hMemDC;
    HBITMAP hBitmap, hOldBitmap;
    int width, height;
    RGBQUAD* pPixels;
public:
    ScreenCapturer() : hScreenDC(NULL), hMemDC(NULL), hBitmap(NULL), hOldBitmap(NULL), width(0), height(0), pPixels(NULL) {}
    ~ScreenCapturer() { Cleanup(); }
    void Cleanup() {
        if (hOldBitmap) SelectObject(hMemDC, hOldBitmap);
        if (hBitmap) DeleteObject(hBitmap);
        if (hMemDC) DeleteDC(hMemDC);
        if (hScreenDC) ReleaseDC(NULL, hScreenDC);
    }
    bool Initialize(int w, int h) {
        if (w == width && h == height && hMemDC != NULL) return true;
        Cleanup();
        width = w; height = h;
        hScreenDC = GetDC(NULL);
        hMemDC = CreateCompatibleDC(hScreenDC);
        BITMAPINFO bi = { 0 };
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        hBitmap = CreateDIBSection(hMemDC, &bi, DIB_RGB_COLORS, (void**)&pPixels, NULL, 0);
        hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
        return (hBitmap != NULL);
    }
    const RGBQUAD* Capture(int targetW, int targetH) {
        int sx = (GetSystemMetrics(SM_CXSCREEN) - targetW) / 2;
        int sy = (GetSystemMetrics(SM_CYSCREEN) - targetH) / 2;
        if (!BitBlt(hMemDC, 0, 0, targetW, targetH, hScreenDC, sx, sy, SRCCOPY)) return nullptr;
        return pPixels;
    }
};

class TriggerBot {
private:
    Config cfg;
    ScreenCapturer capturer;
    std::mt19937 rng;
    bool is_running;
    HWND hTargetWindow;
    // Đã xóa các biến tracking trạng thái cũ (counter_strafe_queued, counter_key_was_down)

public:
    TriggerBot() : rng(std::random_device{}()), is_running(true), hTargetWindow(NULL) {
        timeBeginPeriod(1);
        LoadConfig();
    }
    ~TriggerBot() { timeEndPeriod(1); }

    void LoadConfig() {
        char path[MAX_PATH];
        GetFullPathNameA("config.txt", MAX_PATH, path, NULL);
        if (PathFileExistsA(path)) {
            cfg.boost_sens_key = GetPrivateProfileIntA("Settings", "boost_sens_key", cfg.boost_sens_key, path);
            cfg.counter_key = GetPrivateProfileIntA("Settings", "counter_key", cfg.counter_key, path);
            cfg.stop_ms = GetPrivateProfileIntA("Settings", "stop_ms", cfg.stop_ms, path);
            cfg.tap_time_min = GetPrivateProfileIntA("Settings", "tap_time_min", cfg.tap_time_min, path);
            cfg.tap_time_max = GetPrivateProfileIntA("Settings", "tap_time_max", cfg.tap_time_max, path);
            cfg.scan_width = GetPrivateProfileIntA("Settings", "scan_width", cfg.scan_width, path);
            cfg.scan_height = GetPrivateProfileIntA("Settings", "scan_height", cfg.scan_height, path);
            cfg.boost_scan_width = GetPrivateProfileIntA("Settings", "boost_scan_width", cfg.boost_scan_width, path);
            cfg.boost_scan_height = GetPrivateProfileIntA("Settings", "boost_scan_height", cfg.boost_scan_height, path);
            cfg.counter_scan_width = GetPrivateProfileIntA("Settings", "counter_scan_width", cfg.counter_scan_width, path);
            cfg.counter_scan_height = GetPrivateProfileIntA("Settings", "counter_scan_height", cfg.counter_scan_height, path);
            cfg.click_hold_time_min = GetPrivateProfileIntA("Settings", "click_hold_time_min", cfg.click_hold_time_min, path);
            cfg.click_hold_time_max = GetPrivateProfileIntA("Settings", "click_hold_time_max", cfg.click_hold_time_max, path);
            cfg.red = GetPrivateProfileIntA("ColorRGB", "red", cfg.red, path);
            cfg.red_tolerance = GetPrivateProfileIntA("ColorRGB", "red_tolerance", cfg.red_tolerance, path);
            cfg.green = GetPrivateProfileIntA("ColorRGB", "green", cfg.green, path);
            cfg.green_tolerance = GetPrivateProfileIntA("ColorRGB", "green_tolerance", cfg.green_tolerance, path);
            cfg.blue = GetPrivateProfileIntA("ColorRGB", "blue", cfg.blue, path);
            cfg.blue_tolerance = GetPrivateProfileIntA("ColorRGB", "blue_tolerance", cfg.blue_tolerance, path);
            cfg.hide_console = GetPrivateProfileIntA("Settings", "hide_console", cfg.hide_console, path);
        }
        int maxW = std::max({cfg.scan_width, cfg.boost_scan_width, cfg.counter_scan_width});
        int maxH = std::max({cfg.scan_height, cfg.boost_scan_height, cfg.counter_scan_height});
        capturer.Initialize(maxW, maxH);
    }

    inline bool IsKeyDown(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }
    void PressKey(BYTE key, bool down) { keybd_event(key, 0, down ? 0 : KEYEVENTF_KEYUP, 0); }

    void JustClick() {
        PostMessageA(hTargetWindow, WM_LBUTTONDOWN, MK_LBUTTON, 0);
    }

    void PostClickDelay(bool is_boost) {
        std::uniform_int_distribution<int> hold_dist(cfg.click_hold_time_min, cfg.click_hold_time_max);
        Sleep(is_boost ? (hold_dist(rng) / 4) : hold_dist(rng));
        if (!IsKeyDown(VK_LBUTTON))PostMessageA(hTargetWindow, WM_LBUTTONUP, 0, 0);
        std::uniform_int_distribution<int> tap_dist(cfg.tap_time_min, cfg.tap_time_max);
        Sleep(is_boost ? (tap_dist(rng) / 4) : tap_dist(rng));
    }

    void Run() {
        hTargetWindow = FindWindowA(NULL, "VALORANT  ");
        if (!hTargetWindow) hTargetWindow = FindWindowA(NULL, "VALORANT");
        if (cfg.hide_console)
        {
            ShowWindow(GetConsoleWindow(), SW_HIDE);
            FreeConsole();
        }

        auto last_wasd_time = std::chrono::steady_clock::now();
        bool was_moving = false;

        while (is_running) {
            if (IsKeyDown(VK_END)) break;

            // Nếu người dùng đang tự bắn thì bot nghỉ
            if (IsKeyDown(VK_LBUTTON)) {
                Sleep(10); continue;
            }

            // Kiểm tra trạng thái nhấn giữ các phím chức năng
            bool is_countering = IsKeyDown(cfg.counter_key); // Nhấn giữ để kích hoạt mode counter strafe
            bool is_boost = IsKeyDown(cfg.boost_sens_key);

            // Logic chặn bắn khi di chuyển:
            // Chỉ chặn nếu không nhấn Boost VÀ không nhấn giữ phím Counter Strafe
            // (Nếu đang giữ phím Counter, ta cho phép quét màu để thực hiện logic dừng-bắn)
            if (!is_boost && !is_countering) {
                if (IsKeyDown('W') || IsKeyDown('A') || IsKeyDown('S') || IsKeyDown('D')) {
                    last_wasd_time = std::chrono::steady_clock::now();
                    was_moving = true;
                    Sleep(1); continue;
                }
                if (was_moving) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_wasd_time).count();
                    if (elapsed < 100) { Sleep(1); continue; }
                    was_moving = false;
                }
            }

            // Chọn kích thước quét màn hình
            int cur_w, cur_h;
            if (is_countering) {
                cur_w = cfg.counter_scan_width; cur_h = cfg.counter_scan_height;
            } else if (is_boost) {
                cur_w = cfg.boost_scan_width; cur_h = cfg.boost_scan_height;
            } else {
                cur_w = cfg.scan_width; cur_h = cfg.scan_height;
            }

            const RGBQUAD* pixels = capturer.Capture(cur_w, cur_h);
            if (pixels) {
                int rTol = is_boost ? cfg.red_tolerance + 20 : cfg.red_tolerance;
                bool found = false;
                int max_dim = std::max({cfg.scan_width, cfg.boost_scan_width, cfg.counter_scan_width});

                for (int y = 0; y < cur_h; ++y) {
                    for (int x = 0; x < cur_w; ++x) {
                        const RGBQUAD& p = pixels[y * max_dim + x];
                        if (p.rgbRed >= cfg.red - rTol && p.rgbRed <= cfg.red + rTol &&
                            p.rgbGreen >= cfg.green - cfg.green_tolerance && p.rgbGreen <= cfg.green + cfg.green_tolerance &&
                            p.rgbBlue >= cfg.blue - cfg.blue_tolerance && p.rgbBlue <= cfg.blue + cfg.blue_tolerance) {
                            found = true; break;
                        }
                    }
                    if (found) break;
                }

                if (found) {
                    if (is_countering) {
                        // Logic Counter Strafe: Nhấn phím ngược hướng di chuyển
                        bool uA = IsKeyDown('A'), uD = IsKeyDown('D'), uW = IsKeyDown('W'), uS = IsKeyDown('S');
                        
                        // Nhấn phím đối diện
                        if (uA && !uD) PressKey('D', true);
                        if (uD && !uA) PressKey('A', true);
                        if (uW && !uS) PressKey('S', true);
                        if (uS && !uW) PressKey('W', true);

                        // Chỉ delay nếu thực sự đang di chuyển
                        if (uA || uD || uW || uS) Sleep(cfg.stop_ms);

                        JustClick();

                        // Nhả phím đối diện
                        if (uA && !uD) PressKey('D', false);
                        if (uD && !uA) PressKey('A', false);
                        if (uW && !uS) PressKey('S', false);
                        if (uS && !uW) PressKey('W', false);

                        // Không reset biến is_countering vì đang dùng IsKeyDown kiểm tra trực tiếp
                        PostClickDelay(false);
                    } 
                    else {
                        // Chế độ thường hoặc boost
                        JustClick();
                        PostClickDelay(is_boost);
                    }
                }
            }
            Sleep(1);
        }
    }
};

int main() {
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    TriggerBot bot;
    bot.Run();
    return 0;
}