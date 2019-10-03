#define _CRT_SECURE_NO_WARNINGS (1)
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <array>
#include <unordered_map>

#pragma pack(push)
#pragma pack(1)

struct DDS_PIXELFORMAT
{
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DDS_HEADER
{
    uint32_t magic;
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};


struct BlockDxt1
{
    uint16_t endPointA; // RGB565 endpoint A
    uint16_t endPointB; // RGB565 endpoint B
    uint32_t indices;   // 4x4 block of 2 bit indices 
};


struct TGA_HEADER
{
    uint8_t idLength;
    uint8_t colourMapType;
    uint8_t imageType;
    uint16_t firstEntry;
    uint16_t numEntries;
    uint8_t bitsPerEntry;
    uint16_t xOrigin;
    uint16_t yOrigin;
    uint16_t width;
    uint16_t height;
    uint8_t bitsPerPixel;
    uint8_t descriptor;
};

#pragma pack(pop)


struct RGB
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
};


static RGB unpackColor565(uint16_t color)
{
    uint32_t b = (color & 0x1f) << 3;
    uint32_t g = ((color >> 5) & 0x3f) << 2;
    uint32_t r = ((color >> 11) & 0x1f) << 3;

    if (r > 255)
        r = 255;

    if (g > 255)
        g = 255;

    if (b > 255)
        b = 255;

    RGB res;
    res.r = r;
    res.g = g;
    res.b = b;
    return res;
}


static void saveToTga(const char* fileName, const std::vector<RGB>& pixels, uint32_t width, uint32_t height)
{
    TGA_HEADER header;
    memset(&header, 0, sizeof(TGA_HEADER));

    header.imageType = 2;
    header.width = width;
    header.height = height;
    header.bitsPerPixel = 24;
    header.descriptor = 0x20; // set proper image orientation

    FILE* file = fopen(fileName, "wb");

    fwrite(&header, 1, sizeof(header), file);
    fwrite(&pixels[0], 1, pixels.size() * sizeof(RGB), file);

    fclose(file);
}

static void splitAndSave(const std::vector<uint8_t>& mipData, uint32_t width, uint32_t height, const char* colorNameA, const char* colorNameB, const char* indicesName)
{
    uint32_t blockCountW = width / 4;
    uint32_t blockCountH = height / 4;

    uint32_t blockLineStride = blockCountW * sizeof(BlockDxt1);

    uint32_t blockCount = blockCountW * blockCountH;

    std::vector<RGB> endPointsA(blockCount);
    std::vector<RGB> endPointsB(blockCount);

    std::vector<RGB> indices(width * height);

    RGB indiceColor[4];
    indiceColor[0] = { 0x00, 0x40, 0x40 };
    indiceColor[1] = { 0x55, 0x80, 0x80 };
    indiceColor[2] = { 0xAA, 0xC0, 0xC0 };
    indiceColor[3] = { 0xFF, 0xFF, 0xFF };

    std::unordered_map<uint16_t, uint32_t> uniqueColors;
    std::unordered_map<uint32_t, uint32_t> uniqueIndices;

    for (uint32_t y = 0; y < blockCountH; y++)
    {
        for (uint32_t x = 0; x < blockCountW; x++)
        {
            uint32_t blockOffset = (y * blockLineStride) + (x * sizeof(BlockDxt1));

            BlockDxt1 block;
            memcpy(&block, &mipData[blockOffset], sizeof(BlockDxt1));

            // decode end-points
            uint32_t offset = y * blockCountW + x;
            endPointsA[offset] = unpackColor565(block.endPointA);
            endPointsB[offset] = unpackColor565(block.endPointB);

            // decode block indices
            uint32_t ind = block.indices;
            for (uint32_t oy = 0; oy < 4; oy++)
            {
                for (uint32_t ox = 0; ox < 4; ox++)
                {
                    uint32_t currentIndice = (ind & 0x3);
                    uint32_t indiceOffset = (y * 4 + oy) * width + (x * 4 + ox);
                    indices[indiceOffset] = indiceColor[currentIndice];

                    // colorize blocks
                    if (((x + y) % 2) == 0)
                    {
                        indices[indiceOffset].r = 0;
                    }
                    else
                    {
                        indices[indiceOffset].g = 0;
                    }

                    ind = ind >> 2;
                }
            }

            // counting for unique colors/indices
            {
                auto it = uniqueColors.find(block.endPointA);
                if (it != uniqueColors.end())
                {
                    it->second++;
                }
                else
                {
                    uniqueColors[block.endPointA] = 1;
                }
            }

            {
                auto it = uniqueColors.find(block.endPointB);
                if (it != uniqueColors.end())
                {
                    it->second++;
                }
                else
                {
                    uniqueColors[block.endPointB] = 1;
                }
            }

            {
                auto it = uniqueIndices.find(block.indices);
                if (it != uniqueIndices.end())
                {
                    it->second++;
                }
                else
                {
                    uniqueIndices[block.indices] = 1;
                }
            }
        }
    }

    uint32_t numbBytesForEndPoints = blockCountW * blockCountH * 2;
    uint32_t numbBytesForIndices = blockCountW * blockCountH * 4;

    printf("\n");
    printf("End-points-A data size = %d bytes (25%%)\n", numbBytesForEndPoints);
    printf("End-points-B data size = %d bytes (25%%)\n", numbBytesForEndPoints);
    printf("Indices data size = %d bytes (50%%)\n", numbBytesForIndices);
    printf("Number of blocks: %d\n", blockCount);
    uint32_t sharedColorBlocks = blockCount - (uint32_t)uniqueColors.size();
    printf("Shared color blocks: %d (%3.2f%%)\n", sharedColorBlocks, 100.0f * sharedColorBlocks / (float)blockCount);
    uint32_t sharedIndicesBlocks = blockCount - (uint32_t)uniqueIndices.size();
    printf("Shared indices blocks: %d (%3.2f%%)\n", sharedIndicesBlocks, 100.0f * sharedIndicesBlocks / (float)blockCount);
    printf("\n");

    printf("Saving end-points-A to '%s'\n", colorNameA);
    saveToTga(colorNameA, endPointsA, blockCountW, blockCountH);

    printf("Saving end-points-B to '%s'\n", colorNameB);
    saveToTga(colorNameB, endPointsB, blockCountW, blockCountH);

    printf("Saving indices to '%s'\n", indicesName);
    saveToTga(indicesName, indices, width, height);
}



int main()
{
    FILE* file = fopen("../data/dx1_test.dds", "rb");
    if (!file)
    {
        printf("Can't open file\n");
        return -1;
    }

    DDS_HEADER header;
    size_t bytesReaded = fread(&header, 1, sizeof(DDS_HEADER), file);
    if (bytesReaded != sizeof(DDS_HEADER))
    {
        printf("File is too small. Not a DDS file?\n");
        return -1;
    }

    if (header.magic != 0x20534444)
    {
        printf("DDS magic header missing. Not a DDS file?\n");
        return -1;
    }

    if (header.size != (sizeof(DDS_HEADER) - sizeof(uint32_t)))
    {
        printf("Invalid/incompatible header struct size\n");
        return -1;
    }

    printf("Width: %d\n", header.width);
    printf("Height: %d\n", header.height);
    printf("MipCount: %d\n", header.mipMapCount);
    printf("\n");

    if (header.ddspf.size != sizeof(DDS_PIXELFORMAT))
    {
        printf("Invalid/incompatible pixel format struct size\n");
        return -1;
    }

    if (((header.ddspf.flags & 0x4) == 0) || (header.ddspf.fourCC != 0x31545844))
    {
        printf("Can't load non DXT1 texture\n");
        return -1;
    }

    if ((header.width % 4) != 0)
    {
        printf("Width should be multiple of 4\n");
        return -1;
    }

    if ((header.height % 4) != 0)
    {
        printf("Height should be multiple of 4\n");
        return -1;
    }

    if (header.depth != 1)
    {
        printf("Can't read volume texture.\n");
        return -1;
    }

    std::vector<std::vector<uint8_t>> data;
    for (uint32_t mip = 0; mip < header.mipMapCount; mip++)
    {
        uint32_t width = (header.width >> mip);
        uint32_t height = (header.height >> mip);

        if (width < 1)
            width = 1;

        if (height < 1)
            height = 1;

        uint32_t blockW = width / 4;
        uint32_t blockH = height / 4;

        if (blockW < 1)
            blockW = 1;

        if (blockH < 1)
            blockH = 1;

        data.emplace_back();

        uint32_t mipSizeInBytes = blockW * blockH * sizeof(BlockDxt1);
        printf("mip#%d, %dx%d [%dx%d], size %d bytes\n", mip, width, height, blockW, blockH, mipSizeInBytes);

        std::vector<uint8_t>& mipData = data.back();
        mipData.resize(mipSizeInBytes);

        size_t bytesReaded = fread(&mipData[0], 1, mipSizeInBytes, file);
        if (bytesReaded != mipSizeInBytes)
        {
            printf("Can't read data for mip#%d\n", mip);
            return -1;
        }
    }

    fclose(file);

    splitAndSave(data[0], header.width, header.height, "mip0_colorA.tga", "mip0_colorB.tga", "mip0_indices.tga");
    //splitAndSave(data[1], (header.width >> 1), (header.height >> 1), "mip1_colorA.tga", "mip1_colorB.tga", "mip1_indices.tga");
    //splitAndSave(data[2], (header.width >> 2), (header.height >> 2), "mip2_colorA.tga", "mip2_colorB.tga", "mip2_indices.tga");

    printf("\nDone.\n");

    return 0;
}

