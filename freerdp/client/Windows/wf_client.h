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
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __WF_INTERFACE_H
#define __WF_INTERFACE_H

#include <winpr/windows.h>

#include <winpr/collections.h>

#include <freerdp/api.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/dc.h>
#include <freerdp/gdi/region.h>
#include <freerdp/cache/cache.h>
#include <freerdp/codec/color.h>

#include <freerdp/client/rail.h>
#include <freerdp/channels/channels.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/client/file.h>

typedef struct wf_context wfContext;

#include "wf_channels.h"
#include "wf_floatbar.h"
#include "wf_event.h"
#include "wf_cliprdr.h"

#ifdef __cplusplus
extern "C" {
#endif

// System menu constants
#define SYSCOMMAND_ID_SMARTSIZING 1000

struct wf_bitmap
{
	rdpBitmap _bitmap;
	HDC hdc;
	HBITMAP bitmap;
	HBITMAP org_bitmap;
	BYTE* pdata;
};
typedef struct wf_bitmap wfBitmap;

struct wf_pointer
{
	rdpPointer pointer;
	HCURSOR cursor;
};
typedef struct wf_pointer wfPointer;



// Apex {{
typedef unsigned char ex_u8;
typedef unsigned short ex_u16;
typedef unsigned int ex_u32;
typedef unsigned __int64 ex_u64;
//typedef unsigned long ex_ulong;

#pragma pack(push,1)

// 录像文件头
typedef struct TS_RECORD_HEADER
{
	ex_u32 magic;		// "TPPR" 标志 TelePort Protocol Record
	ex_u16 ver;			// 录像文件版本，目前为2
	ex_u16 protocol;	// 协议：1=RDP, 2=SSH, 3=Telnet
	ex_u64 timestamp;	// 本次录像的起始时间（UTC时间戳）
	ex_u32 packages;	// 总包数
	ex_u32 time_ms;		// 总耗时（毫秒）
	ex_u16 width;		// 初始屏幕尺寸：宽
	ex_u16 height;		// 初始屏幕尺寸：高
	ex_u16 file_count;	// 数据文件总数
	ex_u32 file_size;	// 所有数据文件的总大小（不包括每个数据文件的头，即4字节的每文件大小）
	char account[16];	// teleport账号
	char username[16];	// 远程主机用户名
	char ip[18];
	ex_u16 port;

	// RDP专有
	ex_u8 rdp_security;	// 0 = RDP, 1 = TLS

	ex_u8 reserve[128 - 4 - 2 - 2 - 8 - 4 - 4 - 2 - 2 - 2 - 4 - 16 - 16 - 18 - 2 - 1];	// 保留
}TS_RECORD_HEADER;

// 一个数据包的头
typedef struct TS_RECORD_PKG
{
	ex_u8 type;			// 包的数据类型
	ex_u32 size;		// 这个包的总大小（不含包头）
	ex_u32 time_ms;		// 这个包距起始时间的时间差（毫秒，意味着一个连接不能持续超过49天）
	ex_u8 reserve[3];	// 保留
}TS_RECORD_PKG;

typedef struct TS_RECORD_RDP_MOUSE_POS
{
	ex_u16 x;
	ex_u16 y;
}TS_RECORD_RDP_MOUSE_POS;

#pragma pack(pop)

typedef struct TS_DOWNLOADER
{
	HANDLE dlg_thread;
	DWORD dlg_thread_id;

	HANDLE download_thread;
	DWORD download_thread_id;

	HWND hwnd_dlg;
	HWND hwnd_info;
	HFONT font;
	BOOL is_shown;

	BOOL result;
	//int percent;
	ex_u32 downloaded_size;

	BOOL exit_flag;
	char filename_base[1024];
	char url_base[1024];
	char host_base[1024];
	char current_download_filename[1024];
}TS_DOWNLOADER;
// }}

struct wf_context
{
	rdpContext context;
	DEFINE_RDP_CLIENT_COMMON();

	rdpSettings* settings;

	int width;
	int height;
	int offset_x;
	int offset_y;
	int fs_toggle;
	int fullscreen;
	int percentscreen;
	char window_title[64];
	int client_x;
	int client_y;
	int client_width;
	int client_height;
	UINT32 bitmap_size;
	BYTE* bitmap_buffer;

	HANDLE keyboardThread;

	HICON icon;
	HWND hWndParent;
	HINSTANCE hInstance;
	WNDCLASSEX wndClass;
	LPCTSTR wndClassName;
	HCURSOR hDefaultCursor;

	HWND hwnd;
	POINT diff;
	HGDI_DC hdc;
	UINT16 srcBpp;
	UINT16 dstBpp;
	rdpCodecs* codecs;
	freerdp* instance;
	wfBitmap* primary;
	wfBitmap* drawing;
	HCLRCONV clrconv;
	HCURSOR cursor;
	HBRUSH brush;
	HBRUSH org_brush;
	RECT update_rect;
	RECT scale_update_rect;

	wfBitmap* tile;
	DWORD mainThreadId;
	DWORD keyboardThreadId;

	rdpFile* connectionRdpFile;

	BOOL disablewindowtracking;

	BOOL updating_scrollbars;
	BOOL xScrollVisible;
	int xMinScroll;
	int xCurrentScroll;
	int xMaxScroll;

	BOOL yScrollVisible;
	int yMinScroll;
	int yCurrentScroll;
	int yMaxScroll;

	void* clipboard;
	CliprdrClientContext* cliprdr;

	FloatBar* floatbar;

	RailClientContext* rail;
	wHashTable* railWindows;

// Apex {{
	//FILE* record_file;

	int vcursor_x;
	int vcursor_y;
	int vcursor_x_old;
	int vcursor_y_old;

	HBITMAP bmp_vcursor;
	HDC memdc_vcursor;

	HBITMAP bmp_bg;
	HDC memdc_bg;
	//BOOL show_bg;
	int bg_w;
	int bg_h;

	TS_RECORD_HEADER record_hdr;

	HANDLE record_hdr_exist_event;

//	char record_filename_base[1024];
	TS_DOWNLOADER downloader;
// 	HWND download_hwnd;
// 	HANDLE download_thread;
// 	DWORD download_thread_id;
// 	HWND download_info;
// 	HFONT download_font;
// 	BOOL download_result;
// 	int download_percent;
// 	BOOL download_exit_flag;
// 	char download_filename[1024];
// 	char download_url[1024];
// }}
};

// Apex {{
// 虚拟鼠标的图像尺寸，宽高都必须是偶数
#define VCURSOR_W 16
#define VCURSOR_H 16
// }}

/**
 * Client Interface
 */

FREERDP_API int RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints);
FREERDP_API int freerdp_client_set_window_size(wfContext* wfc, int width, int height);
FREERDP_API void wf_size_scrollbars(wfContext* wfc, UINT32 client_width, UINT32 client_height);

// Apex {{
void wf_reset(wfContext* wfc);

//DWORD WINAPI wf_download_thread(LPVOID lpParam);
//BOOL wf_create_download_box(wfContext* wfc);
BOOL wf_create_downloader(wfContext* wfc);
void wf_show_downloader(wfContext* wfc);
void wf_hide_downloader(wfContext* wfc);
// }}

#ifdef __cplusplus
}
#endif

#endif
