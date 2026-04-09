#include <windows.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

HHOOK g_hMouseHook = NULL;
HHOOK g_hKeyboardHook = NULL;
std::atomic<bool> g_active(false);
std::atomic<bool> g_running(true);
std::chrono::steady_clock::time_point g_lastActivityTime;
HWND g_hDesktopIcons = NULL;

// Функция для получения окна иконок
HWND GetDesktopIconsWindow() {
    HWND hWnd = FindWindow(L"Progman", L"Program Manager");
    if (hWnd == NULL) return NULL;

    HWND hShellViewWin = FindWindowEx(hWnd, NULL, L"SHELLDLL_DefView", NULL);
    if (hShellViewWin == NULL) {
        hShellViewWin = FindWindowEx(NULL, NULL, L"Progman", L"Program Manager");
        if (hShellViewWin) {
            hShellViewWin = FindWindowEx(hShellViewWin, NULL, L"SHELLDLL_DefView", NULL);
            if (hShellViewWin) {
                hShellViewWin = FindWindowEx(hShellViewWin, NULL, L"SysListView32", NULL);
            }
        }
    }
    else {
        hShellViewWin = FindWindowEx(hShellViewWin, NULL, L"SysListView32", NULL);
    }
    return hShellViewWin;
}

// Хуки для отслеживания активности
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        g_active = true;
        g_lastActivityTime = std::chrono::steady_clock::now();
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        g_active = true;
        g_lastActivityTime = std::chrono::steady_clock::now();
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

void MonitorInactivity() {
    bool iconsVisible = true;

    while (g_running) {
        if (g_active) {
            // Была активность
            if (!iconsVisible) {
                ShowWindow(g_hDesktopIcons, SW_SHOW);
                iconsVisible = true;
                std::cout << "[" << std::time(nullptr) << "] Иконки показаны" << std::endl;
            }
            g_active = false;
        }
        else {
            // Проверяем бездействие
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastActivityTime).count();

            if (elapsed >= 60 && iconsVisible) {
                ShowWindow(g_hDesktopIcons, SW_HIDE);
                iconsVisible = false;
                std::cout << "[" << std::time(nullptr) << "] Иконки скрыты (бездействие 60 сек)" << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main() {
    setlocale(LC_ALL, "Ru");

    // СКРЫВАЕМ КОНСОЛЬ И ОКНО
    HWND hConsole = GetConsoleWindow();
    ShowWindow(hConsole, SW_HIDE);  // Скрыть консоль

    // Убрать из Alt+Tab и Taskbar (для оконного режима)
    HWND hWnd = GetForegroundWindow();
    if (hWnd) {
        SetWindowLong(hWnd, GWL_EXSTYLE,
            GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
        ShowWindow(hWnd, SW_HIDE);
    }

    g_hDesktopIcons = GetDesktopIconsWindow();
    if (!g_hDesktopIcons) {
        std::cerr << "Ошибка: Не удалось найти иконки рабочего стола" << std::endl;
        return 1;
    }

    g_lastActivityTime = std::chrono::steady_clock::now();

    // Устанавливаем хуки
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(NULL), 0);
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);

    if (!g_hMouseHook || !g_hKeyboardHook) {
        std::cerr << "Ошибка установки хуков" << std::endl;
        return 1;
    }

    // Запускаем мониторинг в отдельном потоке
    std::thread monitorThread(MonitorInactivity);

    // Цикл сообщений для хуков
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && g_running) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running = false;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    // Очистка
    UnhookWindowsHookEx(g_hMouseHook);
    UnhookWindowsHookEx(g_hKeyboardHook);

    return 0;
}