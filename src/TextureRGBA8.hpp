#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

#include <glm/gtc/type_precision.hpp>

#ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
#else
    #include <GL/glew.h>
#endif

#include <imgui.h>


namespace CALevelGen
{
    //An OpenGL 2D texture with RGBA8 pixels, owned RAII-style, which can be displayed in Dear ImGUI.
    //Requires a current GL context for its whole lifetime (including destruction).
    //
    //Pixels are tightly-packed rows, starting from the top-left (ImGUI's default UV's display it that way).
    //There are no mipmaps; to change the size, assign a new instance.
    //
    //NOTE: Creation and modification both leave this texture bound to GL_TEXTURE_2D.
    class TextureRGBA8
    {
    public:

        //If no initial pixels are given, the contents start undefined.
        TextureRGBA8(int width, int height,
                     bool smoothFiltering = false,
                     std::span<const glm::u8vec4> initialPixels = { })
            : width(width), height(height)
        {
            assert(width > 0 && height > 0);
            assert(initialPixels.empty() || initialPixels.size() == PixelCount());

            glGenTextures(1, &handle);
            glBindTexture(GL_TEXTURE_2D, handle);

            //The default min filter uses mipmaps, which this texture doesn't have.
            //   That would make the texture "incomplete", which renders as black.
            GLint filter = smoothFiltering ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            //RGBA8 rows are always a multiple of 4 bytes, so the default GL_UNPACK_ALIGNMENT of 4 is fine.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         initialPixels.empty() ? nullptr : initialPixels.data());
        }
        ~TextureRGBA8()
        {
            //glDeleteTextures() ignores 0, which is what a moved-from instance holds.
            glDeleteTextures(1, &handle);
        }

        //Only one instance can own a GL texture, so no copying; moving is fine.
        TextureRGBA8(const TextureRGBA8&) = delete;
        TextureRGBA8& operator=(const TextureRGBA8&) = delete;
        TextureRGBA8(TextureRGBA8&& from) noexcept
            : handle(std::exchange(from.handle, 0)), width(from.width), height(from.height) { }
        TextureRGBA8& operator=(TextureRGBA8&& from) noexcept
        {
            std::swap(handle, from.handle);
            std::swap(width, from.width);
            std::swap(height, from.height);
            return *this;
        }


        //Updates the pixels of the entire texture.
        void Update(std::span<const glm::u8vec4> pixels)
        {
            assert(handle != 0);
            assert(pixels.size() == PixelCount());

            glBindTexture(GL_TEXTURE_2D, handle);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }

        //Displays the texture as an ImGUI widget.
        //By default it draws at its own pixel size; 'scale' enlarges it.
        void DrawInImGui(float scale = 1.0f) const
        {
            ImGui::Image(ImGuiTexture(), ImVec2(width * scale, height * scale));
        }
        //For other ImGUI calls that take a texture, like ImGui::ImageButton() or ImDrawList::AddImage().
        ImTextureID ImGuiTexture() const { return static_cast<ImTextureID>(static_cast<intptr_t>(handle)); }


        int Width() const { return width; }
        int Height() const { return height; }
        GLuint GLHandle() const { return handle; }

    private:

        GLuint handle = 0;
        int width, height;

        size_t PixelCount() const { return static_cast<size_t>(width) * static_cast<size_t>(height); }
    };
}
