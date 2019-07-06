// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

// Include the main libnx system header, for Switch development
#include <switch.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/effects/SkGradientShader.h"
#include "include/effects/SkDiscretePathEffect.h"
#include "include/gpu/GrContext.h"
#include "include/gpu/gl/GrGLInterface.h"

#define LTRACEF(fmt, ...) printf("%s: " fmt "\n", __PRETTY_FUNCTION__, ## __VA_ARGS__)

#define FB_WIDTH  1280
#define FB_HEIGHT 720

int nx_link_sock = -1;

sk_sp<SkTypeface> font_standard;
sk_sp<SkTypeface> dm_sans_regular;

static EGLDisplay s_display;
static EGLContext s_context;
static EGLSurface s_surface;

static bool initEgl(NWindow* win)
{
    // Connect to the EGL default display
    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!s_display)
    {
        LTRACEF("Could not connect to display! error: %d", eglGetError());
        goto _fail0;
    }

    // Initialize the EGL display connection
    eglInitialize(s_display, nullptr, nullptr);

    // Select OpenGL (Core) as the desired graphics API
    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE)
    {
        LTRACEF("Could not set API! error: %d", eglGetError());
        goto _fail1;
    }

    // Get an appropriate EGL framebuffer configuration
    EGLConfig config;
    EGLint numConfigs;
    static const EGLint framebufferAttributeList[] =
    {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE,     8,
        EGL_GREEN_SIZE,   8,
        EGL_BLUE_SIZE,    8,
        EGL_ALPHA_SIZE,   8,
        EGL_DEPTH_SIZE,   24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    eglChooseConfig(s_display, framebufferAttributeList, &config, 1, &numConfigs);
    if (numConfigs == 0)
    {
        LTRACEF("No config found! error: %d", eglGetError());
        goto _fail1;
    }

    // Create an EGL window surface
    s_surface = eglCreateWindowSurface(s_display, config, win, nullptr);
    if (!s_surface)
    {
        LTRACEF("Surface creation failed! error: %d", eglGetError());
        goto _fail1;
    }

    // Create an EGL rendering context
    static const EGLint contextAttributeList[] =
    {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
        EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_NONE
    };
    s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttributeList);
    if (!s_context)
    {
        LTRACEF("Context creation failed! error: %d", eglGetError());
        goto _fail2;
    }

    // Connect the context to the surface
    eglMakeCurrent(s_display, s_surface, s_surface, s_context);
    return true;

_fail2:
    eglDestroySurface(s_display, s_surface);
    s_surface = nullptr;
_fail1:
    eglTerminate(s_display);
    s_display = nullptr;
_fail0:
    return false;
}

static void deinitEgl()
{
    if (s_display)
    {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context)
        {
            eglDestroyContext(s_display, s_context);
            s_context = nullptr;
        }
        if (s_surface)
        {
            eglDestroySurface(s_display, s_surface);
            s_surface = nullptr;
        }
        eglTerminate(s_display);
        s_display = nullptr;
    }
}

extern "C" void userAppInit(void)
{
    Result res;

    res = socketInitializeDefault();
    if (R_FAILED(res))
        fatalSimple(res);

    nx_link_sock = nxlinkStdio();

    res = romfsInit();
    if (R_FAILED(res))
        fatalSimple(res);

    res = plInitialize();
    if (R_FAILED(res))
        fatalSimple(res);
}

extern "C" void userAppExit(void)
{
    plExit();
    close(nx_link_sock);
    socketExit();
    romfsExit();
}

SkPath star() {
    const SkScalar R = 60.0f, C = 128.0f;
    SkPath path;
    path.moveTo(C + R, C);
    for (int i = 1; i < 15; ++i) {
        SkScalar a = 0.44879895f * i;
        SkScalar r = R + R * (i % 2);
        path.lineTo(C + r * cos(a), C + r * sin(a));
    }
    return path;
}

void draw_star(SkCanvas* canvas) {
    SkPaint paint;
    paint.setPathEffect(SkDiscretePathEffect::Make(10.0f, 4.0f));
        SkPoint points[2] = {
        SkPoint::Make(0.0f, 0.0f),
        SkPoint::Make(256.0f, 256.0f)
    };
    SkColor colors[2] = {SkColorSetRGB(66,133,244), SkColorSetRGB(15,157,88)};
    paint.setShader(SkGradientShader::MakeLinear(
                     points, colors, NULL, 2,
                     SkTileMode::kClamp, 0, NULL));
    paint.setAntiAlias(true);
    SkPath path(star());
    canvas->drawPath(path, paint);
}

static void load_fonts(void)
{
    PlFontData font;
    Result res = plGetSharedFontByType(&font, PlSharedFontType_Standard);

    if(R_SUCCEEDED(res))
    {
        font_standard = SkTypeface::MakeFromData(SkData::MakeWithoutCopy(font.address, font.size), 0);
    }

    if(font_standard.get() == nullptr)
    {
        font_standard = SkTypeface::MakeDefault();
    }

    dm_sans_regular = SkTypeface::MakeFromFile("romfs:/DMSans-Regular.ttf", 0);
    if(dm_sans_regular.get() == nullptr)
    {
        dm_sans_regular = SkTypeface::MakeDefault();
    }
}

// Main program entrypoint
int main(int argc, char* argv[])
{
    if (!initEgl(nwindowGetDefault()))
        return EXIT_FAILURE;

    auto interface = GrGLMakeNativeInterface();

    auto ctx = GrContext::MakeGL();

    GrGLint buffer;
    interface->fFunctions.fGetIntegerv(GL_FRAMEBUFFER_BINDING, &buffer);
    GrGLFramebufferInfo info;
    info.fFormat = GL_RGBA8;
    info.fFBOID = (GrGLuint)buffer;

    GrBackendRenderTarget target(FB_WIDTH, FB_HEIGHT, 0, 8, info);

    SkSurfaceProps props(SkSurfaceProps::kLegacyFontHost_InitType);

    auto surface = SkSurface::MakeFromBackendRenderTarget(ctx.get(), target,
            kBottomLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType,
            nullptr, &props);

    load_fonts();

    SkCanvas* canvas = surface->getCanvas();

    // Main loop
    while (appletMainLoop())
    {
        // Scan all the inputs. This should be done once for each frame
        hidScanInput();

        // hidKeysDown returns information about which buttons have been
        // just pressed in this frame compared to the previous one
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_PLUS)
            break; // break in order to return to hbmenu

        canvas->clear(SK_ColorBLACK);
        draw_star(canvas);

        SkPaint paint;
        paint.setColor(SK_ColorRED);
        paint.setAntiAlias(true);

        canvas->drawString("Standard Font", 20, 300, SkFont(font_standard, 36), paint);
        canvas->drawString("DM Sans Regular", 20, 340, SkFont(dm_sans_regular, 36), paint);

        canvas->flush();
        eglSwapBuffers(s_display, s_surface);
    }

    deinitEgl();

    return 0;
}
