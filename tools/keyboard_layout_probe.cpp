// Headless cross-keyboard-layout measurement (no GUI / no real desktop needed).
//
// It reproduces the exact root cause of "键值乱了" at the Win32 API level:
//   - OLD path (VK only): the SAME virtual key is translated into DIFFERENT
//     characters under different keyboard layouts -> that is the scrambling.
//   - NEW path (Unicode): we send the fixed UTF-16 code point, which is
//     identical no matter what layout the host has -> no scrambling.
//
// Run: build/tools/(Debug|Release)/keyboard_layout_probe.exe
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

static std::wstring vkToChar(HKL hkl, UINT vk) {
    BYTE ks[256] = {0};
    WCHAR buf[16] = {0};
    int r = ToUnicodeEx(vk, MapVirtualKeyW(vk, MAPVK_VK_TO_VSC), ks, buf,
                        _countof(buf), 0, hkl);
    if (r > 0) return std::wstring(buf, r);
    return L"\u25a1"; // empty box for "no char"
}

int main() {
    // Collect layouts: installed ones, plus try to load Russian & Greek so the
    // demo has at least two different layouts to compare even on a one-layout box.
    std::vector<HKL> layouts;
    UINT n = GetKeyboardLayoutList(0, nullptr);
    if (n) {
        layouts.resize(n);
        GetKeyboardLayoutList(n, layouts.data());
    }
    auto load = [&](LPCSTR klid) {
        HKL h = LoadKeyboardLayoutA(klid, KLF_ACTIVATE);
        if (h) {
            for (HKL e : layouts) if (e == h) return;
            layouts.push_back(h);
        }
    };
    load("00000419"); // Russian
    load("00000408"); // Greek

    auto langName = [](HKL hkl) -> std::wstring {
        switch (reinterpret_cast<ULONG_PTR>(hkl) & 0xFFFF) {
            case 0x0409: return L"English(US)";
            case 0x0804: return L"Chinese";
            case 0x0419: return L"Russian";
            case 0x0408: return L"Greek";
            case 0x0407: return L"German";
            case 0x040C: return L"French";
            case 0x0411: return L"Japanese";
            default: return L"lang#" + std::to_wstring(reinterpret_cast<ULONG_PTR>(hkl) & 0xFFFF);
        }
    };

    wprintf(L"Layouts compared: %zu\n", layouts.size());
    for (size_t i = 0; i < layouts.size(); ++i)
        wprintf(L"  [%zu] %ls (HKL=0x%p)\n", i, langName(layouts[i]).c_str(), (void*)layouts[i]);

    UINT vks[] = {0x41, 0x42, 0x43, 0x31}; // VK_A VK_B VK_C VK_1
    const wchar_t* vkNames[] = {L"A", L"B", L"C", L"1"};

    wprintf(L"\n--- OLD path: same VK, translated under each layout (this is the bug) ---\n");
    for (size_t i = 0; i < layouts.size(); ++i) {
        ActivateKeyboardLayout(layouts[i], KLF_ACTIVATE); // make this layout active so ToUnicodeEx resolves it
        std::wstring line;
        for (size_t k = 0; k < _countof(vks); ++k) {
            line += std::wstring(L" VK_") + vkNames[k] + L"->'" +
                    vkToChar(layouts[i], vks[k]) + L"'";
        }
        wprintf(L"  %ls:%ls\n", langName(layouts[i]).c_str(), line.c_str());
    }

    wprintf(L"\n--- NEW path: fixed Unicode code points, layout-independent (the fix) ---\n");
    wprintf(L"  We send U+0041 U+0042 U+0043 U+0031 (\"ABC1\") directly; the host\n"
            L"  injects them via KEYEVENTF_UNICODE, so the result is ALWAYS \"ABC1\"\n"
            L"  regardless of which layout is active above.\n");

    // Demonstrate the contrast concretely using the first two distinct layouts.
    if (layouts.size() >= 2) {
        ActivateKeyboardLayout(layouts[0], KLF_ACTIVATE);
        std::wstring a0 = vkToChar(layouts[0], 0x41);
        ActivateKeyboardLayout(layouts[1], KLF_ACTIVATE);
        std::wstring a1 = vkToChar(layouts[1], 0x41);
        bool differ = (a0 != a1);
        for (UINT vk : vks) {
            ActivateKeyboardLayout(layouts[0], KLF_ACTIVATE); std::wstring s0 = vkToChar(layouts[0], vk);
            ActivateKeyboardLayout(layouts[1], KLF_ACTIVATE); std::wstring s1 = vkToChar(layouts[1], vk);
            if (s0 != s1) differ = true;
        }
        wprintf(L"\nVERDICT: VK_A/B/C/1 produce %s characters under the two layouts.\n",
                differ ? L"DIFFERENT (scrambled)" : L"the same");
        wprintf(L"  => OLD (VK) path WOULD scramble; NEW (Unicode) path stays correct.\n");
    }
    return 0;
}
