#define UNICODE
#define _UNICODE

#include <windows.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>


#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")


std::vector<uint16_t> framebuffer;
std::vector<uint8_t> rgb;


int width = 0;
int height = 0;


void rgb565_to_rgb24(const std::vector<uint16_t>& src, std::vector<uint8_t>& dst, int w, int h)
{
    dst.resize(w * h * 3);
    for (int i = 0; i < w * h; i++) {
        uint16_t p = src[i];
        dst[i*3 + 2] = ((p >> 11) & 0x1F) << 3; // R
        dst[i*3 + 1] = ((p >> 5) & 0x3F) << 2;  // G
        dst[i*3 + 0] = (p & 0x1F) << 3;         // B
    }
}

const uint32_t capW = GetSystemMetrics(SM_CXSCREEN);
const uint32_t capH = GetSystemMetrics(SM_CYSCREEN);

void render_frame(HWND hwnd, const std::vector<uint8_t>& rgb, int w, int h)
{
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(hwnd);
    StretchDIBits(hdc, 0, 0, w, h, 0, 0, w, h, rgb.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(hwnd, hdc);
}

uint16_t read_u16(const std::vector<uint8_t>& data, size_t& offset)
{
    uint16_t v = (data[offset] << 8) | data[offset + 1];
    offset += 2;
    return v;
}

void codec(const std::vector<uint8_t>& data,
           std::vector<uint16_t>& framebuffer,
           int width,
           int height,
           bool isKeyframe)
{
    size_t pixels = size_t(width) * size_t(height);
    size_t bytes  = pixels * 2;

    if (data.size() != bytes)
        return;

    if (isKeyframe) {
        framebuffer.resize(pixels);
        memcpy(framebuffer.data(), data.data(), bytes);
        return;
    }

    if (framebuffer.size() != pixels)
        return;

    for (size_t i = 0; i < bytes; i++) {
        ((uint8_t*)framebuffer.data())[i] ^= data[i];
    }
}


HWND create_window(int w, int h)
{
    WNDCLASS wc{};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"KGB_PLAYER";

    if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS);


    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"KGB Player",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        w, h,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_ICONERROR);
    }

    return hwnd;
}

std::vector<uint8_t> get_info(const std::string& name)
{
    std::vector<uint8_t> info;
    std::ifstream input(name, std::ios::binary);
    if (!input) return info;

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());

    if (bytes.size() < 4) return info;

    uint16_t header = (bytes[0] << 8) | bytes[1];
    info.push_back((header == 0xADDA) ? 0xFF : 0x00);
    info.push_back(bytes[2]);
    info.push_back(bytes[3]);

    return info;
}

std::vector<uint8_t> get_img_data(const std::string& filename, uint16_t imgIndex)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file) return {};

    uint8_t hdr[2];
    file.read((char*)hdr, 2);
    if (hdr[0] != 0xAD || hdr[1] != 0xDA)
        return {};

    uint8_t fc_be[2];
    file.read((char*)fc_be, 2);
    uint16_t frameCount = (fc_be[0] << 8) | fc_be[1];

    if (imgIndex >= frameCount)
        return {};

    for (uint16_t i = 0; i <= imgIndex; i++) {
        uint8_t size_be[4];
        file.read((char*)size_be, 4);

        uint32_t frameSize =(size_be[0] << 24) | (size_be[1]<<16) | (size_be[2]<<8) | size_be[3];

        if (i == imgIndex) {
            std::vector<uint8_t> frame(frameSize);
            file.read((char*)frame.data(), frameSize);
            return frame;
        }

        file.seekg(frameSize, std::ios::cur);
    }

    return {};
}


int main()
{   
    std::string mfs="";
    std::cin >> mfs;
    auto HS = get_info(mfs);
    if (HS.size() < 3) return 1;

    int totalFrames = (HS[1] << 8) | HS[2];
    if (totalFrames == 0) return 1;

    HWND hwnd = create_window(1280, 720);
    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    int frameIndex = 0;
    DWORD lastTick = GetTickCount();

    while (true) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                return 0;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD now = GetTickCount();
        if (now - lastTick >= 1000 / 24) {
            if (frameIndex >= totalFrames)
            frameIndex = 0; // loop video
            bool isKeyframe = (frameIndex == 0);
            auto mp2 = get_img_data(mfs, frameIndex);
            if (!mp2.empty()) {
                codec(mp2, framebuffer, capW, capH, isKeyframe);
                if (!framebuffer.empty()) {
                    rgb565_to_rgb24(framebuffer, rgb, capW, capH);
                    render_frame(hwnd, rgb, capW, capH);
                }
            }

            frameIndex++;
            lastTick = now;
        }
    }
}
