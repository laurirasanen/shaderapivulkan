//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: Unit test program for testing of materialsystem
//
// $NoKeywords: $
//=============================================================================//

#include <windows.h>
#include "FileSystem.h"
#include "appframework/tier2app.h"
#include "filesystem_init.h"
#ifdef TF
#include "materialsystem/imaterialsystem_81.h"
#else
#include "materialsystem/imaterialsystem.h"
#endif
#include "materialsystem/imesh.h"
#include "materialsystem/materialsystem_config.h"
#include "materialsystem/materialsystemutil.h"
#include "tier0/ICommandLine.h"
#include "tier1/KeyValues.h"
#include "tier1/UtlBuffer.h"
#include "vstdlib/random.h"

//-----------------------------------------------------------------------------
// Global systems
//-----------------------------------------------------------------------------
extern IFileSystem *g_pFullFileSystem;
extern IMaterialSystem *g_pMaterialSystem;
extern IMaterialSystem *materials;

//-----------------------------------------------------------------------------
// Purpose: Warning/Msg call back through this API
// Input  : type -
//			*pMsg -
// Output : SpewRetval_t
//-----------------------------------------------------------------------------
SpewRetval_t SpewFunc(SpewType_t type, const char *pMsg)
{
    if (Plat_IsInDebugSession())
    {
        OutputDebugString(pMsg);
        if (type == SPEW_ASSERT)
            return SPEW_DEBUGGER;
    }
    return SPEW_CONTINUE;
}

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
class CMaterialSystemTestApp : public CAppSystemGroup
{
  public:
    virtual bool Create();
    virtual bool PreInit();
    virtual int Main();
    virtual void PostShutdown();
    virtual void Destroy();
    virtual const char *GetAppName() { return "MaterialSystemTest"; }
    virtual bool AppUsesReadPixels() { return false; }

  private:
    // Window management
    bool CreateAppWindow(const char *pTitle, bool bWindowed, int w, int h);

    // Sets up the game path
    bool SetupSearchPaths();

    // Waits for a keypress
    bool WaitForKeypress();

    // Sets the video mode
    bool SetMode();

    // Tests dynamic buffers
    void TestDynamicBuffers(IMatRenderContext *pRenderContext, bool bBuffered);

    // Creates, destroys a test material
    void CreateWireframeMaterial();
    void DestroyMaterial();

    CMaterialReference m_pMaterial;

    HWND m_HWnd;
};

CMaterialSystemTestApp g_ApplicationObject;
DEFINE_WINDOWED_APPLICATION_OBJECT_GLOBALVAR(g_ApplicationObject);

//-----------------------------------------------------------------------------
// Create all singleton systems
//-----------------------------------------------------------------------------
bool CMaterialSystemTestApp::Create()
{
    SpewOutputFunc(SpewFunc);

    bool bSteam;
    char pFileSystemDLL[MAX_PATH];
    if (FileSystem_GetFileSystemDLLName(pFileSystemDLL, MAX_PATH, bSteam) != FS_OK)
        return false;

    AppModule_t fileSystemModule = LoadModule(pFileSystemDLL);
    g_pFullFileSystem = (IFileSystem *)AddSystem(fileSystemModule, FILESYSTEM_INTERFACE_VERSION);
    if (!g_pFullFileSystem)
    {
        Warning("CMaterialSystemTestApp::Create: Unable to connect to %s!\n", FILESYSTEM_INTERFACE_VERSION);
        return false;
    }

    // clang-format off
    AppSystemInfo_t appSystems[] = {
        {"materialsystem.dll", MATERIAL_SYSTEM_INTERFACE_VERSION},
        {"", ""} // Required to terminate the list
    };
    // clang-format on

    if (!AddSystems(appSystems))
        return false;

    g_pMaterialSystem = materials = (IMaterialSystem *)FindSystem(MATERIAL_SYSTEM_INTERFACE_VERSION);
    if (!g_pMaterialSystem)
    {
        Warning("CMaterialSystemTestApp::Create: Unable to connect to %s!\n", MATERIAL_SYSTEM_INTERFACE_VERSION);
        return false;
    }

    const char *pShaderDLL = CommandLine()->ParmValue("-shaderdll");
    if (!pShaderDLL)
    {
        pShaderDLL = "shaderapidx9.dll";
    }

    g_pMaterialSystem->SetShaderAPI(pShaderDLL);
    return true;
}

void CMaterialSystemTestApp::Destroy() {}

//-----------------------------------------------------------------------------
// Window callback
//-----------------------------------------------------------------------------
static LRESULT CALLBACK MaterialSystemTestWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

//-----------------------------------------------------------------------------
// Window management
//-----------------------------------------------------------------------------
bool CMaterialSystemTestApp::CreateAppWindow(const char *pTitle, bool bWindowed, int w, int h)
{
    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = MaterialSystemTestWndProc;
    wc.hInstance = (HINSTANCE)GetAppInstance();
    wc.lpszClassName = "Valve001";
    wc.hIcon = NULL; // LoadIcon( s_HInstance, MAKEINTRESOURCE( IDI_LAUNCHER ) );
    wc.hIconSm = wc.hIcon;

    RegisterClassEx(&wc);

    // Note, it's hidden
    DWORD style = WS_POPUP | WS_CLIPSIBLINGS;

    if (bWindowed)
    {
        // Give it a frame
        style |= WS_OVERLAPPEDWINDOW;
        style &= ~WS_THICKFRAME;
    }

    // Never a max box
    style &= ~WS_MAXIMIZEBOX;

    RECT windowRect;
    windowRect.top = 0;
    windowRect.left = 0;
    windowRect.right = w;
    windowRect.bottom = h;

    // Compute rect needed for that size client area based on window style
    AdjustWindowRectEx(&windowRect, style, FALSE, 0);

    // Create the window
    m_HWnd = CreateWindow(wc.lpszClassName, pTitle, style, 0, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
                          NULL, NULL, (HINSTANCE)GetAppInstance(), NULL);

    if (!m_HWnd)
        return false;

    int CenterX, CenterY;

    CenterX = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    CenterY = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    CenterX = (CenterX < 0) ? 0 : CenterX;
    CenterY = (CenterY < 0) ? 0 : CenterY;

    // In VCR modes, keep it in the upper left so mouse coordinates are always relative to the window.
    SetWindowPos(m_HWnd, NULL, CenterX, CenterY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME);

    return true;
}

//-----------------------------------------------------------------------------
// PreInit, PostShutdown
//-----------------------------------------------------------------------------
bool CMaterialSystemTestApp::PreInit()
{
    if (!g_pFullFileSystem || !g_pMaterialSystem || !materials)
        return false;

    const char *pArg;
    int iWidth = 1024;
    int iHeight = 768;
    bool bWindowed = (CommandLine()->CheckParm("-fullscreen") == nullptr);
    if (CommandLine()->CheckParm("-width", &pArg))
    {
        iWidth = atoi(pArg);
    }
    if (CommandLine()->CheckParm("-height", &pArg))
    {
        iHeight = atoi(pArg);
    }

    if (!CreateAppWindow("Press a Key To Continue", bWindowed, iWidth, iHeight))
        return false;

    // Get the adapter from the command line....
    const char *pAdapterString;
    int nAdapter = 0;
    if (CommandLine()->CheckParm("-adapter", &pAdapterString))
    {
        nAdapter = atoi(pAdapterString);
    }

    int nAdapterFlags = 0;
    if (AppUsesReadPixels())
    {
        nAdapterFlags |= MATERIAL_INIT_ALLOCATE_FULLSCREEN_TEXTURE;
    }

    g_pMaterialSystem->SetAdapter(nAdapter, nAdapterFlags);

    return true;
}

void CMaterialSystemTestApp::PostShutdown() {}

//-----------------------------------------------------------------------------
// Waits for a keypress
//-----------------------------------------------------------------------------
bool CMaterialSystemTestApp::WaitForKeypress()
{
    MSG msg = {nullptr};
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (msg.message == WM_KEYDOWN)
            return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// Sets the video mode
//-----------------------------------------------------------------------------
bool CMaterialSystemTestApp::SetMode()
{
    MaterialSystem_Config_t config;
    if (CommandLine()->CheckParm("-fullscreen"))
    {
        config.SetFlag(MATSYS_VIDCFG_FLAGS_WINDOWED, false);
    }
    else
    {
        config.SetFlag(MATSYS_VIDCFG_FLAGS_WINDOWED, true);
    }

    if (CommandLine()->CheckParm("-resizing"))
    {
        config.SetFlag(MATSYS_VIDCFG_FLAGS_RESIZING, true);
    }

    if (CommandLine()->CheckParm("-mat_vsync"))
    {
        config.SetFlag(MATSYS_VIDCFG_FLAGS_NO_WAIT_FOR_VSYNC, false);
    }
    config.m_nAASamples = CommandLine()->ParmValue("-mat_antialias", 1);
    config.m_nAAQuality = CommandLine()->ParmValue("-mat_aaquality", 0);

    config.m_VideoMode.m_Width = config.m_VideoMode.m_Height = 0;
    config.m_VideoMode.m_Format = IMAGE_FORMAT_BGRX8888;
    config.m_VideoMode.m_RefreshRate = 0;

    bool modeSet = g_pMaterialSystem->SetMode(m_HWnd, config);
    if (!modeSet)
    {
        Error("Unable to set mode\n");
        return false;
    }

    g_pMaterialSystem->OverrideConfig(config, false);
    return true;
}

//-----------------------------------------------------------------------------
// Creates, destroys a test material
//-----------------------------------------------------------------------------
void CMaterialSystemTestApp::CreateWireframeMaterial()
{
    KeyValues *pVMTKeyValues = new KeyValues("Wireframe");
    pVMTKeyValues->SetInt("$vertexcolor", 1);
    pVMTKeyValues->SetInt("$nocull", 1);
    pVMTKeyValues->SetInt("$ignorez", 1);
    m_pMaterial.Init("__test", pVMTKeyValues);
}

void CMaterialSystemTestApp::DestroyMaterial() { m_pMaterial.Shutdown(); }

//-----------------------------------------------------------------------------
// Tests dynamic buffers
//-----------------------------------------------------------------------------
void CMaterialSystemTestApp::TestDynamicBuffers(IMatRenderContext *pMatRenderContext, bool bBuffered)
{
    CreateWireframeMaterial();

    g_pMaterialSystem->BeginFrame(0);

    pMatRenderContext->Bind(m_pMaterial);
    IMesh *pMesh = pMatRenderContext->GetDynamicMesh(bBuffered);

    // clear (so that we can make sure that we aren't getting results from the previous quad)
    pMatRenderContext->ClearColor3ub(RandomInt(0, 100), RandomInt(0, 100), RandomInt(190, 255));
    pMatRenderContext->ClearBuffers(true, true);

    static unsigned char s_pColors[4][4] = {
        {255, 255, 255, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
        {255, 0, 0, 255},
    };

    static int nCount = 0;

    const int nLoopCount = 8;
    float flWidth = 2.0f / nLoopCount;
    float flHeight = 0.1f;
    for (int i = 0; i < nLoopCount; ++i)
    {
        CMeshBuilder mb;
        mb.SetCompressionType(VERTEX_COMPRESSION_NONE);
        mb.Begin(pMesh, MATERIAL_TRIANGLES, 4, 6);

        mb.Position3f(-1.0f + i * flWidth + flWidth * 0.05f, -1.0f, 0.5f);
        mb.Normal3f(0.0f, 0.0f, 1.0f);
        mb.Color4ubv(s_pColors[nCount++ % 4]);
        mb.AdvanceVertex();

        mb.Position3f(-1.0f + i * flWidth + flWidth * 0.95f, -1.0f, 0.5f);
        mb.Normal3f(0.0f, 0.0f, 1.0f);
        mb.Color4ubv(s_pColors[nCount++ % 4]);
        mb.AdvanceVertex();

        mb.Position3f(-1.0f + i * flWidth + flWidth * 0.95f, 1.0f, 0.5f);
        mb.Normal3f(0.0f, 0.0f, 1.0f);
        mb.Color4ubv(s_pColors[nCount++ % 4]);
        mb.AdvanceVertex();

        mb.Position3f(-1.0f + i * flWidth + flWidth * 0.05f, 1.0f, 0.5f);
        mb.Normal3f(0.0f, 0.0f, 1.0f);
        mb.Color4ubv(s_pColors[nCount++ % 4]);
        mb.AdvanceVertex();

        ++nCount;

        mb.FastIndex(0);
        mb.FastIndex(2);
        mb.FastIndex(1);
        mb.FastIndex(0);
        mb.FastIndex(3);
        mb.FastIndex(2);

        mb.End(true);

        pMesh->Draw();
    }

    ++nCount;

    g_pMaterialSystem->EndFrame();
    g_pMaterialSystem->SwapBuffers();

    DestroyMaterial();
}

//-----------------------------------------------------------------------------
// main application
//-----------------------------------------------------------------------------
int CMaterialSystemTestApp::Main()
{
    if (!SetMode())
        return 0;

    CMatRenderContextPtr pRenderContext = g_pMaterialSystem->GetRenderContext();

    // Sets up a full-screen viewport
    int w, h;
    pRenderContext->GetWindowSize(w, h);
    pRenderContext->Viewport(0, 0, w, h);
    pRenderContext->DepthRange(0.0f, 1.0f);

    // set camera position
    pRenderContext->MatrixMode(MATERIAL_VIEW);
    pRenderContext->LoadIdentity();
    pRenderContext->Translate(0, 0, -4.0f);

    pRenderContext->MatrixMode(MATERIAL_PROJECTION);
    pRenderContext->LoadIdentity();
    pRenderContext->PerspectiveX(45.0f, (float)w / (float)h, 0.1f, 1000.0f);

    pRenderContext->MatrixMode(MATERIAL_MODEL);
    pRenderContext->LoadIdentity();

    // Clears the screen
    g_pMaterialSystem->BeginFrame(0);
    pRenderContext->ClearColor4ub(76, 88, 68, 255);
    pRenderContext->ClearBuffers(true, true);
    g_pMaterialSystem->EndFrame();
    g_pMaterialSystem->SwapBuffers();

    SetWindowText(m_HWnd, "Buffer clearing . . hit a key");
    if (!WaitForKeypress())
        return 1;

    SetWindowText(m_HWnd, "Dynamic buffer test.. hit a key");
    TestDynamicBuffers(pRenderContext, false);
    if (!WaitForKeypress())
        return 1;

    SetWindowText(m_HWnd, "Buffered dynamic buffer test.. hit a key");
    TestDynamicBuffers(pRenderContext, true);
    if (!WaitForKeypress())
        return 1;

    return 1;
}