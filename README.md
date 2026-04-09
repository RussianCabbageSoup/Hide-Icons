<h1>Hide-Icons</h1>
<h2>Program for automatic hiding of desktop icons during inactivity.</h2>

The program monitors mouse and keyboard activity using system‑level low‑level hooks. After 30 (configurable) seconds of inactivity, desktop icons are hidden (ShowWindow(SW_HIDE)); when activity resumes, they are shown again (SW_SHOW).

**Main features**
- Global tracking: works across all applications
- Background mode: console window is hidden

**Technical details**
- C++ WinAPI: WH_MOUSE_LL, WH_KEYBOARD_LL hooks

To enable automatic startup, create a new task in Task Scheduler and specify the path to the .exe file.
