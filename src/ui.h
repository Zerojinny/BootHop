// ============================================================
//  BootHop - ui.h
//  The window toolkit on plain Win32 (ui.cpp): the dark look of the C#
//  version ("Dark Console") or a light one (SetTheme, setting "theme"),
//  DPI scaling, fonts, controls, callbacks
//  and the row layout of the tabs (Grid = the C# Grid / AddRow).
//    - containers (Panel): child windows with a background color; they
//      route the notifications of their controls to the callbacks
//      (OnClick / OnChange / OnNotify) and call their layout on WM_SIZE;
//    - buttons are owner drawn, check boxes / radio buttons, the tab
//      strip and the progress line are classes of their own;
//    - edits, combo boxes and number fields are the Windows controls
//      with the theme's colors (WM_CTLCOLOR*, SetWindowTheme).
//  Sizes are given in pixels at 96 DPI and scaled with S().
// ============================================================
#pragma once
#include "common.h"
#include <functional>
#include <initializer_list>
#include <commctrl.h>

namespace ui
{
    using std::wstring;
    typedef std::function<void()> Fn;

    extern float Scale;
    extern HINSTANCE Inst;
    extern HWND Main;                     // the main window (owner of message boxes)
    extern HFONT FontUI, FontBold, FontMono;
    inline int S(int px) { return (int)(px * Scale + (px >= 0 ? 0.5f : -0.5f)); }

    // the colors of the theme: dark by default; SetTheme(false) gives the
    // light set (before the windows are made: they keep the colors they got)
    namespace color
    {
        inline COLORREF Back = RGB(0x17, 0x19, 0x1C);       // window
        inline COLORREF Panel = RGB(0x20, 0x23, 0x28);      // fields, boxes
        inline COLORREF Button = RGB(0x2A, 0x2E, 0x34);
        inline COLORREF ButtonHover = RGB(0x34, 0x39, 0x40);
        inline COLORREF Border = RGB(0x35, 0x3A, 0x40);
        inline COLORREF Line = RGB(0x2C, 0x30, 0x35);
        inline COLORREF Footer = RGB(0x11, 0x13, 0x16);
        inline COLORREF Text = RGB(0xE6, 0xE8, 0xEB);
        inline COLORREF Dim = RGB(0xA0, 0xA6, 0xAE);
        inline COLORREF Accent = RGB(0x3D, 0xD6, 0xC6);
        inline COLORREF AccentText = RGB(0x08, 0x20, 0x1E);
        inline COLORREF AccentSoft = RGB(0x7F, 0xE3, 0xD8);
        inline COLORREF Danger = RGB(0xF4, 0x70, 0x67);
        inline COLORREF DangerText = RGB(0x1A, 0x08, 0x07);
        inline COLORREF Selected = RGB(0x1D, 0x3B, 0x39);
        inline COLORREF Strong = RGB(0xFF, 0xFF, 0xFF);     // the selected tab, the selected row
    }
    extern bool DarkMode;
    void SetTheme(bool dark);
    // the visual style of a control: "DarkMode_X" in the dark theme, "X" in the light one
    const wchar_t* ThemeApp(const wchar_t* darkName);
    HBRUSH Brush(COLORREF c);
    COLORREF Lighter(COLORREF c, float amount);

    // ---- start ----
    void Init(HINSTANCE inst);            // classes, common controls, DPI
    void InitFonts(const wstring& family);
    void Reset();                         // forgets every callback (the windows are gone)
    int MessageLoop();                    // until WM_QUIT; Tab / Enter between controls
    void DarkTitle(HWND top);             // the title bar in the theme (dark or not)

    // ---- callbacks ----
    void OnClick(HWND h, Fn f);           // buttons, check boxes, radio buttons, links
    void OnChange(HWND h, Fn f);          // text, selection, value, checked state
    void OnNotify(HWND h, std::function<LRESULT(NMHDR*)> f);
    void FireChange(HWND h);

    // ---- windows ----
    HWND TopWindow(const wstring& title, int w, int h, HWND owner, DWORD style = WS_OVERLAPPEDWINDOW);
    HWND Panel(HWND parent, COLORREF back);
    // a titled box (a thin frame, the title in the dim color); its content
    // starts GroupTop() below its top
    HWND Group(HWND parent, const wstring& title);
    int GroupTop();
    void SetBack(HWND panel, COLORREF back);
    COLORREF BackOf(HWND h);
    void SetLayout(HWND panel, std::function<void(int w, int h)> f);
    void SetPaint(HWND panel, std::function<void(HDC dc, const RECT& r)> f);
    void SetMinSize(HWND top, int w, int h);          // 96-DPI pixels of the client area (at most the screen)
    // small screens (a Windows PE without a video driver: 640 x 480, 800 x 600)
    bool FitsWorkArea(int w, int h);                  // a top window of this client size fits the screen
    void FitToWorkArea(HWND top);                     // smaller and moved, so that it is on the screen
    // a scrolling host: the inner panel is at least minW x minH (96-DPI
    // pixels); a smaller host gets scroll bars (and the mouse wheel)
    HWND ScrollHost(HWND parent, int minW, int minH, HWND& inner);
    void ScrollIntoView(HWND control);                // the scrolling host around it shows it
    void SetScrollMin(HWND inner, int minW, int minH); // another minimum for the inner panel of a host
    void OnClose(HWND top, std::function<bool()> f);  // false: the window stays
    void OnDestroy(HWND top, Fn f);
    void SetTimer(HWND w, UINT_PTR id, UINT ms, Fn f);
    void KillTimer(HWND w, UINT_PTR id);
    void Post(HWND w, Fn f);              // runs f on the thread of w (from any thread)
    void Relayout(HWND panel);

    // ---- controls ----
    HWND Label(HWND parent, const wstring& text, HFONT font = nullptr, COLORREF fore = color::Text, DWORD style = 0);
    HWND Link(HWND parent, const wstring& text);       // clickable text in the accent color
    HWND Button(HWND parent, const wstring& text);
    void Primary(HWND button, bool danger);            // filled in the accent / danger color
    HWND Edit(HWND parent, DWORD style = 0);
    HWND Combo(HWND parent, bool editable = false);
    HWND Number(HWND parent, int min, int max, int value);
    HWND Check(HWND parent, const wstring& text);
    HWND Radio(HWND parent, const wstring& text, int group);
    HWND Tabs(HWND parent);
    // a details list (dark; the selected row in the accent tone, also
    // without focus); columns: { title, width at 96 DPI }, right aligned when width < 0
    HWND List(HWND parent, const std::vector<std::pair<wstring, int>>& columns, DWORD style = LVS_SINGLESEL);
    // more notifications of a list (LVN_GETDISPINFO, LVN_COLUMNCLICK, ...); true = handled
    void OnListNotify(HWND list, std::function<bool(NMHDR*, LRESULT&)> f);
    int ListAdd(HWND list, const std::vector<wstring>& cells, bool bold = false);
    void ListClear(HWND list);
    int ListSelected(HWND list);                      // -1 = none
    void ListSelect(HWND list, int index);
    void OnListDoubleClick(HWND list, Fn f);          // OnChange: the selection
    HWND Progress(HWND parent);
    void Tooltip(HWND control, const wstring& text);

    void SetColor(HWND label, COLORREF fore);
    wstring GetText(HWND h);
    void SetText(HWND h, const wstring& text);
    void Enable(HWND h, bool on);
    void Show(HWND h, bool on);
    bool Checked(HWND h);
    void SetChecked(HWND h, bool on);
    int ComboCount(HWND h);
    int ComboIndex(HWND h);
    void ComboSelect(HWND h, int index);              // fires OnChange when it changes
    int ComboAdd(HWND h, const wstring& text);
    void ComboClear(HWND h);
    wstring ComboItem(HWND h, int index);
    int NumberValue(HWND h);
    void SetNumber(HWND h, int value);
    void NumberStep(HWND h, int step);                // the step of the arrows
    int TabAdd(HWND tabs, const wstring& text);
    int TabIndex(HWND tabs);
    void TabSelect(HWND tabs, int index);             // fires OnChange
    int TabHeight(HWND tabs);
    void ProgressValue(HWND p, int value);
    void ProgressMarquee(HWND p, bool on);

    int TextWidth(HFONT font, const wstring& text);
    int TextHeight(HFONT font);
    int CheckWidth(HWND check);                       // box + text
    void Move(HWND h, int x, int y, int w, int hgt);  // in pixels (already scaled)

    int TextHeightWrapped(HFONT font, const wstring& text, int width);

    // ---- menus ----
    struct MenuItem
    {
        int id = 0;               // 0 and no text: a separator
        wstring text, key;        // key: shown on the right ("F2")
        bool enabled = true;
    };
    // a popup menu drawn in the theme (the one of Windows stays light);
    // returns the id chosen, 0 = none
    int PopupMenu(HWND owner, POINT pt, const std::vector<MenuItem>& items);

    // ---- messages and dialogs ----
    void Info(const wstring& text, UINT icon = MB_ICONINFORMATION);
    bool YesNo(const wstring& text, UINT icon = MB_ICONQUESTION);
    // Enter / Esc in a window (IsDialogMessage sends IDOK / IDCANCEL)
    void OnKeys(HWND top, Fn ok, Fn cancel);
    // a window as a modal dialog over Main: returns the value given to EndModal (0 = closed)
    int RunModal(HWND dlg);
    void EndModal(HWND dlg, int result);
    HWND Dialog(const wstring& title, int w, int h);   // fixed size, no taskbar button, over Main

    // file dialogs (filedlg.cpp: a window of this program, no Explorer / shell
    // dialog, so they work in Windows PE); filters: { name, "*.wim;*.esd" } ...;
    // false = cancelled
    typedef std::vector<std::pair<wstring, wstring>> Filters;
    bool OpenFile(const Filters& filters, const wstring& initialDir, wstring& file);
    bool OpenFiles(const Filters& filters, const wstring& initialDir, std::vector<wstring>& files);
    bool SaveFile(const Filters& filters, const wstring& initialDir, const wstring& fileName, bool overwritePrompt, wstring& file);
    bool PickFolder(const wstring& title, const wstring& initialDir, wstring& folder);
    // one line of text (OK with a non-empty value: true)
    bool InputText(const wstring& title, const wstring& prompt, wstring& value);

    // ---- row layout of a tab (C# Grid / AddRow) ----
    // An item of a row: width (96-DPI px, 0 = the width of its text),
    // margins and height as in the C# version; fill = the rest of the row.
    struct Item
    {
        HWND h = nullptr;
        int w = 0, left = 0, top = 0, right = 0, height = 23;
        bool fill = false;
    };
    Item Field(HWND h, int w);                        // edit, combo, number: top 5, right 6
    Item Fill(HWND h);                                // a field that takes the rest of the row
    Item Btn(HWND h, int w);                          // button: 28 high, top 2, right 8
    Item Box(HWND h);                                 // check / radio: top 7, right 16
    Item Txt(HWND h, int left = 6, int right = 0);    // label sized to its text: top 8

    struct Grid
    {
        struct Row { HWND label = nullptr; std::vector<Item> items; };
        HWND parent = nullptr;
        int labelWidth = 150, rowHeight = 34;
        std::vector<Row> rows;
        explicit Grid(HWND p) : parent(p) {}
        void Add(const wstring& label, std::initializer_list<Item> items);
        // rows of rowHeight; returns the y below the last row
        int Place(int x, int y, int w);
        static void PlaceRow(const std::vector<Item>& items, int x, int y, int w);
    };
}
