#include "SystemTray.h"
#include "framework.h"

CSystemTray* CSystemTray::m_pThis = NULL;

const UINT CSystemTray::m_nTimerID = 4567;
UINT CSystemTray::m_nMaxTooltipLength = 64;     // This may change...
const UINT CSystemTray::m_nTaskbarCreatedMsg = ::RegisterWindowMessage(_T("TaskbarCreated"));
HWND  CSystemTray::m_hWndInvisible;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CSystemTray::CSystemTray()
{
    Initialise();
}

CSystemTray::CSystemTray(HINSTANCE hInst,			// Handle to application instance
    HWND hParent,				// The window that will recieve tray notifications
    UINT uCallbackMessage,     // the callback message to send to parent
    LPCTSTR szToolTip,         // tray icon tooltip
    HICON icon,                // Handle to icon
    UINT uID,                  // Identifier of tray icon
    BOOL bHidden /*=FALSE*/,   // Hidden on creation?                  
    LPCTSTR szBalloonTip /*=NULL*/,    // Ballon tip (w2k only)
    LPCTSTR szBalloonTitle /*=NULL*/,  // Balloon tip title (w2k)
    DWORD dwBalloonIcon /*=NIIF_NONE*/,// Ballon tip icon (w2k)
    UINT uBalloonTimeout /*=10*/)      // Balloon timeout (w2k)
{
    Initialise();
    Create(hInst, hParent, uCallbackMessage, szToolTip, icon, uID, bHidden,
        szBalloonTip, szBalloonTitle, dwBalloonIcon, uBalloonTimeout);
}

void CSystemTray::Initialise()
{
    // If maintaining a list of all TrayIcon windows (instead of
    // only allowing a single TrayIcon per application) then add
    // this TrayIcon to the list
    m_pThis = this;

    memset(&m_tnd, 0, sizeof(m_tnd));
    m_bEnabled = FALSE;
    m_bHidden = TRUE;
    m_bRemoved = TRUE;

    m_DefaultMenuItemID = 0;
    m_DefaultMenuItemByPos = TRUE;

    m_bShowIconPending = FALSE;

    m_uIDTimer = 0;
    m_hSavedIcon = NULL;

    m_hTargetWnd = NULL;
    m_uCreationFlags = 0;

#ifdef SYSTEMTRAY_USEW2K
    OSVERSIONINFO os = { sizeof(os) };
    GetVersionEx(&os);
    m_bWin2K = (VER_PLATFORM_WIN32_NT == os.dwPlatformId && os.dwMajorVersion >= 5);
#else
    m_bWin2K = FALSE;
#endif
}

ATOM CSystemTray::RegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = (WNDPROC)WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = 0;
    wcex.hCursor = 0;
    wcex.hbrBackground = 0;
    wcex.lpszMenuName = 0;
    wcex.lpszClassName = TRAYICON_CLASS;
    wcex.hIconSm = 0;

    return RegisterClassEx(&wcex);
}

BOOL CSystemTray::Create(HINSTANCE hInst, HWND hParent, UINT uCallbackMessage,
    LPCTSTR szToolTip, HICON icon, UINT uID, BOOL bHidden /*=FALSE*/,
    LPCTSTR szBalloonTip /*=NULL*/,
    LPCTSTR szBalloonTitle /*=NULL*/,
    DWORD dwBalloonIcon /*=NIIF_NONE*/,
    UINT uBalloonTimeout /*=10*/)
{
#ifdef _WIN32_WCE
    m_bEnabled = TRUE;
#else
    // this is only for Windows 95 (or higher)
    m_bEnabled = (GetVersion() & 0xff) >= 4;
    if (!m_bEnabled)
    {
        ASSERT(FALSE);
        return FALSE;
    }
#endif

    m_nMaxTooltipLength = _countof(m_tnd.szTip);

    // Make sure we avoid conflict with other messages
    ASSERT(uCallbackMessage >= WM_APP);

    // Tray only supports tooltip text up to m_nMaxTooltipLength) characters
    ASSERT(_tcslen(szToolTip) <= m_nMaxTooltipLength);

    m_hInstance = hInst;

    RegisterClass(hInst);

    // Create an invisible window
    m_hWnd = ::CreateWindow(TRAYICON_CLASS, _T(""), WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, 0,
        hInst, 0);

    // load up the NOTIFYICONDATA structure
    m_tnd.cbSize = sizeof(NOTIFYICONDATA);
    m_tnd.hWnd = (hParent) ? hParent : m_hWnd;
    m_tnd.uID = uID;
    m_tnd.hIcon = icon;
    m_tnd.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_tnd.uCallbackMessage = uCallbackMessage;

    strncpy(m_tnd.szTip, szToolTip, m_nMaxTooltipLength);

#ifdef SYSTEMTRAY_USEW2K
    if (m_bWin2K && szBalloonTip)
    {
#if _MSC_VER < 0x1000
        // The balloon tooltip text can be up to 255 chars long.
//        ASSERT(AfxIsValidString(szBalloonTip)); 
        ASSERT(lstrlen(szBalloonTip) < 256);
#endif

        // The balloon title text can be up to 63 chars long.
        if (szBalloonTitle)
        {
            //            ASSERT(AfxIsValidString(szBalloonTitle));
            ASSERT(lstrlen(szBalloonTitle) < 64);
        }

        // dwBalloonIcon must be valid.
        ASSERT(NIIF_NONE == dwBalloonIcon || NIIF_INFO == dwBalloonIcon ||
            NIIF_WARNING == dwBalloonIcon || NIIF_ERROR == dwBalloonIcon);

        // The timeout must be between 10 and 30 seconds.
        ASSERT(uBalloonTimeout >= 10 && uBalloonTimeout <= 30);

        m_tnd.uFlags |= NIF_INFO;

        _tcsncpy(m_tnd.szInfo, szBalloonTip, 255);
        if (szBalloonTitle)
            _tcsncpy(m_tnd.szInfoTitle, szBalloonTitle, 63);
        else
            m_tnd.szInfoTitle[0] = _T('\0');
        m_tnd.uTimeout = uBalloonTimeout * 1000; // convert time to ms
        m_tnd.dwInfoFlags = dwBalloonIcon;
    }
#endif

    m_bHidden = bHidden;
    m_hTargetWnd = m_tnd.hWnd;

#ifdef SYSTEMTRAY_USEW2K    
    if (m_bWin2K && m_bHidden)
    {
        m_tnd.uFlags = NIF_STATE;
        m_tnd.dwState = NIS_HIDDEN;
        m_tnd.dwStateMask = NIS_HIDDEN;
    }
#endif

    m_uCreationFlags = m_tnd.uFlags;	// Store in case we need to recreate in OnTaskBarCreate

    BOOL bResult = TRUE;
    if (!m_bHidden || m_bWin2K)
    {
        bResult = Shell_NotifyIcon(NIM_ADD, &m_tnd);
        m_bShowIconPending = m_bHidden = m_bRemoved = !bResult;
    }

#ifdef SYSTEMTRAY_USEW2K    
    if (m_bWin2K && szBalloonTip)
    {
        // Zero out the balloon text string so that later operations won't redisplay
        // the balloon.
        m_tnd.szInfo[0] = _T('\0');
    }
#endif


    return bResult;
}

CSystemTray::~CSystemTray()
{
    RemoveIcon();
    m_IconList.clear();
    if (m_hWnd)
        ::DestroyWindow(m_hWnd);
}
