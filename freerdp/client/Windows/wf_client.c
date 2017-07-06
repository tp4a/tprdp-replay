/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Windows Client
 *
 * Copyright 2009-2011 Jay Sorg
 * Copyright 2010-2011 Vic Lee
 * Copyright 2010-2011 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <winpr/windows.h>

#include <winpr/crt.h>
#include <winpr/credui.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <assert.h>
#include <sys/types.h>

// Apex {{
#include <Shlwapi.h>
// }}

#include <freerdp/log.h>
#include <freerdp/event.h>
#include <freerdp/freerdp.h>
#include <freerdp/constants.h>

#include <freerdp/codec/region.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client/channels.h>
#include <freerdp/channels/channels.h>

#include "wf_gdi.h"
#include "wf_rail.h"
#include "wf_channels.h"
#include "wf_graphics.h"
#include "wf_cliprdr.h"

#include "wf_client.h"

// Apex {{
#include "mongoose.h"
// }}

#include "resource.h"

#define TAG CLIENT_TAG("windows")

int wf_create_console(void)
{
// Apex {{
// 	if (!AllocConsole())
// 		return 1;
// 
// 	freopen("CONOUT$", "w", stdout);
// 	freopen("CONOUT$", "w", stderr);
// 	WLog_INFO(TAG,  "Debug console created.");
// }}
	return 0;
}

BOOL wf_sw_begin_paint(wfContext* wfc)
{
	rdpGdi* gdi = ((rdpContext*) wfc)->gdi;
	gdi->primary->hdc->hwnd->invalid->null = 1;
	gdi->primary->hdc->hwnd->ninvalid = 0;
	return TRUE;
}

BOOL wf_sw_end_paint(wfContext* wfc)
{
	int i;
	rdpGdi* gdi;
	int ninvalid;
	RECT updateRect;
	HGDI_RGN cinvalid;
	REGION16 invalidRegion;
	RECTANGLE_16 invalidRect;
	const RECTANGLE_16* extents;
	rdpContext* context = (rdpContext*) wfc;

	gdi = context->gdi;

	ninvalid = gdi->primary->hdc->hwnd->ninvalid;
	cinvalid = gdi->primary->hdc->hwnd->cinvalid;

// Apex {{
	//wfc->drawing->hdc
	//::Draw(  gdi->hdc
	

// 	RECT _rc;
// 	_rc.left = wfc->vcursor_x - 3;
// 	_rc.right = wfc->vcursor_x + 3;
// 	_rc.top = wfc->vcursor_y - 3;
// 	_rc.bottom = wfc->vcursor_y + 3;
// 	FillRect(gdi->primary->hdc, &_rc, GetStockObject(BLACK_BRUSH));
// 	gdi_InvalidateRegion(gdi->primary->hdc, _rc.left, _rc.top, 6, 6);
// }}

	if (ninvalid < 1)
		return TRUE;

	region16_init(&invalidRegion);

	for (i = 0; i < ninvalid; i++)
	{
		invalidRect.left = cinvalid[i].x;
		invalidRect.top = cinvalid[i].y;
		invalidRect.right = cinvalid[i].x + cinvalid[i].w;
		invalidRect.bottom = cinvalid[i].y + cinvalid[i].h;

		region16_union_rect(&invalidRegion, &invalidRegion, &invalidRect);
	}

	if (!region16_is_empty(&invalidRegion))
	{
		extents = region16_extents(&invalidRegion);

		updateRect.left = extents->left;
		updateRect.top = extents->top;
		updateRect.right = extents->right;
		updateRect.bottom = extents->bottom;

		InvalidateRect(wfc->hwnd, &updateRect, FALSE);

		if (wfc->rail)
			wf_rail_invalidate_region(wfc, &invalidRegion);
	}

	region16_uninit(&invalidRegion);
	return TRUE;
}

BOOL wf_sw_desktop_resize(wfContext* wfc)
{
	rdpGdi* gdi;
	rdpContext* context;
	rdpSettings* settings;
	freerdp* instance = wfc->instance;

	context = (rdpContext*) wfc;
	settings = wfc->instance->settings;
	gdi = context->gdi;

	wfc->width = settings->DesktopWidth;
	wfc->height = settings->DesktopHeight;

	gdi->primary->bitmap->data = NULL;
	gdi_free(instance);

	if (wfc->primary)
	{
		wf_image_free(wfc->primary);
		wfc->primary = wf_image_new(wfc, wfc->width, wfc->height, wfc->dstBpp, NULL);
	}

	if (!gdi_init(instance, CLRCONV_ALPHA | CLRBUF_32BPP, wfc->primary->pdata))
		return FALSE;

	gdi = instance->context->gdi;
	wfc->hdc = gdi->primary->hdc;

	return TRUE;
}

BOOL wf_hw_begin_paint(wfContext* wfc)
{
	wfc->hdc->hwnd->invalid->null = 1;
	wfc->hdc->hwnd->ninvalid = 0;
	return TRUE;
}

BOOL wf_hw_end_paint(wfContext* wfc)
{
	return TRUE;
}

BOOL wf_hw_desktop_resize(wfContext* wfc)
{
	BOOL same;
	RECT rect;
	rdpSettings* settings;

	settings = wfc->instance->settings;

	wfc->width = settings->DesktopWidth;
	wfc->height = settings->DesktopHeight;

	if (wfc->primary)
	{
		same = (wfc->primary == wfc->drawing) ? TRUE : FALSE;

		wf_image_free(wfc->primary);

		wfc->primary = wf_image_new(wfc, wfc->width, wfc->height, wfc->dstBpp, NULL);

		if (same)
			wfc->drawing = wfc->primary;
	}

	if (wfc->fullscreen != TRUE)
	{
		if (wfc->hwnd)
			SetWindowPos(wfc->hwnd, HWND_TOP, -1, -1, wfc->width + wfc->diff.x, wfc->height + wfc->diff.y, SWP_NOMOVE);
	}
	else
	{
		wf_update_offset(wfc);
		GetWindowRect(wfc->hwnd, &rect);
		InvalidateRect(wfc->hwnd, &rect, TRUE);
	}
	return TRUE;
}

BOOL wf_pre_connect(freerdp* instance)
{
	wfContext* wfc;
	int desktopWidth;
	int desktopHeight;
	rdpContext* context;
	rdpSettings* settings;

	context = instance->context;
	wfc = (wfContext*) instance->context;
	wfc->instance = instance;
	wfc->codecs = instance->context->codecs;

	settings = instance->settings;

	settings->OsMajorType = OSMAJORTYPE_WINDOWS;
	settings->OsMinorType = OSMINORTYPE_WINDOWS_NT;
	settings->OrderSupport[NEG_DSTBLT_INDEX] = TRUE;
	settings->OrderSupport[NEG_PATBLT_INDEX] = TRUE;
	settings->OrderSupport[NEG_SCRBLT_INDEX] = TRUE;
	settings->OrderSupport[NEG_OPAQUE_RECT_INDEX] = TRUE;
	settings->OrderSupport[NEG_DRAWNINEGRID_INDEX] = FALSE;
	settings->OrderSupport[NEG_MULTIDSTBLT_INDEX] = FALSE;
	settings->OrderSupport[NEG_MULTIPATBLT_INDEX] = FALSE;
	settings->OrderSupport[NEG_MULTISCRBLT_INDEX] = FALSE;
	settings->OrderSupport[NEG_MULTIOPAQUERECT_INDEX] = TRUE;
	settings->OrderSupport[NEG_MULTI_DRAWNINEGRID_INDEX] = FALSE;
	settings->OrderSupport[NEG_LINETO_INDEX] = TRUE;
	settings->OrderSupport[NEG_POLYLINE_INDEX] = TRUE;
	settings->OrderSupport[NEG_MEMBLT_INDEX] = TRUE;
	settings->OrderSupport[NEG_MEM3BLT_INDEX] = FALSE;
	settings->OrderSupport[NEG_SAVEBITMAP_INDEX] = FALSE;
	settings->OrderSupport[NEG_GLYPH_INDEX_INDEX] = FALSE;
	settings->OrderSupport[NEG_FAST_INDEX_INDEX] = FALSE;
	settings->OrderSupport[NEG_FAST_GLYPH_INDEX] = FALSE;
	settings->OrderSupport[NEG_POLYGON_SC_INDEX] = FALSE;
	settings->OrderSupport[NEG_POLYGON_CB_INDEX] = FALSE;
	settings->OrderSupport[NEG_ELLIPSE_SC_INDEX] = FALSE;
	settings->OrderSupport[NEG_ELLIPSE_CB_INDEX] = FALSE;

	settings->GlyphSupportLevel = GLYPH_SUPPORT_NONE;

	wfc->fullscreen = settings->Fullscreen;

	if (wfc->fullscreen)
		wfc->fs_toggle = 1;

	wfc->clrconv = (HCLRCONV) malloc(sizeof(CLRCONV));
	ZeroMemory(wfc->clrconv, sizeof(CLRCONV));

	wfc->clrconv->palette = NULL;
	wfc->clrconv->alpha = FALSE;

	if (!(instance->context->cache = cache_new(settings)))
		return FALSE;

	desktopWidth = settings->DesktopWidth;
	desktopHeight = settings->DesktopHeight;

	if (wfc->percentscreen > 0)
	{
		desktopWidth = (GetSystemMetrics(SM_CXSCREEN) * wfc->percentscreen) / 100;
		settings->DesktopWidth = desktopWidth;

		desktopHeight = (GetSystemMetrics(SM_CYSCREEN) * wfc->percentscreen) / 100;
		settings->DesktopHeight = desktopHeight;
	}

	if (wfc->fullscreen)
	{
		if (settings->UseMultimon)
		{
			desktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
			desktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		}
		else
		{
			desktopWidth = GetSystemMetrics(SM_CXSCREEN);
			desktopHeight = GetSystemMetrics(SM_CYSCREEN);
		}
	}

	/* FIXME: desktopWidth has a limitation that it should be divisible by 4,
	 *        otherwise the screen will crash when connecting to an XP desktop.*/
	desktopWidth = (desktopWidth + 3) & (~3);

	if (desktopWidth != settings->DesktopWidth)
	{
		freerdp_set_param_uint32(settings, FreeRDP_DesktopWidth, desktopWidth);
	}

	if (desktopHeight != settings->DesktopHeight)
	{
		freerdp_set_param_uint32(settings, FreeRDP_DesktopHeight, desktopHeight);
	}

	if ((settings->DesktopWidth < 64) || (settings->DesktopHeight < 64) ||
		(settings->DesktopWidth > 4096) || (settings->DesktopHeight > 4096))
	{
		WLog_ERR(TAG, "invalid dimensions %d %d", settings->DesktopWidth, settings->DesktopHeight);
		return FALSE;
	}

	freerdp_set_param_uint32(settings, FreeRDP_KeyboardLayout, (int) GetKeyboardLayout(0) & 0x0000FFFF);

	PubSub_SubscribeChannelConnected(instance->context->pubSub,
		(pChannelConnectedEventHandler) wf_OnChannelConnectedEventHandler);

	PubSub_SubscribeChannelDisconnected(instance->context->pubSub,
		(pChannelDisconnectedEventHandler) wf_OnChannelDisconnectedEventHandler);

	if (freerdp_channels_pre_connect(instance->context->channels, instance) != CHANNEL_RC_OK)
		return FALSE;

	return TRUE;
}

void wf_add_system_menu(wfContext* wfc)
{
	HMENU hMenu = GetSystemMenu(wfc->hwnd, FALSE);

	MENUITEMINFO item_info;
	ZeroMemory(&item_info, sizeof(MENUITEMINFO));

	item_info.fMask = MIIM_CHECKMARKS | MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_DATA;
	item_info.cbSize = sizeof(MENUITEMINFO);
	item_info.wID = SYSCOMMAND_ID_SMARTSIZING;
	item_info.fType = MFT_STRING;
	item_info.dwTypeData = _wcsdup(_T("Smart sizing"));
	item_info.cch = (UINT) _wcslen(_T("Smart sizing"));
	item_info.dwItemData = (ULONG_PTR) wfc;

	InsertMenuItem(hMenu, 6, TRUE, &item_info);

	if (wfc->instance->settings->SmartSizing)
	{
		CheckMenuItem(hMenu, SYSCOMMAND_ID_SMARTSIZING, MF_CHECKED);
	}
}

BOOL wf_post_connect(freerdp* instance)
{
	rdpGdi* gdi;
	DWORD dwStyle;
	rdpCache* cache;
	wfContext* wfc;
	rdpContext* context;
	WCHAR lpWindowName[64];
	rdpSettings* settings;
	EmbedWindowEventArgs e;

	settings = instance->settings;
	context = instance->context;
	wfc = (wfContext*) instance->context;
	cache = instance->context->cache;

	wfc->dstBpp = 32;
	wfc->width = settings->DesktopWidth;
	wfc->height = settings->DesktopHeight;

	if (settings->SoftwareGdi)
	{
		wfc->primary = wf_image_new(wfc, wfc->width, wfc->height, wfc->dstBpp, NULL);

		if (!gdi_init(instance, CLRCONV_ALPHA | CLRBUF_32BPP, wfc->primary->pdata))
			return FALSE;

		gdi = instance->context->gdi;
		wfc->hdc = gdi->primary->hdc;

// Apex {{
		wfc->vcursor_x = 0;
		wfc->vcursor_y = 0;
		wfc->vcursor_x_old = 0;
		wfc->vcursor_y_old = 0;

		wfc->bmp_vcursor = (HBITMAP)LoadImage(wfc->hInstance, MAKEINTRESOURCE(IDB_CURSOR), IMAGE_BITMAP, 16, 16, LR_CREATEDIBSECTION);
		wfc->memdc_vcursor = CreateCompatibleDC(wfc->primary->hdc);
		if(NULL != wfc->memdc_vcursor)
			SelectObject(wfc->memdc_vcursor, wfc->bmp_vcursor);

		wfc->bg_w = 500;
		wfc->bg_h = 360;
		wfc->bmp_bg = (HBITMAP)LoadImage(wfc->hInstance, MAKEINTRESOURCE(IDB_BG), IMAGE_BITMAP, wfc->bg_w, wfc->bg_h, LR_DEFAULTCOLOR);
		wfc->memdc_bg = CreateCompatibleDC(wfc->primary->hdc);
		if (NULL != wfc->memdc_bg)
			SelectObject(wfc->memdc_bg, wfc->bmp_bg);
// }}
	}
	else
	{
		wf_gdi_register_update_callbacks(instance->update);
		wfc->srcBpp = instance->settings->ColorDepth;
		wfc->primary = wf_image_new(wfc, wfc->width, wfc->height, wfc->dstBpp, NULL);

		if (!(wfc->hdc = gdi_GetDC()))
			return FALSE;

		wfc->hdc->bitsPerPixel = wfc->dstBpp;
		wfc->hdc->bytesPerPixel = wfc->dstBpp / 8;

		wfc->hdc->alpha = wfc->clrconv->alpha;
		wfc->hdc->invert = wfc->clrconv->invert;

		wfc->hdc->hwnd = (HGDI_WND) malloc(sizeof(GDI_WND));
		wfc->hdc->hwnd->invalid = gdi_CreateRectRgn(0, 0, 0, 0);
		wfc->hdc->hwnd->invalid->null = 1;

		wfc->hdc->hwnd->count = 32;
		wfc->hdc->hwnd->cinvalid = (HGDI_RGN) malloc(sizeof(GDI_RGN) * wfc->hdc->hwnd->count);
		wfc->hdc->hwnd->ninvalid = 0;

		if (settings->RemoteFxCodec)
		{
			wfc->tile = wf_image_new(wfc, 64, 64, 32, NULL);
		}
	}

// Apex {{
	_snwprintf(lpWindowName, ARRAYSIZE(lpWindowName), L"Teleport-RDP录像回放");//, settings->WindowTitle);
// 	if (settings->WindowTitle != NULL)
// 		_snwprintf(lpWindowName, ARRAYSIZE(lpWindowName), L"%S", settings->WindowTitle);
// 	else if (settings->ServerPort == 3389)
// 		_snwprintf(lpWindowName, ARRAYSIZE(lpWindowName), L"FreeRDP: %S", settings->ServerHostname);
// 	else
// 		_snwprintf(lpWindowName, ARRAYSIZE(lpWindowName), L"FreeRDP: %S:%d", settings->ServerHostname, settings->ServerPort);
// }}

	if (settings->EmbeddedWindow)
		settings->Decorations = FALSE;

	if (wfc->fullscreen)
		dwStyle = WS_POPUP;
	else if (!settings->Decorations)
		dwStyle = WS_CHILD | WS_BORDER;
	else
		dwStyle = WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX | WS_SIZEBOX | WS_MAXIMIZEBOX;

	if (!wfc->hwnd)
	{
		wfc->hwnd = CreateWindowEx((DWORD) NULL, wfc->wndClassName, lpWindowName, dwStyle,
			0, 0, 0, 0, wfc->hWndParent, NULL, wfc->hInstance, NULL);

		SetWindowLongPtr(wfc->hwnd, GWLP_USERDATA, (LONG_PTR) wfc);
	}

	wf_resize_window(wfc);

// Apex {{
	//wf_add_system_menu(wfc);
// }}

	BitBlt(wfc->primary->hdc, 0, 0, wfc->width, wfc->height, NULL, 0, 0, BLACKNESS);

// Apex {{
	{
		int _x = (wfc->width - wfc->bg_w) / 2;
		int _y = (wfc->height - wfc->bg_h) / 2;
		BitBlt(wfc->primary->hdc, _x, _y, wfc->bg_w, wfc->bg_h, wfc->memdc_bg, 0, 0, SRCCOPY);
	}
// }}

	wfc->drawing = wfc->primary;

	EventArgsInit(&e, "wfreerdp");
	e.embed = FALSE;
	e.handle = (void*) wfc->hwnd;
	PubSub_OnEmbedWindow(context->pubSub, context, &e);

// Apex {{
	char szWinTitle[256] = { 0 };
	if (wfc->record_hdr.port == 3389)
		_snprintf(szWinTitle, 255, "[%s] %s@%s [Teleport-RDP录像回放]", wfc->record_hdr.account, wfc->record_hdr.username, wfc->record_hdr.ip);
	else
		_snprintf(szWinTitle, 255, "[%s] %s@%s:%d [Teleport-RDP录像回放]", wfc->record_hdr.account, wfc->record_hdr.username, wfc->record_hdr.ip, wfc->record_hdr.port);
	SetWindowTextA(wfc->hwnd, szWinTitle);
// }}

	ShowWindow(wfc->hwnd, SW_SHOWNORMAL);
	UpdateWindow(wfc->hwnd);

	if (settings->SoftwareGdi)
	{
		instance->update->BeginPaint = (pBeginPaint) wf_sw_begin_paint;
		instance->update->EndPaint = (pEndPaint) wf_sw_end_paint;
		instance->update->DesktopResize = (pDesktopResize) wf_sw_desktop_resize;
	}
	else
	{
		instance->update->BeginPaint = (pBeginPaint) wf_hw_begin_paint;
		instance->update->EndPaint = (pEndPaint) wf_hw_end_paint;
		instance->update->DesktopResize = (pDesktopResize) wf_hw_desktop_resize;
	}

	pointer_cache_register_callbacks(instance->update);
	wf_register_pointer(context->graphics);

	if (!settings->SoftwareGdi)
	{
		brush_cache_register_callbacks(instance->update);
		bitmap_cache_register_callbacks(instance->update);
		offscreen_cache_register_callbacks(instance->update);
		wf_register_graphics(context->graphics);
		instance->update->BitmapUpdate = wf_gdi_bitmap_update;
	}

	if (freerdp_channels_post_connect(context->channels, instance) != CHANNEL_RC_OK)
		return FALSE;

	floatbar_window_create(wfc);

	{
		// Bring the FreeRDP main window into front.

		HWND hFWnd = GetForegroundWindow();
		AttachThreadInput(GetWindowThreadProcessId(hFWnd, NULL), GetCurrentThreadId(), TRUE);
		SetForegroundWindow(wfc->hwnd);
		BringWindowToTop(wfc->hwnd);
		SwitchToThisWindow(wfc->hwnd, TRUE);
		AttachThreadInput(GetWindowThreadProcessId(hFWnd, NULL), GetCurrentThreadId(), FALSE);
	}

	return TRUE;
}

DWORD WINAPI wf_client_thread(LPVOID lpParam)
{
	MSG msg;
	int width;
	int height;
	BOOL msg_ret;
	int quit_msg;
	DWORD nCount;
	wfContext* wfc;
	freerdp* instance;
	rdpContext* context;
	rdpChannels* channels;
	rdpSettings* settings;
	BOOL async_input;
	BOOL async_transport;

	instance = (freerdp*) lpParam;
	context = instance->context;
	wfc = (wfContext*) instance->context;

	if (!freerdp_connect(instance))
		return 0;

	channels = instance->context->channels;
	settings = instance->context->settings;

	async_input = settings->AsyncInput;
	async_transport = settings->AsyncTransport;

	while (1)
	{
		nCount = 0;

		quit_msg = FALSE;

		while (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
		{
			msg_ret = GetMessage(&msg, NULL, 0, 0);

			if (instance->settings->EmbeddedWindow)
			{
				if ((msg.message == WM_SETFOCUS) && (msg.lParam == 1))
				{
					PostMessage(wfc->hwnd, WM_SETFOCUS, 0, 0);
				}
				else if ((msg.message == WM_KILLFOCUS) && (msg.lParam == 1))
				{
					PostMessage(wfc->hwnd, WM_KILLFOCUS, 0, 0);
				}
			}

			if (msg.message == WM_SIZE)
			{
				width = LOWORD(msg.lParam);
				height = HIWORD(msg.lParam);

				SetWindowPos(wfc->hwnd, HWND_TOP, 0, 0, width, height, SWP_FRAMECHANGED);
			}

			if ((msg_ret == 0) || (msg_ret == -1))
			{
				quit_msg = TRUE;
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (quit_msg)
			break;
	}


	freerdp_disconnect(instance);
	WLog_DBG(TAG, "Main thread exited.");

	ExitThread(0);
	return 0;
}

// Apex {{
// DWORD WINAPI wf_keyboard_thread(LPVOID lpParam)
// {
// 	MSG msg;
// 	BOOL status;
// 	wfContext* wfc;
// 	HHOOK hook_handle;
// 
// 	wfc = (wfContext*) lpParam;
// 	assert(NULL != wfc);
// 
// 	hook_handle = SetWindowsHookEx(WH_KEYBOARD_LL, wf_ll_kbd_proc, wfc->hInstance, 0);
// 
// 	if (hook_handle)
// 	{
// 		while ((status = GetMessage(&msg, NULL, 0, 0)) != 0)
// 		{
// 			if (status == -1)
// 			{
// 				WLog_ERR(TAG, "keyboard thread error getting message");
// 				break;
// 			}
// 			else
// 			{
// 				TranslateMessage(&msg);
// 				DispatchMessage(&msg);
// 			}
// 		}
// 
// 		UnhookWindowsHookEx(hook_handle);
// 	}
// 	else
// 	{
// 		WLog_ERR(TAG, "failed to install keyboard hook");
// 	}
// 
// 	WLog_DBG(TAG, "Keyboard thread exited.");
// 	ExitThread(0);
// 	return (DWORD) NULL;
// }
// }}

rdpSettings* freerdp_client_get_settings(wfContext* wfc)
{
	return wfc->instance->settings;
}

int freerdp_client_focus_in(wfContext* wfc)
{
	PostThreadMessage(wfc->mainThreadId, WM_SETFOCUS, 0, 1);
	return 0;
}

int freerdp_client_focus_out(wfContext* wfc)
{
	PostThreadMessage(wfc->mainThreadId, WM_KILLFOCUS, 0, 1);
	return 0;
}

int freerdp_client_set_window_size(wfContext* wfc, int width, int height)
{
	WLog_DBG(TAG,  "freerdp_client_set_window_size %d, %d", width, height);

	if ((width != wfc->client_width) || (height != wfc->client_height))
	{
		PostThreadMessage(wfc->mainThreadId, WM_SIZE, SIZE_RESTORED, ((UINT) height << 16) | (UINT) width);
	}

	return 0;
}

void wf_size_scrollbars(wfContext* wfc, UINT32 client_width, UINT32 client_height)
{
	if (wfc->disablewindowtracking)
		return;

	// prevent infinite message loop
	wfc->disablewindowtracking = TRUE;

	if (wfc->instance->settings->SmartSizing)
	{
		wfc->xCurrentScroll = 0;
		wfc->yCurrentScroll = 0;

		if (wfc->xScrollVisible || wfc->yScrollVisible)
		{
			if (ShowScrollBar(wfc->hwnd, SB_BOTH, FALSE))
			{
				wfc->xScrollVisible = FALSE;
				wfc->yScrollVisible = FALSE;
			}
		}
	}
	else
	{
		SCROLLINFO si;
		BOOL horiz = wfc->xScrollVisible;
		BOOL vert = wfc->yScrollVisible;;

		if (!horiz && client_width < wfc->instance->settings->DesktopWidth)
		{
			horiz = TRUE;
		}
		else if (horiz && client_width >= wfc->instance->settings->DesktopWidth/* - GetSystemMetrics(SM_CXVSCROLL)*/)
		{
			horiz = FALSE;
		}

		if (!vert && client_height < wfc->instance->settings->DesktopHeight)
		{
			vert = TRUE;
		}
		else if (vert && client_height >= wfc->instance->settings->DesktopHeight/* - GetSystemMetrics(SM_CYHSCROLL)*/)
		{
			vert = FALSE;
		}

		if (horiz == vert && (horiz != wfc->xScrollVisible && vert != wfc->yScrollVisible))
		{
			if (ShowScrollBar(wfc->hwnd, SB_BOTH, horiz))
			{
				wfc->xScrollVisible = horiz;
				wfc->yScrollVisible = vert;
			}
		}

		if (horiz != wfc->xScrollVisible)
		{
			if (ShowScrollBar(wfc->hwnd, SB_HORZ, horiz))
			{
				wfc->xScrollVisible = horiz;
			}
		}

		if (vert != wfc->yScrollVisible)
		{
			if (ShowScrollBar(wfc->hwnd, SB_VERT, vert))
			{
				wfc->yScrollVisible = vert;
			}
		}

		if (horiz)
		{
			// The horizontal scrolling range is defined by
			// (bitmap_width) - (client_width). The current horizontal
			// scroll value remains within the horizontal scrolling range.
			wfc->xMaxScroll = MAX(wfc->instance->settings->DesktopWidth - client_width, 0);
			wfc->xCurrentScroll = MIN(wfc->xCurrentScroll, wfc->xMaxScroll);
			si.cbSize = sizeof(si);
			si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
			si.nMin   = wfc->xMinScroll;
			si.nMax   = wfc->instance->settings->DesktopWidth;
			si.nPage  = client_width;
			si.nPos   = wfc->xCurrentScroll;
			SetScrollInfo(wfc->hwnd, SB_HORZ, &si, TRUE);
		}

		if (vert)
		{
			// The vertical scrolling range is defined by
			// (bitmap_height) - (client_height). The current vertical
			// scroll value remains within the vertical scrolling range.
			wfc->yMaxScroll = MAX(wfc->instance->settings->DesktopHeight - client_height, 0);
			wfc->yCurrentScroll = MIN(wfc->yCurrentScroll, wfc->yMaxScroll);
			si.cbSize = sizeof(si);
			si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
			si.nMin   = wfc->yMinScroll;
			si.nMax   = wfc->instance->settings->DesktopHeight;
			si.nPage  = client_height;
			si.nPos   = wfc->yCurrentScroll;
			SetScrollInfo(wfc->hwnd, SB_VERT, &si, TRUE);
		}
	}

	wfc->disablewindowtracking = FALSE;
	wf_update_canvas_diff(wfc);
}

BOOL wfreerdp_client_global_init(void)
{
	WSADATA wsaData;

// Apex {{
// 	if (!getenv("HOME"))
// 	{
// 		char home[MAX_PATH * 2] = "HOME=";
// 		strcat(home, getenv("HOMEDRIVE"));
// 		strcat(home, getenv("HOMEPATH"));
// 		_putenv(home);
// 	}
// }}

	WSAStartup(0x101, &wsaData);

#if defined(WITH_DEBUG) || defined(_DEBUG)
	wf_create_console();
#endif

	freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
	return TRUE;
}

void wfreerdp_client_global_uninit(void)
{
	WSACleanup();
}

BOOL wfreerdp_client_new(freerdp* instance, rdpContext* context)
{
	wfContext* wfc = (wfContext*) context;

	if (!(wfreerdp_client_global_init()))
		return FALSE;

	if (!(context->channels = freerdp_channels_new()))
		return FALSE;

	instance->PreConnect = wf_pre_connect;
	instance->PostConnect = wf_post_connect;
// Apex {{
// 	instance->Authenticate = wf_authenticate;
// 	instance->GatewayAuthenticate = wf_gw_authenticate;
// 	instance->VerifyCertificate = wf_verify_certificate;
// 	instance->VerifyChangedCertificate = wf_verify_changed_certificate;
// }}

	wfc->instance = instance;
	wfc->settings = instance->settings;

// Apex {{
	wfc->downloader.hwnd_dlg = NULL;
// }}

	return TRUE;
}

void wfreerdp_client_free(freerdp* instance, rdpContext* context)
{
	if (!context)
		return;

	if (context->channels)
	{
		freerdp_channels_close(context->channels, instance);
		freerdp_channels_free(context->channels);
		context->channels = NULL;
	}

	if (context->cache)
	{
		cache_free(context->cache);
		context->cache = NULL;
	}
}

int wfreerdp_client_start(rdpContext* context)
{
	HWND hWndParent;
	HINSTANCE hInstance;
	wfContext* wfc = (wfContext*) context;
	freerdp* instance = context->instance;

	hInstance = GetModuleHandle(NULL);
	hWndParent = (HWND) instance->settings->ParentWindowId;
	instance->settings->EmbeddedWindow = (hWndParent) ? TRUE : FALSE;

	wfc->hWndParent = hWndParent;
	wfc->hInstance = hInstance;
	wfc->cursor = LoadCursor(NULL, IDC_ARROW);
	wfc->icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
	wfc->wndClassName = _tcsdup(_T("FreeRDP"));

	wfc->wndClass.cbSize = sizeof(WNDCLASSEX);
	wfc->wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wfc->wndClass.lpfnWndProc = wf_event_proc;
	wfc->wndClass.cbClsExtra = 0;
	wfc->wndClass.cbWndExtra = 0;
	wfc->wndClass.hCursor = wfc->cursor;
	wfc->wndClass.hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH);
	wfc->wndClass.lpszMenuName = NULL;
	wfc->wndClass.lpszClassName = wfc->wndClassName;
	wfc->wndClass.hInstance = hInstance;
	wfc->wndClass.hIcon = wfc->icon;
	wfc->wndClass.hIconSm = wfc->icon;
	RegisterClassEx(&(wfc->wndClass));

// Apex {{
// 	wfc->keyboardThread = CreateThread(NULL, 0, wf_keyboard_thread, (void*) wfc, 0, &wfc->keyboardThreadId);
// 
// 	if (!wfc->keyboardThread)
// 		return -1;
// }}

	if (!freerdp_client_load_addins(context->channels, instance->settings))
		return -1;

	wfc->thread = CreateThread(NULL, 0, wf_client_thread, (void*) instance, 0, &wfc->mainThreadId);

	if (!wfc->thread)
		return -1;

	return 0;
}

int wfreerdp_client_stop(rdpContext* context)
{
	wfContext* wfc = (wfContext*) context;

	if (wfc->thread)
	{
		PostThreadMessage(wfc->mainThreadId, WM_QUIT, 0, 0);

		WaitForSingleObject(wfc->thread, INFINITE);
		CloseHandle(wfc->thread);
		wfc->thread = NULL;
		wfc->mainThreadId = 0;
	}

// Apex {{
// 	if (wfc->keyboardThread)
// 	{
// 		PostThreadMessage(wfc->keyboardThreadId, WM_QUIT, 0, 0);
// 
// 		WaitForSingleObject(wfc->keyboardThread, INFINITE);
// 		CloseHandle(wfc->keyboardThread);
// 
// 		wfc->keyboardThread = NULL;
// 		wfc->keyboardThreadId = 0;
// 	}
// }}

	return 0;
}

int RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints)
{
	pEntryPoints->Version = 1;
	pEntryPoints->Size = sizeof(RDP_CLIENT_ENTRY_POINTS_V1);

	pEntryPoints->GlobalInit = wfreerdp_client_global_init;
	pEntryPoints->GlobalUninit = wfreerdp_client_global_uninit;

	pEntryPoints->ContextSize = sizeof(wfContext);
	pEntryPoints->ClientNew = wfreerdp_client_new;
	pEntryPoints->ClientFree = wfreerdp_client_free;

	pEntryPoints->ClientStart = wfreerdp_client_start;
	pEntryPoints->ClientStop = wfreerdp_client_stop;

	return 0;
}

// Apex {{
void wf_reset(wfContext* wfc)
{
	// 		wfc->instance->context->cache = cache_new(wfc->settings);
	resetRdp(wfc->instance);
	cache_free(wfc->instance->context->cache);
	wfc->instance->context->cache = NULL;

	//gdi_init(wfc->instance, CLRCONV_ALPHA | CLRBUF_32BPP, wfc->primary->pdata);
	wf_sw_desktop_resize(wfc);
}

#define WMU_DOWNLOAD_PERCENT	(WM_USER+1)

static void _download_ev_handler(struct mg_connection* nc, int ev, void* ev_data)
{
	wfContext* wfc = (wfContext*)nc->mgr->user_data;

	struct http_message* hm = (struct http_message*)ev_data;
	switch (ev)
	{
	case MG_EV_CONNECT:
		if (*(int *)ev_data != 0) {
			wfc->downloader.result = FALSE;
			wfc->downloader.exit_flag = TRUE;
		}
		break;
	case MG_EV_HTTP_REPLY:
	{
		FILE* f = fopen(wfc->downloader.current_download_filename, "wb");
		size_t wlen = fwrite(hm->body.p, 1, hm->body.len, f);
		fflush(f);
		fclose(f);
		if (wlen != hm->body.len)
		{
			DeleteFileA(wfc->downloader.current_download_filename);
			wfc->downloader.result = FALSE;
		}

		wfc->downloader.downloaded_size += (wlen - sizeof(ex_u32));

		wfc->downloader.exit_flag = TRUE;
		break;
	}
	}
}


static ex_u8 TS_RDP_RECORD_MAGIC[4] = { 'T', 'P', 'P', 'R' };

#define TIMER_DOWNLOADER_HIDE      1
#define TIMER_DOWNLOADER_PERCENT   2

// download RDP-record from server
DWORD WINAPI _download_thread(LPVOID lpParam)
{
	wfContext* wfc = (wfContext*)lpParam;

	Sleep(100);
	//Sleep(1000*20);

	wfc->downloader.downloaded_size = 0;

	char szFilename[1024] = { 0 };
	strcpy(szFilename, wfc->downloader.filename_base);
	strcat(szFilename, "\\tp-rdp.tpr");
	char szUrl[1024] = { 0 };
	strcpy(szUrl, wfc->downloader.url_base);
	strcat(szUrl, "/tp-rdp.tpr");
	OutputDebugStringA(szUrl);
	OutputDebugStringA("\n");
	struct mg_mgr mgr;
	mg_mgr_init(&mgr, wfc);
	char szHeader[1024] = { 0 };
	sprintf(szHeader, "%s", wfc->downloader.host_base);
	//sprintf(szHeader, "Host:www.baidu.com");
	if (!PathFileExistsA(szFilename))
	{
		strcpy(wfc->downloader.current_download_filename, szFilename);
		mg_connect_http(&mgr, _download_ev_handler, szUrl, szHeader, NULL);
		wfc->downloader.exit_flag = FALSE;
		while (!wfc->downloader.exit_flag)
		{
			mg_mgr_poll(&mgr, 500);
		}
	}
	if (!wfc->downloader.result)
	{
		MessageBox(NULL, _T("下载失败！"), _T("错误"), MB_OK);
		PostMessage(wfc->downloader.hwnd_dlg, WM_CLOSE, 0, 0);
		return 0;
	}

	// 可以读取基本信息了
	FILE* f = fopen(szFilename, "rb");
	//TS_RECORD_HEADER hdr;

	ex_u8* buf = NULL;
	ex_u32 buf_size = 0;
	ex_u32 pkg_size = 0;

	int size_read = 0;
	size_read = fread(&wfc->record_hdr, 1, sizeof(TS_RECORD_HEADER), f);
	fclose(f);
	if (size_read != sizeof(TS_RECORD_HEADER))
	{
		MessageBox(NULL, _T("无法读取 Teleport RDP 录像文件！"), _T("错误"), MB_OK);
		wfc->downloader.result = FALSE;
		DeleteFileA(wfc->downloader.current_download_filename);
		PostMessage(wfc->downloader.hwnd_dlg, WM_CLOSE, 0, 0);
		return 0;
	}

	if (0 != memcmp(TS_RDP_RECORD_MAGIC, &(wfc->record_hdr.magic), 4))
	{
		MessageBox(NULL, _T("文件不是 Teleport RDP 录像文件格式！"), _T("错误"), MB_OK);
		wfc->downloader.result = FALSE;
		DeleteFileA(wfc->downloader.current_download_filename);
		PostMessage(wfc->downloader.hwnd_dlg, WM_CLOSE, 0, 0);
		return 0;
	}

	// 有了基本信息，可以通知主线程继续了
	SetEvent(wfc->record_hdr_exist_event);

	// 继续下载数据文件
	int i = 0;
	for (i = 0; i < wfc->record_hdr.file_count; ++i)
	{
		char file_id[24] = { 0 };
		sprintf(file_id, "%03d", i);

		strcpy(szFilename, wfc->downloader.filename_base);
		strcat(szFilename, "\\tp-rdp.");
		strcat(szFilename, file_id);
		strcpy(szUrl, wfc->downloader.url_base);
		strcat(szUrl, "/tp-rdp.");
		strcat(szUrl, file_id);

		if (!PathFileExistsA(szFilename))
		{
			strcpy(wfc->downloader.current_download_filename, szFilename);
			mg_connect_http(&mgr, _download_ev_handler, szUrl, szHeader, NULL);
			wfc->downloader.exit_flag = FALSE;
			while (!wfc->downloader.exit_flag)
			{
				mg_mgr_poll(&mgr, 500);
			}

			if (!wfc->downloader.result)
			{
				break;
			}
		}
		else
		{
			FILE* f = fopen(szFilename, "rb");
			fseek(f, 0, SEEK_END);
			wfc->downloader.downloaded_size += (ftell(f) - sizeof(ex_u32));
			fclose(f);
		}

		PostMessage(wfc->downloader.hwnd_dlg, WMU_DOWNLOAD_PERCENT, (WPARAM)NULL, (LPARAM)NULL);
	}


	mg_mgr_free(&mgr);

	PostMessage(wfc->downloader.hwnd_dlg, WM_CLOSE, 0, 0);

	ExitThread(0);

	return 0;
}

INT_PTR CALLBACK _download_box_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	wfContext* wfc = (wfContext*)GetWindowLongPtr(hDlg, GWL_USERDATA);
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		SetWindowLongPtr(hDlg, GWL_USERDATA, lParam);
		wfc = (wfContext*)lParam;

		wfc->downloader.font = CreateFont(15, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, \
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, \
			DEFAULT_PITCH | FF_SWISS, L"Arial");
		RECT rc;
		GetClientRect(hDlg, &rc);
		wfc->downloader.hwnd_info = CreateWindow(_T("static"), _T("正在缓存 RDP 录像数据，请稍候..."), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 14, rc.top, rc.right - rc.left - 14, rc.bottom - rc.top, hDlg, NULL, wfc->hInstance, NULL);

		// need download the record file from server first.
		wfc->downloader.download_thread = CreateThread(NULL, 0, _download_thread, (void*)wfc, 0, &wfc->downloader.download_thread_id);

		if (!wfc->downloader.download_thread)
		{
			MessageBox(NULL, _T("无法下载录像数据！"), _T("错误"), MB_OK);
			wfc->downloader.result = FALSE;
			EndDialog(hDlg, 0);
		}

		//SetTimer(hDlg, 1, 1000, NULL);
	}
	return FALSE;

	case WM_TIMER:
	{
		if(TIMER_DOWNLOADER_HIDE == wParam)
		{
			KillTimer(hDlg, TIMER_DOWNLOADER_HIDE);
			ShowWindow(hDlg, SW_HIDE);
			return TRUE;
		}
		else if (TIMER_DOWNLOADER_PERCENT == wParam)
		{
			return TRUE;
		}
		//SendMessage(wfc->hStartbox_Info, WM_S, (WPARAM)wfc->hStartboxFont, TRUE);
// 		wfc->startboxTimer++;
// 		wchar_t szMsg[256] = { 0 };
// 		wsprintf(szMsg, _T("正在通过 teleport 连接远程主机，请稍候...  已等待 %d 秒"), wfc->startboxTimer);
// 		SetWindowText(wfc->hStartbox_Info, szMsg);
	}
	return FALSE;

	case WMU_DOWNLOAD_PERCENT:
	{
		wchar_t szMsg[256] = { 0 };
		int p = (int)(((float)wfc->downloader.downloaded_size / (float)wfc->record_hdr.file_size) * 100 + 0.5);
		wsprintf(szMsg, _T("正在缓存 RDP 录像数据，请稍候...  [%d%%]"), p);
		SetWindowText(wfc->downloader.hwnd_info, szMsg);
	}
	return TRUE;

	case WM_SETFONT:
		SendMessage(wfc->downloader.hwnd_info, WM_SETFONT, (WPARAM)wfc->downloader.font, TRUE);
		return TRUE;

	case WM_CLOSE:
		DeleteObject(wfc->downloader.font);
		PostMessage(hDlg, WM_QUIT, 0, 0);
		return FALSE;
	}

	return FALSE;
}

DWORD WINAPI _download_hwnd_thread(LPVOID lpParam)
{
	wfContext* wfc = (wfContext*)lpParam;

	HGLOBAL hgbl = NULL;
	LPDLGTEMPLATE lpdt = NULL;
	LPDLGITEMTEMPLATE lpdit = NULL;
	LPWORD lpw = NULL;
	LPWSTR lpwsz = NULL;
	LRESULT ret = 0;
	int nchar = 0;

	hgbl = GlobalAlloc(GMEM_ZEROINIT, 1024);
	if (!hgbl)
		return FALSE;

	lpdt = (LPDLGTEMPLATE)GlobalLock(hgbl);

	lpdt->style = WS_POPUP | WS_BORDER | WS_CAPTION | DS_MODALFRAME | DS_CENTER;
	lpdt->cdit = 0;
	lpdt->x = 0;
	lpdt->y = 0;
	lpdt->cx = 200;
	lpdt->cy = 30;

	lpw = (LPWORD)(lpdt + 1);
	*lpw++ = 0;	// no menu
	*lpw++ = 0;	// predefined dialog box class

	lpwsz = (LPWSTR)lpw;
	nchar = 1 + MultiByteToWideChar(CP_ACP, 0, "缓存 RDP 录像数据", -1, lpwsz, 50);
	lpw += nchar;

	wfc->downloader.hwnd_dlg = CreateDialogIndirectParam(wfc->hInstance, (LPDLGTEMPLATE)hgbl, NULL, _download_box_proc, (LPARAM)wfc);
	SendMessage(wfc->downloader.hwnd_dlg, WM_SETFONT, (WPARAM)NULL, (LPARAM)NULL);
	//ShowWindow(wfc->downloader.hwnd, SW_NORMAL);
	SetWindowPos(wfc->downloader.hwnd_dlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);

	GlobalFree(hgbl);

	MSG msg;
	BOOL bRet = 0;
	while ((bRet = GetMessage(&msg, wfc->downloader.hwnd_dlg, 0, 0)) != 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	DestroyWindow(wfc->downloader.hwnd_dlg);

	wfc->downloader.result = FALSE;
	wfc->downloader.exit_flag = TRUE;
	WaitForSingleObject(wfc->downloader.download_thread, INFINITE);
	SetEvent(wfc->record_hdr_exist_event);
	return TRUE;
}


BOOL wf_create_downloader(wfContext* wfc)
{
	wfc->downloader.result = TRUE;
	wfc->downloader.is_shown = FALSE;
	wfc->downloader.dlg_thread = CreateThread(NULL, 0, _download_hwnd_thread, (void*)wfc, 0, &wfc->downloader.dlg_thread_id);
	if (NULL == wfc->downloader.dlg_thread)
		return FALSE;
	return TRUE;
}

void wf_show_downloader(wfContext* wfc)
{
	if (!wfc->downloader.is_shown)
	{
		ShowWindow(wfc->downloader.hwnd_dlg, SW_NORMAL);
		wfc->downloader.is_shown = TRUE;
	}
	else
	{
		if (NULL != wfc->downloader.hwnd_dlg && IsWindow(wfc->downloader.hwnd_dlg))
			KillTimer(wfc->downloader.hwnd_dlg, TIMER_DOWNLOADER_HIDE);
	}
}

void wf_hide_downloader(wfContext* wfc)
{
	if (NULL != wfc->downloader.hwnd_dlg && IsWindow(wfc->downloader.hwnd_dlg))
		SetTimer(wfc->downloader.hwnd_dlg, TIMER_DOWNLOADER_HIDE, 1000, NULL);
}
// }}

