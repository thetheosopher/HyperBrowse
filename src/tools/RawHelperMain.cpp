#include "decode/ImageDecoder.h"

#include <iostream>
#include <string>
#include <string_view>

namespace
{
    void PrintUsage()
    {
        std::wcerr << L"Usage: HyperBrowseRawHelper --mode <thumbnail|full> --input <path> --output <path> [--width <pixels> --height <pixels>]\n";
    }

    bool ParseInteger(std::wstring_view value, int* parsedValue)
    {
        if (!parsedValue || value.empty())
        {
            return false;
        }

        try
        {
            std::size_t consumed = 0;
            const int parsed = std::stoi(std::wstring(value), &consumed, 10);
            if (consumed != value.size())
            {
                return false;
            }

            *parsedValue = parsed;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

int wmain(int argc, wchar_t* argv[])
{
#if !defined(HYPERBROWSE_ENABLE_LIBRAW)
    (void)argc;
    (void)argv;
    std::wcerr << L"This helper was built without LibRaw support.\n";
    return 3;
#else
    hyperbrowse::decode::LibRawHelperInvocation invocation;
    bool hasMode = false;
    bool hasInput = false;
    bool hasOutput = false;

    for (int index = 1; index < argc; ++index)
    {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--mode")
        {
            if (index + 1 >= argc)
            {
                PrintUsage();
                return 2;
            }

            const std::wstring_view modeValue(argv[++index]);
            if (modeValue == L"thumbnail")
            {
                invocation.mode = hyperbrowse::decode::LibRawHelperMode::Thumbnail;
            }
            else if (modeValue == L"full")
            {
                invocation.mode = hyperbrowse::decode::LibRawHelperMode::FullImage;
            }
            else
            {
                PrintUsage();
                return 2;
            }
            hasMode = true;
        }
        else if (argument == L"--input")
        {
            if (index + 1 >= argc)
            {
                PrintUsage();
                return 2;
            }

            invocation.filePath = argv[++index];
            hasInput = true;
        }
        else if (argument == L"--output")
        {
            if (index + 1 >= argc)
            {
                PrintUsage();
                return 2;
            }

            invocation.outputFilePath = argv[++index];
            hasOutput = true;
        }
        else if (argument == L"--width")
        {
            if (index + 1 >= argc || !ParseInteger(argv[++index], &invocation.targetWidth))
            {
                PrintUsage();
                return 2;
            }
        }
        else if (argument == L"--height")
        {
            if (index + 1 >= argc || !ParseInteger(argv[++index], &invocation.targetHeight))
            {
                PrintUsage();
                return 2;
            }
        }
        else
        {
            PrintUsage();
            return 2;
        }
    }

    if (!hasMode || !hasInput || !hasOutput)
    {
        PrintUsage();
        return 2;
    }

    if (invocation.mode == hyperbrowse::decode::LibRawHelperMode::Thumbnail
        && (invocation.targetWidth <= 0 || invocation.targetHeight <= 0))
    {
        PrintUsage();
        return 2;
    }

    std::wstring errorMessage;
    if (!hyperbrowse::decode::RunLibRawHelperInvocation(invocation, &errorMessage))
    {
        if (!errorMessage.empty())
        {
            std::wcerr << errorMessage << L'\n';
        }
        return 1;
    }

    return 0;
#endif
}