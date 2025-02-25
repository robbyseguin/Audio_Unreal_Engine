/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

class Version
{
public:
    constexpr Version() = default;

    constexpr explicit Version (int majorIn)
        : Version (majorIn, 0) {}

    constexpr Version (int majorIn, int minorIn)
        : major (majorIn), minor (minorIn) {}

    int major = 0, minor = 0;

    constexpr bool operator== (const Version& other) const noexcept
    {
        return toTuple() == other.toTuple();
    }

    constexpr bool operator!= (const Version& other) const noexcept
    {
        return toTuple() != other.toTuple();
    }

    constexpr bool operator< (const Version& other) const noexcept
    {
        return toTuple() < other.toTuple();
    }

    constexpr bool operator<= (const Version& other) const noexcept
    {
        return toTuple() <= other.toTuple();
    }

    constexpr bool operator> (const Version& other) const noexcept
    {
        return toTuple() > other.toTuple();
    }

    constexpr bool operator>= (const Version& other) const noexcept
    {
        return toTuple() >= other.toTuple();
    }

private:
    constexpr std::tuple<int, int> toTuple() const noexcept
    {
        return std::make_tuple (major, minor);
    }
};


template <typename Char>
static auto* findNullTerminator (const Char* ptr)
{
    while (*ptr != 0)
        ++ptr;

    return ptr;
}

static Version getOpenGLVersion()
{
    const auto* versionBegin = glGetString (GL_VERSION);

    if (versionBegin == nullptr)
        return {};

    const auto* versionEnd = findNullTerminator (versionBegin);
    const std::string versionString (versionBegin, versionEnd);
    const auto spaceSeparated = StringArray::fromTokens (versionString.c_str(), false);

    for (const auto& token : spaceSeparated)
    {
        const auto pointSeparated = StringArray::fromTokens (token, ".", "");

        const auto major = pointSeparated[0].getIntValue();
        const auto minor = pointSeparated[1].getIntValue();

        if (major != 0)
            return { major, minor };
    }

    return {};
}

void OpenGLHelpers::resetErrorState()
{
    while (glGetError() != GL_NO_ERROR) {}
}

void* OpenGLHelpers::getExtensionFunction (const char* functionName)
{
   #if JUCE_WINDOWS
    return (void*) wglGetProcAddress (functionName);
   #elif JUCE_LINUX || JUCE_BSD
    return (void*) glXGetProcAddress ((const GLubyte*) functionName);
   #else
    static void* handle = dlopen (nullptr, RTLD_LAZY);
    return dlsym (handle, functionName);
   #endif
}

bool OpenGLHelpers::isExtensionSupported (const char* const extensionName)
{
    jassert (extensionName != nullptr); // you must supply a genuine string for this.
    jassert (isContextActive()); // An OpenGL context will need to be active before calling this.

    if (getOpenGLVersion().major >= 3)
    {
        using GetStringi = const GLubyte* (*) (GLenum, GLuint);

        if (auto* thisGlGetStringi = reinterpret_cast<GetStringi> (getExtensionFunction ("glGetStringi")))
        {
            GLint n = 0;
            glGetIntegerv (GL_NUM_EXTENSIONS, &n);

            for (auto i = (decltype (n)) 0; i < n; ++i)
                if (StringRef (extensionName) == StringRef ((const char*) thisGlGetStringi (GL_EXTENSIONS, (GLuint) i)))
                    return true;

            return false;
        }
    }

    const char* extensions = (const char*) glGetString (GL_EXTENSIONS);
    jassert (extensions != nullptr); // Perhaps you didn't activate an OpenGL context before calling this?

    for (;;)
    {
        const char* found = strstr (extensions, extensionName);

        if (found == nullptr)
            break;

        extensions = found + strlen (extensionName);

        if (extensions[0] == ' ' || extensions[0] == 0)
            return true;
    }

    return false;
}

void OpenGLHelpers::clear (Colour colour)
{
    glClearColor (colour.getFloatRed(), colour.getFloatGreen(),
                  colour.getFloatBlue(), colour.getFloatAlpha());

    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void OpenGLHelpers::enableScissorTest (Rectangle<int> clip)
{
    glEnable (GL_SCISSOR_TEST);
    glScissor (clip.getX(), clip.getY(), clip.getWidth(), clip.getHeight());
}

String OpenGLHelpers::getGLSLVersionString()
{
    if (getOpenGLVersion() >= Version (3, 2))
    {
       #if JUCE_OPENGL_ES
        return "#version 300 es";
       #else
        return "#version 150";
       #endif
    }

    return "#version 110";
}

String OpenGLHelpers::translateVertexShaderToV3 (const String& code)
{
    if (getOpenGLVersion() >= Version (3, 2))
    {
        String output;

       #if JUCE_ANDROID
        {
            int numAttributes = 0;

            for (int p = code.indexOf (0, "attribute "); p >= 0; p = code.indexOf (p + 1, "attribute "))
                numAttributes++;

            int last = 0;

            for (int p = code.indexOf (0, "attribute "); p >= 0; p = code.indexOf (p + 1, "attribute "))
            {
                output += code.substring (last, p) + "layout(location=" + String (--numAttributes) + ") in ";

                last = p + 10;
            }

            output += code.substring (last);
        }
       #else
        output = code.replace ("attribute", "in");
       #endif

        return getGLSLVersionString() + "\n" + output.replace ("varying", "out");
    }

    return code;
}

String OpenGLHelpers::translateFragmentShaderToV3 (const String& code)
{
    if (getOpenGLVersion() >= Version (3, 2))
        return getGLSLVersionString() + "\n"
               "out " JUCE_MEDIUMP " vec4 fragColor;\n"
                + code.replace ("varying", "in")
                      .replace ("texture2D", "texture")
                      .replace ("gl_FragColor", "fragColor");

    return code;
}

} // namespace juce
