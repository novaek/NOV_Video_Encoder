#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <vfw.h>
#include <cstring>



#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")


int width  = GetSystemMetrics(SM_CXSCREEN);
int height = GetSystemMetrics(SM_CYSCREEN);

void capture_screen(std::vector<uint8_t>& rgb, int& width, int& height)
{
    HDC hScreen = GetDC(nullptr);
    HDC hDC = CreateCompatibleDC(hScreen);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

    SelectObject(hDC, bmp);
    BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY);

    rgb.resize(width * height * 3);
    memcpy(rgb.data(), bits, rgb.size());

    DeleteObject(bmp);
    DeleteDC(hDC);
    ReleaseDC(nullptr, hScreen);
}

void rgb24_to_rgb565(const std::vector<uint8_t>& src,
                     std::vector<uint16_t>& dst)
{
    size_t pixels = src.size() / 3;
    dst.resize(pixels);

    for (size_t i = 0; i < pixels; i++) {
        uint8_t b = src[i*3 + 0];
        uint8_t g = src[i*3 + 1];
        uint8_t r = src[i*3 + 2];

        dst[i] = ((r >> 3) << 11) |
                 ((g >> 2) << 5)  |
                 (b >> 3);
    }
}


void encode_pframe(const std::vector<uint16_t>& prev,
                   const std::vector<uint16_t>& curr,
                   std::vector<uint8_t>& out)
{
    out.clear();
    out.push_back(0x01); // P-frame

    uint16_t w = width;
    uint16_t h = height;
    out.push_back(w >> 8); out.push_back(w & 0xFF);
    out.push_back(h >> 8); out.push_back(h & 0xFF);

    size_t pixel_count = curr.size();
    size_t i = 0;

    std::vector<std::pair<uint16_t,std::vector<uint16_t>>> chunks;

    while (i < pixel_count) {
        uint16_t skip = 0;
        while (i < pixel_count && prev[i] == curr[i]) {
            skip++;
            i++;
        }

        std::vector<uint16_t> deltas;
        while (i < pixel_count && prev[i] != curr[i]) {
            deltas.push_back(prev[i] ^ curr[i]);
            i++;
        }

        if (!deltas.empty())
            chunks.push_back({skip, deltas});
    }

    uint16_t chunk_count = chunks.size();
    out.push_back(chunk_count >> 8);
    out.push_back(chunk_count & 0xFF);

    for (auto& c : chunks) {
        out.push_back(c.first >> 8);
        out.push_back(c.first & 0xFF);

        uint16_t n = c.second.size();
        out.push_back(n >> 8);
        out.push_back(n & 0xFF);

        for (uint16_t d : c.second) {
            out.push_back(d >> 8);
            out.push_back(d & 0xFF);
        }
    }
}

void encode_iframe(const std::vector<uint16_t>& frame,
                   std::vector<uint8_t>& out)
{
    out.clear();
    out.push_back(0x00); // I-frame

    out.push_back(width >> 8); out.push_back(width & 0xFF);
    out.push_back(height >> 8); out.push_back(height & 0xFF);

    for (uint16_t px : frame) {
        out.push_back(px >> 8);
        out.push_back(px & 0xFF);
    }
}
std::vector<uint16_t> prev, curr;
std::vector<uint8_t> screen_rgb, encoded;

int main(){
    uint8_t sep = 0x40;
    uint8_t header_be[2] = { 0xAD, 0xDA };
    uint8_t count_be[2]  = { 0x00, 0x00 };
    uint16_t frameCount = 0;

    std::ofstream file("KGB", std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open KGB for writing\n";
        return 1;
    }

    file.write(reinterpret_cast<char*>(header_be), 2);
    file.write(reinterpret_cast<char*>(count_be), 2);
    std::vector<uint8_t> screen_rgb, encoded;
    std::vector<uint16_t> prev, curr;
    file.write(reinterpret_cast<char*>(&sep), 1);
    for (int i=0;i<250;i++) {
        capture_screen(screen_rgb, width, height);
        rgb24_to_rgb565(screen_rgb, curr);

        if (prev.empty()) {
            encode_iframe(curr, encoded);
        } else {
            encode_pframe(prev, curr, encoded);
        }
        file.write(reinterpret_cast<char*>(encoded.data()), encoded.size());
        file.write(reinterpret_cast<char*>(&sep), 1);
        prev = curr;
        Sleep(1000 / 24);
        frameCount++;
    }
    std::cout << "number of frames recorded : " << frameCount << std::endl;
    uint8_t frameCount_be[2] = {
        uint8_t(frameCount >> 8),
        uint8_t(frameCount & 0xFF)
    };
    file.close();
    std::fstream file_out("KGB", std::ios::binary | std::ios::in | std::ios::out);
    file_out.seekp(2, std::ios::beg);
    file_out.write(reinterpret_cast<char*>(frameCount_be), 2);
    file_out.close();

}
