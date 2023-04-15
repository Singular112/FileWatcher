// stl
#include <iostream>

//
#include "file_watcher_c.h"

//

int main()
{
    setlocale(LC_ALL, "Russian");

    const wchar_t* dict[17] = { L"", L"ADDED", L"REMOVED", L"MODIFIED", L"RENAMED_OLD_NAME", L"RENAMED_NEW_NAME" };
	const WORD colors[] =
	{
		0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F,
		0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6
	};
	HANDLE hstdout = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hstdout, &csbi);

    file_watcher_c file_watcher;

    auto dir_wacther = file_watcher.register_directory
    (
        L"C:\\Users\\Singular\\Desktop\\trash",

        file_watcher_c::e_notify::CHANGE_CREATION |
        file_watcher_c::e_notify::CHANGE_FILE_NAME |
        file_watcher_c::e_notify::CHANGE_SIZE
    );

    dir_wacther->register_callback([&](std::wstring directory, std::wstring fname, file_watcher_c::e_action action)
    {
        wprintf_s(L"File ");
        
        SetConsoleTextAttribute(hstdout, colors[10]);
        
        wprintf_s(L"'%s'", fname.c_str());
        
        SetConsoleTextAttribute(hstdout, csbi.wAttributes);
        wprintf_s(L" modifyed(action - %s) in directory '%s'\n", dict[action], directory.c_str());
    });

    file_watcher.start_watching();

    while (true)
    {
        Sleep(100);
    }

    return 0;
}
