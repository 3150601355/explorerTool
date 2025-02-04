/*
* 文件资源管理器背景工具扩展
* 
* Author: Maple
* date: 2021-7-13 Create
* Copyright winmoes.com
*/
#include <string>
#include <vector>
#include <mutex>

//GDI 相关 Using GDI
#include <comdef.h>
#include <gdiplus.h>
#pragma comment(lib, "GdiPlus.lib")
//
//minihook
#include "MinHook.h"

#include "ShellLoader.h"

#include "WinAPI.h"
#include "HookDef.h"

//AlphaBlend
#pragma comment(lib, "Msimg32.lib")  

using namespace Gdiplus;

struct MyData
{
    HWND hWnd = 0;
    HDC hDC = 0;
    SIZE size = { 0,0 };
    int ImgIndex = 0;
};

//全局变量
#pragma region GlobalVariable

HMODULE g_hModule = NULL; // 全局模块句柄 Global module handle
bool m_isInitHook = false;

ULONG_PTR m_gdiplusToken; // GDI初始化标志 GDI Init flag

std::vector<BitmapGDI*> m_pBgBmp; // 背景图

std::vector<MyData> m_DUIList;//dui句柄列表 dui handle list

/* 0 = Left top
*  1 = Right top
*  2 = Left bottom
*  3 = Right bottom
*/
int m_ImgPosMode = 0;//图片定位方式 Image position mode type
bool m_Random = true;//随机显示图片 Random pictures
BYTE m_alpha = 255;//图片透明度

std::mutex m_mx;

#pragma endregion

extern bool InjectionEntryPoint();//注入入口点
extern void LoadSettings(bool loadimg);//加载dll设置

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH && !g_hModule) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        //防止别的程序意外加载
        wchar_t pName[MAX_PATH];
        GetModuleFileNameW(NULL, pName, MAX_PATH);
        std::wstring path = std::wstring(pName);
        if (path.substr(path.length() - 12, path.length()) == L"explorer.exe")
            InjectionEntryPoint();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        for (auto& bmp : m_pBgBmp)
        {
            delete bmp;
        }
        m_pBgBmp.clear();
        m_DUIList.clear();
    }
    return TRUE;
}

bool InjectionEntryPoint()
{
    //设置随机数种子
    srand((int)time(0));

    return true;
}

void LoadSettings(bool loadimg)
{
    std::lock_guard<std::mutex> lock(m_mx);

    //释放旧资源
    if (loadimg) {
        for (auto& pBmp : m_pBgBmp)
        {
            delete pBmp;
        }
        m_pBgBmp.clear();
    }

    //加载配置 Load config
    std::wstring cfgPath = GetCurDllDir() + L"\\config.ini";
    m_Random = GetIniString(cfgPath, L"image", L"random") == L"true" ? true : false;
    //图片定位方式
    std::wstring str = GetIniString(cfgPath, L"image", L"posType");
    if (str == L"") str = L"0";
    m_ImgPosMode = std::stoi(str);
    if (m_ImgPosMode < 0 || m_ImgPosMode > 3)
        m_ImgPosMode = 0;
    //图片透明度
    str = GetIniString(cfgPath, L"image", L"imgAlpha");
    if (str == L"")
        m_alpha = 255;
    else
    {
        int alpha = std::stoi(str);
        if (alpha > 255) alpha = 255;
        if (alpha < 0) alpha = 0;
        m_alpha = (BYTE)alpha;
    }

    //加载图像 Load Image
    if (loadimg) {
        std::wstring imgPath = GetCurDllDir() + L"\\Image";
        if (FileIsExist(imgPath))
        {
            std::vector<std::wstring> fileList;
            EnumFiles(imgPath, L"*.png", fileList);
            EnumFiles(imgPath, L"*.jpg", fileList);

            if (fileList.size() == 0) {
                MessageBoxW(0, L"文件资源管理器背景目录没有文件，因此扩展不会有任何效果.", L"缺少图片目录", MB_ICONERROR);
                return;
            }

            for (size_t i = 0; i < fileList.size(); i++)
            {
                BitmapGDI* bmp = new BitmapGDI(fileList[i]);
                if (bmp->src)
                    m_pBgBmp.push_back(bmp);
                else
                    delete bmp;//图片加载失败 load failed
                /*非随机 只加载一张
                * Load only one image non randomly
                */
                if (!m_Random) break;
            }
        }
        else {
            MessageBoxW(0, L"文件资源管理器背景目录不存在，因此扩展不会有任何效果.", L"缺少图片目录", MB_ICONERROR);
            return;
        }
    }
}

/*
* ShellLoader
* 文件资源管理器创建窗口时会调用 我们可以在这里更新一下配置
*/
void OnWindowLoad()
{
    //如果按住ESC键则不加载扩展
    if (GetKeyState(VK_ESCAPE) < 0)
        return;
    //在开机的时候系统就会加载此动态库 那时候启用HOOK是会失败的 等创建窗口的时候再初始化HOOK
    if (!m_isInitHook)
    {
        //初始化 Gdiplus Init GdiPlus
        GdiplusStartupInput StartupInput;
        int ret = GdiplusStartup(&m_gdiplusToken, &StartupInput, NULL);

        //创建钩子 CreateHook
        if (MH_Initialize() == MH_OK)
        {
            CreateMHook(CreateWindowExW, MyCreateWindowExW, _CreateWindowExW_, 1);
            CreateMHook(DestroyWindow, MyDestroyWindow, _DestroyWindow_, 2);
            CreateMHook(BeginPaint, MyBeginPaint, _BeginPaint_, 3);
            CreateMHook(FillRect, MyFillRect, _FillRect_, 4);
            CreateMHook(CreateCompatibleDC, MyCreateCompatibleDC, _CreateCompatibleDC_, 5);
            MH_EnableHook(MH_ALL_HOOKS);
        }
        else
        {
            MessageBoxW(0, L"Failed to initialize disassembly!\nSuspected duplicate load extension", L"MTweaker Error", MB_ICONERROR | MB_OK);
            FreeLibraryAndExitThread(g_hModule, 0);
        }
        m_isInitHook = true;
    }
    LoadSettings(true);
}

HWND MyCreateWindowExW(
    DWORD     dwExStyle,
    LPCWSTR   lpClassName,
    LPCWSTR   lpWindowName,
    DWORD     dwStyle,
    int       X,
    int       Y,
    int       nWidth,
    int       nHeight,
    HWND      hWndParent,
    HMENU     hMenu,
    HINSTANCE hInstance,
    LPVOID    lpParam
)
{
    HWND hWnd = _CreateWindowExW_(dwExStyle, lpClassName, lpWindowName, dwStyle,
        X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    std::wstring ClassName;
    if (hWnd)
    {
        ClassName = GetWindowClassName(hWnd);
    }

    //explorer window 
    if (ClassName == L"DirectUIHWND"
        && GetWindowClassName(hWndParent) == L"SHELLDLL_DefView")
    {
        //继续查找父级 Continue to find parent
        HWND parent = GetParent(hWndParent);
        if (GetWindowClassName(parent) == L"ShellTabWindowClass")
        {
            //记录到列表中 Add to list
            MyData data;
            data.hWnd = hWnd;
            if (m_Random && m_pBgBmp.size())
            {
                data.ImgIndex = rand() % m_pBgBmp.size();
            }
            m_DUIList.push_back(data);
        }
    }
    return hWnd;
}

BOOL MyDestroyWindow(HWND hWnd)
{
    //查找并删除列表中的记录 Find and remove from list
    for (size_t i = 0; i < m_DUIList.size(); i++)
    {
        if (m_DUIList[i].hWnd == hWnd)
        {
            //ReleaseDC(hWnd, DUIList[i].second);
            m_DUIList.erase(m_DUIList.begin() + i);
            break;
        }
    }
    return _DestroyWindow_(hWnd);
}

HDC MyBeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
{
    //开始绘制DUI窗口 BeginPaint dui window
    HDC hDC = _BeginPaint_(hWnd, lpPaint);
    for (auto& ui : m_DUIList)
    {
        if (ui.hWnd == hWnd)
        {
            //Log(L"Begin");
            //记录到列表 Record values to list
            ui.hDC = hDC;
            break;
        }
    }
    return hDC;
}

int MyFillRect(HDC hDC, const RECT* lprc, HBRUSH hbr)
{
    int ret = _FillRect_(hDC, lprc, hbr);
    for (auto& ui : m_DUIList)
    {
        if (ui.hDC == hDC && m_pBgBmp.size())
        {
            int size[2] = { lprc->right - lprc->left, lprc->bottom - lprc->top };
            RECT pRc;
            GetWindowRect(ui.hWnd, &pRc);
            SIZE wndSize = { pRc.right - pRc.left, pRc.bottom - pRc.top };

            /*因图片定位方式不同 如果窗口大小改变 需要全体重绘 否则有残留
            * Due to different image positioning methods,
            * if the window size changes, you need to redraw, otherwise there will be residues*/
            if ((ui.size.cx != wndSize.cx
                || ui.size.cy != wndSize.cy) && m_ImgPosMode != 0)
                InvalidateRect(ui.hWnd, 0, TRUE);

            /*裁剪矩形 Clip rect*/
            SaveDC(hDC);
            IntersectClipRect(hDC, lprc->left, lprc->top, lprc->right, lprc->bottom);

            BitmapGDI* pBgBmp = m_pBgBmp[ui.ImgIndex];

            //计算图片位置 Calculate picture position
            POINT pos;
            switch (m_ImgPosMode)
            {
            case 0:
                pos = { 0, 0 };
                break;
            case 1:
                pos.x = wndSize.cx - pBgBmp->Size.cx;
                pos.y = 0;
                break;
            case 2:
                pos.x = 0;
                pos.y = wndSize.cy - pBgBmp->Size.cy;
                break;
            case 3:
                pos.x = wndSize.cx - pBgBmp->Size.cx;
                pos.y = wndSize.cy - pBgBmp->Size.cy;
                break;
            default:
                break;
            }

            /*绘制图片 Paint image*/
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, m_alpha, AC_SRC_ALPHA };
            AlphaBlend(hDC, pos.x, pos.y, pBgBmp->Size.cx, pBgBmp->Size.cy, pBgBmp->pMem, 0, 0, pBgBmp->Size.cx, pBgBmp->Size.cy, bf);

            RestoreDC(hDC, -1);

            ui.size = wndSize;

            //Log(L"DrawImage");
            break;
        }
    }
    return ret;
}

HDC MyCreateCompatibleDC(HDC hDC)
{
    //在绘制DUI之前 会调用CreateCompatibleDC 找到它
    //CreateCompatibleDC is called before drawing the DUI
    HDC retDC = _CreateCompatibleDC_(hDC);
    for (auto& ui : m_DUIList)
    {
        if (ui.hDC == hDC)
        {
            ui.hDC = retDC;
            break;
        }
    }
    return retDC;
}
