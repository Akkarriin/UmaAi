#include "utils.h"
#ifdef _WIN32
#include "windows.h"
#endif
#include <cstdlib>
#include <iostream>
using json = nlohmann::json;
using namespace std;

// https://www.codersrc.com/archives/15399.html
#ifdef _WIN32
std::string string_To_UTF8(const std::string& str)
{
    int nwLen = ::MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);

    wchar_t* pwBuf = new wchar_t[nwLen + 1];//一定要加1，不然会出现尾巴
    ZeroMemory(pwBuf, nwLen * 2 + 2);

    ::MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.length(), pwBuf, nwLen);

    int nLen = ::WideCharToMultiByte(CP_UTF8, 0, pwBuf, -1, NULL, NULL, NULL, NULL);

    char* pBuf = new char[nLen + 1];
    ZeroMemory(pBuf, nLen + 1);

    ::WideCharToMultiByte(CP_UTF8, 0, pwBuf, nwLen, pBuf, nLen, NULL, NULL);

    std::string retStr(pBuf);

    delete[]pwBuf;
    delete[]pBuf;

    pwBuf = NULL;
    pBuf = NULL;

    return retStr;
}

std::string UTF8_To_string(const std::string& str)
{
    int nwLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);

    wchar_t* pwBuf = new wchar_t[nwLen + 1];//一定要加1，不然会出现尾巴
    memset(pwBuf, 0, nwLen * 2 + 2);

    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), pwBuf, nwLen);

    int nLen = WideCharToMultiByte(CP_ACP, 0, pwBuf, -1, NULL, NULL, NULL, NULL);

    char* pBuf = new char[nLen + 1];
    memset(pBuf, 0, nLen + 1);

    WideCharToMultiByte(CP_ACP, 0, pwBuf, nwLen, pBuf, nLen, NULL, NULL);

    std::string retStr = pBuf;

    delete[]pBuf;
    delete[]pwBuf;

    pBuf = NULL;
    pwBuf = NULL;

    return retStr;
}

// 计算UTF8字符(Rune)数
#else
// Linux下控制台与文件均为UTF-8，无需转码
std::string string_To_UTF8(const std::string& str) { return str; }
std::string UTF8_To_string(const std::string& str) { return str; }
#endif

int UTF8_rune_count(const std::string& utf8String)
{
    int runeCount = 0;
    for (size_t i = 0; i < utf8String.length(); ++i)
    {
        unsigned char byte = utf8String[i];
        if (byte >= 0xF0) // 4字节字符
            i += 3;
        else if (byte >= 0xE0) // 3字节字符
            i += 2;
        else if (byte >= 0xC0) // 2字节字符
            i += 1;
        // 0x00到0x7F是1字节字符，‌不需要额外操作
        ++runeCount;
    }
    return runeCount;
}

// 计算UTF8字符(Rune)数
std::string UTF8_rune_cut(const std::string& utf8String, int n)
{
    int runeCount = 0;
    int i = 0;
    while (i < utf8String.length() && runeCount < n) {
        unsigned char byte = utf8String[i];
        if (byte >= 0xF0) // 4字节字符
            i += 3;
        else if (byte >= 0xE0) // 3字节字符
            i += 2;
        else if (byte >= 0xC0) // 2字节字符
            i += 1;
        // 0x00到0x7F是1字节字符，‌不需要额外操作
        ++runeCount;
        ++i;
    }
    return utf8String.substr(0, i);
}

void pauseConsole()
{
#ifdef _WIN32
  system("pause");
#else
  std::cout << "按回车键继续..." << std::endl;
  std::cin.get();
#endif
}
