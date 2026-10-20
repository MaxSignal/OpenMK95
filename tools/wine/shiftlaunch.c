// Starts a program while the Shift key is held (to select the runtime's windowed mode).
#include <windows.h>
int main(int argc, char** argv) {
    if (argc < 2) return 1;
    INPUT in = {0};
    in.type = INPUT_KEYBOARD; in.ki.wVk = VK_SHIFT;
    SendInput(1, &in, sizeof in);
    STARTUPINFOA si = {sizeof si}; PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, argv[1], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 2;
    Sleep(4000);
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof in);
    return 0;
}
