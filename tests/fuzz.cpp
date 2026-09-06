#include <windows.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cache/DiskThumbnailCache.h"
#include "decode/RawHelperProtocol.h"
#include "util/PathUtils.h"

namespace fs = std::filesystem;

namespace
{
    constexpr std::uint32_t kRawHelperMagic = 0x52425748;

#pragma pack(push, 1)
    struct RawHelperFileHeader
    {
        std::uint32_t magic{};
        std::uint32_t version{};
        std::uint32_t bitmapWidth{};
        std::uint32_t bitmapHeight{};
        std::uint32_t sourceWidth{};
        std::uint32_t sourceHeight{};
        std::uint64_t pixelBytes{};
    };

    struct DiskThumbnailHeader
    {
        char magic[8]{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t sourceWidth{};
        std::uint32_t sourceHeight{};
        std::uint64_t pixelBytes{};
    };
#pragma pack(pop)

    class TempFolder
    {
    public:
        explicit TempFolder(const wchar_t* name)
            : root_(fs::temp_directory_path() / name)
        {
            std::error_code error;
            fs::remove_all(root_, error);
            fs::create_directories(root_);
        }

        ~TempFolder()
        {
            std::error_code error;
            fs::remove_all(root_, error);
        }

        const fs::path& Root() const noexcept
        {
            return root_;
        }

    private:
        fs::path root_;
    };

    void WriteBytes(const fs::path& path, const std::vector<unsigned char>& bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            throw std::runtime_error("failed to create fuzz input");
        }

        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream)
        {
            throw std::runtime_error("failed to write fuzz input");
        }
    }

    template <typename Header>
    std::vector<unsigned char> HeaderBytes(const Header& header)
    {
        const auto* begin = reinterpret_cast<const unsigned char*>(&header);
        return std::vector<unsigned char>(begin, begin + sizeof(header));
    }

    void Mutate(std::vector<unsigned char>* bytes, std::mt19937* generator)
    {
        if (!bytes || bytes->empty() || !generator)
        {
            return;
        }

        std::uniform_int_distribution<std::size_t> indexDistribution(0, bytes->size() - 1);
        std::uniform_int_distribution<int> mutationCountDistribution(1, 8);
        std::uniform_int_distribution<int> valueDistribution(0, 255);
        const int mutationCount = mutationCountDistribution(*generator);
        for (int mutation = 0; mutation < mutationCount; ++mutation)
        {
            (*bytes)[indexDistribution(*generator)] = static_cast<unsigned char>(valueDistribution(*generator));
        }
    }

    void RunRawHelperFuzz()
    {
        TempFolder root(L"HyperBrowseRawHelperBoundaryFuzz");
        const fs::path payloadPath = root.Root() / L"payload.bin";
        std::mt19937 generator(0x52415748);

        for (int iteration = 0; iteration < 512; ++iteration)
        {
            std::vector<unsigned char> bytes;
            if (iteration % 4 == 0)
            {
                bytes.resize(static_cast<std::size_t>(iteration % 513));
            }
            else
            {
                RawHelperFileHeader header{
                    kRawHelperMagic,
                    1,
                    4,
                    4,
                    4,
                    4,
                    64,
                };
                bytes = HeaderBytes(header);
                bytes.resize(sizeof(header) + 64, 0x7f);
            }

            Mutate(&bytes, &generator);
            WriteBytes(payloadPath, bytes);

            hyperbrowse::decode::RawHelperDecodedPixels decoded;
            std::wstring errorMessage;
            hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &decoded, &errorMessage);
        }
    }

    void RunPersistentCacheFuzz()
    {
        TempFolder root(L"HyperBrowsePersistentCacheBoundaryFuzz");
        const fs::path cacheRoot = root.Root() / L"cache";
        const fs::path imagePath = root.Root() / L"sample.jpg";
        const std::wstring normalizedImagePath = hyperbrowse::util::NormalizePathForComparison(imagePath.wstring());
        const hyperbrowse::cache::ThumbnailCacheKey key{
            imagePath.wstring(),
            7,
            4,
            4,
        };
        std::mt19937 generator(0x43414348);

        for (int iteration = 0; iteration < 512; ++iteration)
        {
            std::error_code error;
            fs::remove_all(cacheRoot, error);
            fs::create_directories(cacheRoot);

            DiskThumbnailHeader header{};
            std::memcpy(header.magic, "HBTHMB01", sizeof(header.magic));
            header.width = 4;
            header.height = 4;
            header.sourceWidth = 4;
            header.sourceHeight = 4;
            header.pixelBytes = 64;

            std::vector<unsigned char> headerBytes = HeaderBytes(header);
            Mutate(&headerBytes, &generator);
            std::vector<unsigned char> payload(64, 0x3f);
            headerBytes.insert(headerBytes.end(), payload.begin(), payload.end());
            WriteBytes(cacheRoot / L"0123456789abcdef.thumb", headerBytes);

            std::wofstream indexStream(cacheRoot / L"index.tsv", std::ios::trunc);
            indexStream << normalizedImagePath << L'\t'
                        << key.modifiedTimestampUtc << L'\t'
                        << key.targetWidth << L'\t'
                        << key.targetHeight << L"\t0123456789abcdef.thumb\t"
                        << headerBytes.size() << L"\t1\n";
            indexStream.close();

            if (iteration % 3 == 0)
            {
                std::wofstream malformedIndexStream(cacheRoot / L"index.tsv", std::ios::app);
                malformedIndexStream << L"malformed\t"
                                     << (iteration % 2 == 0 ? L"not-a-number" : L"18446744073709551616")
                                     << L"\t0\t-1\t..\\outside.thumb\t0\t0\n";
            }

            hyperbrowse::cache::DiskThumbnailCache cache(4ULL * 1024ULL * 1024ULL, cacheRoot.wstring());
            cache.QueryStatistics();
            cache.TryLoad(key);
        }
    }
}

int main(int argc, char* argv[])
{
    try
    {
        const std::string_view scenario = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
        if (scenario == "--persistent-cache")
        {
            RunPersistentCacheFuzz();
        }
        else if (scenario == "--raw-helper")
        {
            RunRawHelperFuzz();
        }
        else
        {
            RunPersistentCacheFuzz();
            RunRawHelperFuzz();
        }
        return 0;
    }
    catch (const std::exception& exception)
    {
        OutputDebugStringA(exception.what());
        return 1;
    }
}
