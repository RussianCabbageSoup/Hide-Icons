#include <windows.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <comdef.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

HHOOK g_hMouseHook = NULL;
HHOOK g_hKeyboardHook = NULL;
std::atomic<bool> g_active(false);
std::atomic<bool> g_running(true);
std::chrono::steady_clock::time_point g_lastActivityTime;
HWND g_hDesktopIcons = NULL;
constexpr int time_out = 14;

// Интерфейс для управления громкостью
IAudioEndpointVolume* g_pEndpointVolume = NULL;
float g_originalVolume = 0.5f;
const float g_inactiveVolume = 1.0f;
bool g_volumeIncreased = false;

// Флаг, активна ли программа (работает только на рабочем столе)
std::atomic<bool> g_programActive(false);

// Функция для инициализации аудио интерфейса
bool InitAudioInterface() {
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) return false;

    IMMDeviceEnumerator* pEnumerator = NULL;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) return false;

    IMMDevice* pDevice = NULL;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnumerator->Release();
    if (FAILED(hr)) return false;

    hr = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
        (void**)&g_pEndpointVolume);
    pDevice->Release();

    return SUCCEEDED(hr);
}

float GetCurrentVolume() {
    if (!g_pEndpointVolume) return 0.5f;
    float volume;
    HRESULT hr = g_pEndpointVolume->GetMasterVolumeLevelScalar(&volume);
    if (FAILED(hr)) return 0.5f;
    return volume;
}

bool SetSystemVolume(float volume) {
    if (!g_pEndpointVolume) return false;
    volume = max(0.0f, min(1.0f, volume));
    HRESULT hr = g_pEndpointVolume->SetMasterVolumeLevelScalar(volume, NULL);
    return SUCCEEDED(hr);
}

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

// Функция проверки, активен ли рабочий стол
bool IsDesktopActive() {
    HWND hForeground = GetForegroundWindow();
    if (!hForeground) return false;

    // Получаем имя класса активного окна
    wchar_t className[256];
    GetClassName(hForeground, className, 256);

    // Проверяем, является ли активное окно рабочим столом или проводником
    // Progman - рабочий стол
    // WorkerW - также часть рабочего стола (на некоторых версиях Windows)
    // ExploreWClass - проводник Windows
    if (wcscmp(className, L"Progman") == 0 ||
        wcscmp(className, L"WorkerW") == 0 ||
        wcscmp(className, L"ExploreWClass") == 0) {
        return true;
    }

    // Дополнительная проверка: может ли окно быть дочерним рабочего стола
    HWND hDesktop = GetDesktopWindow();
    if (hForeground == hDesktop) return true;

    return false;
}

// Хуки для отслеживания активности
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_programActive) {
        g_active = true;
        g_lastActivityTime = std::chrono::steady_clock::now();
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_programActive) {
        g_active = true;
        g_lastActivityTime = std::chrono::steady_clock::now();
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// Отдельный поток для отслеживания активного окна
void MonitorForegroundWindow() {
    bool lastState = false;

    while (g_running) {
        bool isDesktop = IsDesktopActive();

        if (isDesktop != lastState) {
            if (isDesktop) {
                // Переключились на рабочий стол - активируем программу
                g_programActive = true;
                g_active = true; // Сбрасываем таймер бездействия
                g_lastActivityTime = std::chrono::steady_clock::now();
                std::cout << "[*] Рабочий стол активен. Программа включена." << std::endl;

                // Показываем иконки и восстанавливаем громкость если нужно
                if (g_hDesktopIcons) {
                    ShowWindow(g_hDesktopIcons, SW_SHOW);
                }
                if (g_volumeIncreased) {
                    SetSystemVolume(g_originalVolume);
                    g_volumeIncreased = false;
                }
            }
            else {
                // Переключились на другое окно - выключаем программу
                if (g_programActive) {
                    g_programActive = false;
                    std::cout << "[*] Другое окно активно. Программа выключена." << std::endl;

                    // Восстанавливаем всё как было
                    if (g_hDesktopIcons) {
                        ShowWindow(g_hDesktopIcons, SW_SHOW);
                    }
                    if (g_volumeIncreased) {
                        SetSystemVolume(g_originalVolume);
                        g_volumeIncreased = false;
                    }
                }
            }
            lastState = isDesktop;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void MonitorInactivity() {
    bool iconsVisible = true;

    // Сохраняем начальную громкость при запуске
    g_originalVolume = GetCurrentVolume();
    std::cout << "[*] Начальная громкость: " << (g_originalVolume * 100) << "%" << std::endl;

    while (g_running) {
        if (g_programActive) {
            if (g_active) {
                // Была активность на рабочем столе
                if (!iconsVisible) {
                    ShowWindow(g_hDesktopIcons, SW_SHOW);
                    iconsVisible = true;
                    std::cout << "[" << std::time(nullptr) << "] Иконки показаны" << std::endl;
                }

                if (g_volumeIncreased) {
                    SetSystemVolume(g_originalVolume);
                    g_volumeIncreased = false;
                    std::cout << "[" << std::time(nullptr) << "] Громкость восстановлена: "
                        << (g_originalVolume * 100) << "%" << std::endl;
                }

                g_active = false;
            }
            else {
                // Проверяем бездействие на рабочем столе
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastActivityTime).count();

                if (elapsed >= time_out) {
                    if (iconsVisible) {
                        ShowWindow(g_hDesktopIcons, SW_HIDE);
                        iconsVisible = false;
                        std::cout << "[" << std::time(nullptr) << "] Иконки скрыты" << std::endl;
                    }

                    if (!g_volumeIncreased) {
                        g_originalVolume = GetCurrentVolume();
                        SetSystemVolume(g_inactiveVolume);
                        g_volumeIncreased = true;
                        std::cout << "[" << std::time(nullptr) << "] Громкость увеличена до: "
                            << (g_inactiveVolume * 100) << "%" << std::endl;
                    }
                }
            }
        }
        else {
            // Программа неактивна - убеждаемся что всё восстановлено
            if (!iconsVisible) {
                // Этот случай не должен произойти, но на всякий случай
                iconsVisible = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main() {
    setlocale(LC_ALL, "Ru");

    // Инициализируем аудио интерфейс
    if (!InitAudioInterface()) {
        std::cerr << "Предупреждение: Не удалось инициализировать аудио интерфейс" << std::endl;
        std::cerr << "Управление громкостью будет недоступно" << std::endl;
    }

    // СКРЫВАЕМ КОНСОЛЬ И ОКНО
    HWND hConsole = GetConsoleWindow();
    ShowWindow(hConsole, SW_HIDE);

    // Убрать из Alt+Tab и Taskbar
    HWND hWnd = GetForegroundWindow();
    if (hWnd) {
        SetWindowLong(hWnd, GWL_EXSTYLE,
            GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
        ShowWindow(hWnd, SW_HIDE);
    }

    g_hDesktopIcons = GetDesktopIconsWindow();
    if (!g_hDesktopIcons) {
        std::cerr << "Ошибка: Не удалось найти иконки рабочего стола" << std::endl;
        if (g_pEndpointVolume) g_pEndpointVolume->Release();
        CoUninitialize();
        return 1;
    }

    g_lastActivityTime = std::chrono::steady_clock::now();

    // Устанавливаем хуки
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(NULL), 0);
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);

    if (!g_hMouseHook || !g_hKeyboardHook) {
        std::cerr << "Ошибка установки хуков" << std::endl;
        if (g_pEndpointVolume) g_pEndpointVolume->Release();
        CoUninitialize();
        return 1;
    }

    // Запускаем потоки
    std::thread monitorThread(MonitorInactivity);
    std::thread foregroundThread(MonitorForegroundWindow);

    // Цикл сообщений для хуков
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && g_running) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running = false;
    if (monitorThread.joinable()) monitorThread.join();
    if (foregroundThread.joinable()) foregroundThread.join();

    // Восстанавливаем громкость при выходе
    if (g_pEndpointVolume && g_volumeIncreased) {
        SetSystemVolume(g_originalVolume);
    }

    // Показываем иконки при выходе
    if (g_hDesktopIcons) {
        ShowWindow(g_hDesktopIcons, SW_SHOW);
    }

    // Очистка
    UnhookWindowsHookEx(g_hMouseHook);
    UnhookWindowsHookEx(g_hKeyboardHook);

    if (g_pEndpointVolume) {
        g_pEndpointVolume->Release();
        CoUninitialize();
    }

    return 0;
}
