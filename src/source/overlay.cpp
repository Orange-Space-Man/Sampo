#include "overlay.h"

#include "log.h"
#include "mod_manager.h"
#include "noita_mainmenu.h"
#include "settings.h"

#include <windows.h>
#include <gl/GL.h>
#include <imgui.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
    enum class Page {
        logs,
        modManager,
        settings
    };

    bool p_initialized = false;
    bool p_visible = false;
    Page p_page = Page::logs;
    float p_fontSize = 0.0f;
    GLuint p_fontTexture = 0;
    GLuint p_noitaFontTexture = 0;
    GLuint p_noitaShadowTexture = 0;
    unsigned p_noitaFontWidth = 0;
    unsigned p_noitaFontHeight = 0;
    std::vector<unsigned char> p_noitaFontPixels;

    struct NoitaGlyph {
        int offsetX = 0;
        int offsetY = 0;
        int height = 0;
        int width = 0;
        int x = 0;
        int y = 0;
        int advance = 0;
        bool valid = false;
    };

    std::array<NoitaGlyph, 256> p_noitaGlyphs{};
    LARGE_INTEGER p_lastFrame{};
    LARGE_INTEGER p_frequency{};
    HWND p_window = nullptr;
    WNDPROC f_oWindowProcedure = nullptr;
    bool p_mouseButtons[3]{};

    bool isMouseMessage(UINT message) {
        switch (message) {
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            return true;
        default:
            return false;
        }
    }

    ImGuiKey imguiKey(WPARAM key) {
        switch (key) {
        case VK_TAB:
            return ImGuiKey_Tab;
        case VK_LEFT:
            return ImGuiKey_LeftArrow;
        case VK_RIGHT:
            return ImGuiKey_RightArrow;
        case VK_UP:
            return ImGuiKey_UpArrow;
        case VK_DOWN:
            return ImGuiKey_DownArrow;
        case VK_PRIOR:
            return ImGuiKey_PageUp;
        case VK_NEXT:
            return ImGuiKey_PageDown;
        case VK_HOME:
            return ImGuiKey_Home;
        case VK_END:
            return ImGuiKey_End;
        case VK_INSERT:
            return ImGuiKey_Insert;
        case VK_DELETE:
            return ImGuiKey_Delete;
        case VK_BACK:
            return ImGuiKey_Backspace;
        case VK_SPACE:
            return ImGuiKey_Space;
        case VK_RETURN:
            return ImGuiKey_Enter;
        case VK_ESCAPE:
            return ImGuiKey_Escape;
        case 'A':
            return ImGuiKey_A;
        case 'C':
            return ImGuiKey_C;
        case 'V':
            return ImGuiKey_V;
        case 'X':
            return ImGuiKey_X;
        case 'Y':
            return ImGuiKey_Y;
        case 'Z':
            return ImGuiKey_Z;
        default:
            return ImGuiKey_None;
        }
    }

    LRESULT CALLBACK hookWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (p_initialized) {
            ImGuiIO& io = ImGui::GetIO();
            switch (message) {
            case WM_MOUSEMOVE:
                io.AddMousePosEvent(static_cast<float>(static_cast<short>(LOWORD(lParam))), static_cast<float>(static_cast<short>(HIWORD(lParam))));
                break;
            case WM_MOUSEWHEEL:
                io.AddMouseWheelEvent(0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
                break;
            case WM_MOUSEHWHEEL:
                io.AddMouseWheelEvent(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA, 0.0f);
                break;
            case WM_CHAR:
                if (wParam != '`' && wParam != '~') {
                    io.AddInputCharacterUTF16(static_cast<unsigned short>(wParam));
                }
                break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                if (!down && wParam == VK_OEM_3) {
                    p_visible = !p_visible;
                    return 0;
                }

                const ImGuiKey key = imguiKey(wParam);
                if (key != ImGuiKey_None) {
                    io.AddKeyEvent(key, down);
                }
                if (wParam == VK_CONTROL) {
                    io.AddKeyEvent(ImGuiMod_Ctrl, down);
                }
                if (wParam == VK_SHIFT) {
                    io.AddKeyEvent(ImGuiMod_Shift, down);
                }
                if (wParam == VK_MENU) {
                    io.AddKeyEvent(ImGuiMod_Alt, down);
                }
                break;
            }
            }

            if (p_visible && (isMouseMessage(message) || message == WM_CHAR ||
                              message == WM_KEYDOWN || message == WM_KEYUP ||
                              message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)) {
                return 0;
            }
        }

        return CallWindowProcW(f_oWindowProcedure, window, message, wParam, lParam);
    }

    std::filesystem::path noitaDataPath(const wchar_t* relative) {
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        return std::filesystem::path(executable).parent_path() / relative;
    }

    bool loadNoitaFont() {
        const std::filesystem::path imagePath = noitaDataPath(L"data/fonts/font_pixel.png");
        const std::filesystem::path xmlPath = noitaDataPath(L"data/fonts/font_pixel.xml");

        std::ifstream xml(xmlPath);
        if (!xml) {
            return false;
        }

        std::string line;
        while (std::getline(xml, line)) {
            unsigned id = 0;
            NoitaGlyph glyph;
            if (sscanf_s(line.c_str(),
                         " <QuadChar id=\"%u\" offset_x=\"%d\" offset_y=\"%d\" rect_h=\"%d\" rect_w=\"%d\" rect_x=\"%d\" rect_y=\"%d\" width=\"%d\"",
                         &id, &glyph.offsetX, &glyph.offsetY, &glyph.height, &glyph.width,
                         &glyph.x, &glyph.y, &glyph.advance) == 8 &&
                id < p_noitaGlyphs.size()) {
                glyph.valid = true;
                p_noitaGlyphs[id] = glyph;
            }
        }

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            return false;
        }

        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(result)) {
            result = factory->CreateDecoderFromFilename(imagePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
        }
        if (SUCCEEDED(result)) {
            result = decoder->GetFrame(0, &frame);
        }
        if (SUCCEEDED(result)) {
            result = factory->CreateFormatConverter(&converter);
        }
        if (SUCCEEDED(result)) {
            result = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        }
        if (SUCCEEDED(result)) {
            result = converter->GetSize(&p_noitaFontWidth, &p_noitaFontHeight);
        }

        if (SUCCEEDED(result)) {
            p_noitaFontPixels.resize(static_cast<std::size_t>(p_noitaFontWidth) * p_noitaFontHeight * 4);
            result = converter->CopyPixels(nullptr, p_noitaFontWidth * 4, static_cast<UINT>(p_noitaFontPixels.size()), p_noitaFontPixels.data());
        }

        if (converter != nullptr) {
            converter->Release();
        }
        if (frame != nullptr) {
            frame->Release();
        }
        if (decoder != nullptr) {
            decoder->Release();
        }
        if (factory != nullptr) {
            factory->Release();
        }

        if (FAILED(result)) {
            return false;
        }
        if (p_noitaFontPixels.empty()) {
            return false;
        }

        std::vector<unsigned char> letterPixels = p_noitaFontPixels;
        std::vector<unsigned char> shadowPixels = p_noitaFontPixels;
        for (std::size_t offset = 0; offset + 3 < p_noitaFontPixels.size(); offset += 4) {
            const bool shadow = p_noitaFontPixels[offset] == 0 && p_noitaFontPixels[offset + 1] == 0 && p_noitaFontPixels[offset + 2] == 0 && p_noitaFontPixels[offset + 3] != 0;
            if (shadow) {
                letterPixels[offset + 3] = 0;
                shadowPixels[offset] = 0xFF;
                shadowPixels[offset + 1] = 0xFF;
                shadowPixels[offset + 2] = 0xFF;
            } else {
                shadowPixels[offset + 3] = 0;
            }
        }

        GLint previousTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glGenTextures(1, &p_noitaFontTexture);
        glBindTexture(GL_TEXTURE_2D, p_noitaFontTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_noitaFontWidth, p_noitaFontHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, letterPixels.data());
        glGenTextures(1, &p_noitaShadowTexture);
        glBindTexture(GL_TEXTURE_2D, p_noitaShadowTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_noitaFontWidth, p_noitaFontHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, shadowPixels.data());
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        return true;
    }

    bool installNoitaFont() {
        if (p_noitaFontPixels.empty()) {
            return false;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        io.Fonts->TexGlyphPadding = 1;
        ImFontConfig config;
        config.SizePixels = 11.0f;
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        strcpy_s(config.Name, "Noita font_pixel");
        ImFont* const font = io.Fonts->AddFontDefaultBitmap(&config);
        if (font == nullptr) {
            return false;
        }

        std::array<ImFontAtlasRectId, 256> rectangles{};
        rectangles.fill(ImFontAtlasRectId_Invalid);
        for (unsigned codepoint = 32; codepoint < p_noitaGlyphs.size(); ++codepoint) {
            const NoitaGlyph& glyph = p_noitaGlyphs[codepoint];
            if (!glyph.valid || glyph.width <= 0 || glyph.height <= 0) {
                continue;
            }

            rectangles[codepoint] = io.Fonts->AddCustomRectFontGlyph(font,
                                                                     static_cast<ImWchar>(codepoint), glyph.width, glyph.height,
                                                                     static_cast<float>(glyph.advance), ImVec2(static_cast<float>(glyph.offsetX), static_cast<float>(glyph.offsetY)));
            if (rectangles[codepoint] != ImFontAtlasRectId_Invalid) {
                ImFontBaked* const baked = font->GetFontBaked(font->LegacySize);
                if (baked != nullptr && !baked->Glyphs.empty()) {
                    baked->Glyphs.back().Colored = false;
                }
            }
        }

        unsigned char* atlasPixels = nullptr;
        int atlasWidth = 0;
        int atlasHeight = 0;
        io.Fonts->GetTexDataAsRGBA32(&atlasPixels, &atlasWidth, &atlasHeight);
        if (atlasPixels == nullptr || atlasWidth <= 0 || atlasHeight <= 0) {
            return false;
        }

        for (unsigned codepoint = 32; codepoint < rectangles.size(); ++codepoint) {
            if (rectangles[codepoint] == ImFontAtlasRectId_Invalid) {
                continue;
            }

            ImFontAtlasRect rectangle;
            if (!io.Fonts->GetCustomRect(rectangles[codepoint], &rectangle)) {
                continue;
            }

            const NoitaGlyph& glyph = p_noitaGlyphs[codepoint];
            for (int y = 0; y < glyph.height; ++y) {
                for (int x = 0; x < glyph.width; ++x) {
                    const std::size_t source = (static_cast<std::size_t>(glyph.y + y) * p_noitaFontWidth + glyph.x + x) * 4;
                    const std::size_t destination = (static_cast<std::size_t>(rectangle.y + y) * atlasWidth + rectangle.x + x) * 4;
                    std::memcpy(atlasPixels + destination, p_noitaFontPixels.data() + source, 4);
                }
            }
        }

        io.FontDefault = font;
        font->Flags |= ImFontFlags_LockBakedSizes;
        ImGui::GetStyle().FontSizeBase = 20.0f;
        return true;
    }

    void createFontTexture() {
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        GLint previousTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glGenTextures(1, &p_fontTexture);
        glBindTexture(GL_TEXTURE_2D, p_fontTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        io.Fonts->SetTexID(static_cast<ImTextureID>(p_fontTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    }

    void initialize() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.BackendPlatformName = "Sampo Win32";
        io.BackendRendererName = "Sampo OpenGL";
        settings::applyTheme();
        if (loadNoitaFont()) {
            installNoitaFont();
        }
        createFontTexture();
        QueryPerformanceFrequency(&p_frequency);
        QueryPerformanceCounter(&p_lastFrame);

        p_window = WindowFromDC(wglGetCurrentDC());
        if (p_window != nullptr) {
            f_oWindowProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(p_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hookWindowProcedure)));
        }

        p_initialized = true;
    }

    void updateFontSize(const ImVec2& displaySize) {
        float dpiScale = 1.0f;
        if (p_window != nullptr) {
            const UINT dpi = GetDpiForWindow(p_window);
            if (dpi != 0) {
                dpiScale = static_cast<float>(dpi) / 96.0f;
            }
        }

        const float resolutionScale = (std::max)(displaySize.x / 1920.0f, displaySize.y / 1080.0f);
        const float scale = std::clamp((std::max)(dpiScale, resolutionScale), 0.90f, 2.0f);
        const float target = std::clamp(std::round(20.0f * scale), 18.0f, 40.0f);
        if (target == p_fontSize) {
            return;
        }

        p_fontSize = target;
        ImGui::GetStyle().FontSizeBase = target;
    }

    void updateMouse() {
        if (p_window == nullptr) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        POINT cursor{};
        if (GetCursorPos(&cursor) && ScreenToClient(p_window, &cursor)) {
            io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
        }

        constexpr int keys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
        for (int index = 0; index < 3; ++index) {
            const bool down = (GetAsyncKeyState(keys[index]) & 0x8000) != 0;
            if (down != p_mouseButtons[index]) {
                p_mouseButtons[index] = down;
                io.AddMouseButtonEvent(index, down);
            }
        }
    }

    void drawMenuBar() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        if (ImGui::MenuItem("Sampo Logs", nullptr, p_page == Page::logs)) {
            p_page = Page::logs;
        }
        if (ImGui::MenuItem("Mod Manager", nullptr, p_page == Page::modManager)) {
            p_page = Page::modManager;
        }
        if (ImGui::MenuItem("Settings", nullptr, p_page == Page::settings)) {
            p_page = Page::settings;
        }
        ImGui::EndMainMenuBar();
    }

    bool isValueBoundary(const std::string& text, std::size_t position) {
        if (position >= text.size()) {
            return true;
        }

        const unsigned char character = static_cast<unsigned char>(text[position]);
        return std::isalnum(character) == 0 && character != '_';
    }

    bool matchesValueWord(const std::string& text, std::size_t position, const char* word) {
        const std::size_t wordLength = std::strlen(word);
        if (position + wordLength > text.size()) {
            return false;
        }
        if (position > 0 && !isValueBoundary(text, position - 1)) {
            return false;
        }
        if (!isValueBoundary(text, position + wordLength)) {
            return false;
        }

        for (std::size_t index = 0; index < wordLength; ++index) {
            const unsigned char textCharacter = static_cast<unsigned char>(text[position + index]);
            const unsigned char wordCharacter = static_cast<unsigned char>(word[index]);
            if (std::tolower(textCharacter) != std::tolower(wordCharacter)) {
                return false;
            }
        }

        return true;
    }

    std::size_t valueLength(const std::string& text, std::size_t position) {
        constexpr const char* valueWords[] = {"true", "false", "yes", "no"};
        for (const char* word : valueWords) {
            if (matchesValueWord(text, position, word)) {
                return std::strlen(word);
            }
        }

        if (position > 0 && !isValueBoundary(text, position - 1)) {
            return 0;
        }

        std::size_t index = position;
        if (index < text.size() && (text[index] == '-' || text[index] == '+')) {
            ++index;
        }
        if (index >= text.size() || std::isdigit(static_cast<unsigned char>(text[index])) == 0) {
            return 0;
        }

        if (index + 1 < text.size() && text[index] == '0' && (text[index + 1] == 'x' || text[index + 1] == 'X')) {
            index += 2;
            const std::size_t hexadecimalStart = index;
            while (index < text.size() && std::isxdigit(static_cast<unsigned char>(text[index])) != 0) {
                ++index;
            }
            if (index == hexadecimalStart) {
                return 0;
            }
        } else {
            while (index < text.size() && std::isxdigit(static_cast<unsigned char>(text[index])) != 0) {
                ++index;
            }
            if (index < text.size() && text[index] == '.') {
                ++index;
                while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
                    ++index;
                }
            }
        }

        if (!isValueBoundary(text, index)) {
            return 0;
        }

        return index - position;
    }

    void drawTextSegment(ImDrawList* drawList, ImVec2& position, const std::string& text, std::size_t start, std::size_t length, const ImVec4& color) {
        if (length == 0) {
            return;
        }

        const char* begin = text.data() + start;
        const char* end = begin + length;
        drawList->AddText(position, ImGui::ColorConvertFloat4ToU32(color), begin, end);
        position.x += ImGui::CalcTextSize(begin, end).x;
    }

    void drawColoredText(const std::string& text, std::size_t offset, const ImVec4& color) {
        const ImVec4& blue = settings::logValueColor();
        const std::string visibleText = text.substr(offset);
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const ImVec2 startPosition = ImGui::GetCursorScreenPos();
        ImVec2 position = startPosition;
        std::size_t segmentStart = 0;
        std::size_t index = 0;

        while (index < visibleText.size()) {
            const std::size_t highlightedLength = valueLength(visibleText, index);
            if (highlightedLength == 0) {
                ++index;
                continue;
            }

            drawTextSegment(drawList, position, visibleText, segmentStart, index - segmentStart, color);
            drawTextSegment(drawList, position, visibleText, index, highlightedLength, blue);
            index += highlightedLength;
            segmentStart = index;
        }

        drawTextSegment(drawList, position, visibleText, segmentStart, visibleText.size() - segmentStart, color);
        ImGui::Dummy(ImVec2(position.x - startPosition.x, ImGui::GetTextLineHeight()));
    }

    void drawLogLine(const sampo::log::Line& line) {
        constexpr const char* arrowPrefix = "\t\xE2\x86\xB3 ";
        const std::size_t arrowPrefixLength = std::strlen(arrowPrefix);
        const bool isArrowLine = line.text.size() >= arrowPrefixLength &&
                                 line.text.compare(0, arrowPrefixLength, arrowPrefix) == 0;
        const ImVec4& white = settings::logTextColor();
        const ImVec4& red = settings::logErrorColor();

        if (!isArrowLine) {
            if (line.error) {
                drawColoredText(line.text, 0, red);
            } else {
                drawColoredText(line.text, 0, white);
            }
            return;
        }

        const float sampoLabelWidth = ImGui::CalcTextSize("[SAMPO]").x;
        const float messageIndent = sampoLabelWidth * 0.5f;
        ImGui::Indent(messageIndent);
        const ImVec2 textPosition = ImGui::GetCursorScreenPos();
        const float lineHeight = ImGui::GetTextLineHeight();
        const float arrowX = textPosition.x - 15.0f;
        const float arrowMiddleY = textPosition.y + lineHeight * 0.52f;
        const ImVec4& gray = settings::logArrowColor();
        const ImVec4& darkRed = settings::logErrorArrowColor();
        ImVec4 arrowTextColor = gray;
        if (line.error) {
            arrowTextColor = darkRed;
        }
        const ImU32 arrowColor = ImGui::ColorConvertFloat4ToU32(arrowTextColor);
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        drawList->AddLine(ImVec2(arrowX, textPosition.y + 1.0f), ImVec2(arrowX, arrowMiddleY), arrowColor, 1.0f);
        drawList->AddLine(ImVec2(arrowX, arrowMiddleY), ImVec2(arrowX + 8.0f, arrowMiddleY), arrowColor, 1.0f);
        drawList->AddLine(ImVec2(arrowX + 8.0f, arrowMiddleY), ImVec2(arrowX + 5.0f, arrowMiddleY - 3.0f), arrowColor, 1.0f);
        drawList->AddLine(ImVec2(arrowX + 8.0f, arrowMiddleY), ImVec2(arrowX + 5.0f, arrowMiddleY + 3.0f), arrowColor, 1.0f);
        drawColoredText(line.text, arrowPrefixLength, arrowTextColor);
        ImGui::Unindent(messageIndent);
    }

    void drawLogs(const ImVec2& displaySize) {
        const float top = ImGui::GetFrameHeight();
        ImGui::SetNextWindowPos(ImVec2(0.0f, top), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(displaySize.x, displaySize.y - top), ImGuiCond_Always);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##SampoLogs", nullptr, flags)) {
            ImGui::BeginChild("##SampoLogContents", ImVec2(-1.0f, -1.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            const std::vector<sampo::log::Line> lines = sampo::log::snapshot();
            if (lines.empty()) {
                ImGui::TextDisabled("No log entries.");
            } else {
                for (const sampo::log::Line& line : lines) {
                    drawLogLine(line);
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void render(ImDrawData* drawData) {
        if (drawData == nullptr || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) {
            return;
        }

        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT |
                     GL_VIEWPORT_BIT | GL_SCISSOR_BIT | GL_TEXTURE_BIT);
        glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        glViewport(0, 0, static_cast<GLsizei>(drawData->DisplaySize.x), static_cast<GLsizei>(drawData->DisplaySize.y));
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(drawData->DisplayPos.x, drawData->DisplayPos.x + drawData->DisplaySize.x,
                drawData->DisplayPos.y + drawData->DisplaySize.y, drawData->DisplayPos.y, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
        for (const ImDrawList* commandList : drawData->CmdLists) {
            const ImDrawVert* vertices = commandList->VtxBuffer.Data;
            const ImDrawIdx* indices = commandList->IdxBuffer.Data;
            glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), &vertices->pos);
            glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), &vertices->uv);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), &vertices->col);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);

            for (const ImDrawCmd& command : commandList->CmdBuffer) {
                if (command.UserCallback != nullptr) {
                    command.UserCallback(commandList, &command);
                    continue;
                }

                const ImVec4 clip = command.ClipRect;
                glScissor(static_cast<GLint>(clip.x), static_cast<GLint>(framebufferHeight - clip.w), static_cast<GLsizei>(clip.z - clip.x), static_cast<GLsizei>(clip.w - clip.y));
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(command.GetTexID()));
                GLenum indexType = GL_UNSIGNED_INT;
                if (sizeof(ImDrawIdx) == 2) {
                    indexType = GL_UNSIGNED_SHORT;
                }
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(command.ElemCount), indexType,
                               indices + command.IdxOffset);
            }
        }

        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glPopClientAttrib();
        glPopAttrib();
    }
}

void overlay::showModManager() {
    p_page = Page::modManager;
    p_visible = true;
}

void overlay::drawNoitaText(float x, float y, float scale, unsigned int color, const char* text, bool rainbow) {
    if (p_noitaFontTexture == 0 || p_noitaFontWidth == 0 || p_noitaFontHeight == 0 || text == nullptr) {
        return;
    }

    ImDrawList* const drawList = ImGui::GetForegroundDrawList();
    float currentX = x;
    for (const unsigned char character : std::string_view(text)) {
        const NoitaGlyph& glyph = p_noitaGlyphs[character];
        if (!glyph.valid) {
            continue;
        }

        const ImVec2 minimum(currentX + static_cast<float>(glyph.offsetX) * scale, y + static_cast<float>(glyph.offsetY) * scale);
        const ImVec2 maximum(minimum.x + static_cast<float>(glyph.width) * scale, minimum.y + static_cast<float>(glyph.height) * scale);
        const ImVec2 uvMinimum(static_cast<float>(glyph.x) / static_cast<float>(p_noitaFontWidth), static_cast<float>(glyph.y) / static_cast<float>(p_noitaFontHeight));
        const ImVec2 uvMaximum(static_cast<float>(glyph.x + glyph.width) / static_cast<float>(p_noitaFontWidth), static_cast<float>(glyph.y + glyph.height) / static_cast<float>(p_noitaFontHeight));
        ImU32 glyphColor = color;
        if (rainbow) {
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;
            const float hue = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.10f + currentX / scale * 0.005f, 1.0f);
            ImGui::ColorConvertHSVtoRGB(hue, 0.72f, 0.90f, red, green, blue);
            glyphColor = ImGui::ColorConvertFloat4ToU32(ImVec4(red, green, blue, 1.0f));
        }

        drawList->AddImage(static_cast<ImTextureID>(p_noitaShadowTexture), minimum, maximum, uvMinimum, uvMaximum, IM_COL32(0x12, 0x13, 0x1A, 0xFF));
        drawList->AddImage(static_cast<ImTextureID>(p_noitaFontTexture), minimum, maximum, uvMinimum, uvMaximum, glyphColor);
        currentX += static_cast<float>(glyph.advance) * scale;
    }
}

float overlay::noitaTextWidth(float scale, const char* text) {
    if (text == nullptr) {
        return 0.0f;
    }

    float width = 0.0f;
    for (const unsigned char character : std::string_view(text)) {
        const NoitaGlyph& glyph = p_noitaGlyphs[character];
        if (glyph.valid) {
            width += static_cast<float>(glyph.advance) * scale;
        }
    }
    return width;
}

void overlay::draw() {
    if (!p_initialized) {
        initialize();
    }

    if (f_oWindowProcedure == nullptr && (GetAsyncKeyState(VK_OEM_3) & 1) != 0) {
        p_visible = !p_visible;
    }

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(viewport[2]), static_cast<float>(viewport[3]));
    updateFontSize(io.DisplaySize);
    if (p_lastFrame.QuadPart != 0) {
        const LONGLONG elapsedTicks = now.QuadPart - p_lastFrame.QuadPart;
        const float elapsedSeconds = static_cast<float>(elapsedTicks) / static_cast<float>(p_frequency.QuadPart);
        io.DeltaTime = elapsedSeconds;
    } else {
        io.DeltaTime = 1.0f / 60.0f;
    }
    p_lastFrame = now;
    updateMouse();
    noita_mainmenu::updateModCheck();

    ImGui::NewFrame();
    if (!p_visible) {
        noita_mainmenu::draw();
    }
    if (p_visible) {
        drawMenuBar();
        if (p_page == Page::logs) {
            drawLogs(io.DisplaySize);
        } else if (p_page == Page::modManager) {
            mod_manager::draw(ImGui::GetFrameHeight());
        } else {
            settings::draw(ImGui::GetFrameHeight());
        }
    }
    ImGui::Render();
    render(ImGui::GetDrawData());
}
