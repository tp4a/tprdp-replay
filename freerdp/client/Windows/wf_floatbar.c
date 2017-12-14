/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Windows Float Bar
 *
 * Copyright 2013 Zhang Zhaolong <zhangzl2013@126.com>
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

#include <winpr/crt.h>
#include <winpr/windows.h>

#include <Shlwapi.h>

#include "resource.h"

#include "wf_client.h"
#include "wf_floatbar.h"
#include "wf_gdi.h"


typedef struct _Button Button;

/* TIMERs */
#define TIMER_HIDE          1
#define TIMER_ANIMAT_SHOW   2
#define TIMER_ANIMAT_HIDE   3

/* Button Type */
#define BUTTON_PLAY         0
#define BUTTON_STOP         1
#define BUTTON_SPEED        2
#define BUTTON_PROGRESS     3
#define BTN_MAX             4

#define BUTTON_Y            6
#define BUTTON_WIDTH        18
#define BUTTON_HEIGHT       20

/* bmp size */
#define BACKGROUND_W        560
#define BACKGROUND_H        48
#define BTN_PLAY_X          13
#define BTN_STOP_X          (BTN_PLAY_X + BUTTON_WIDTH + 8) //(BACKGROUND_W - 91)
#define BTN_SPEED_X         (BTN_STOP_X + BUTTON_WIDTH + 50) //  (BACKGROUND_W - 64)
#define BTN_SPEED_W         56

#define TXT_SPEED_X         (BTN_SPEED_X+BTN_SPEED_W+5)
#define TXT_SPEED_Y         (BUTTON_Y+3)

#define PROGRESS_X      15//  (BACKGROUND_W - 37)
#define PROGRESS_Y		37
#define PROGRESS_W      531//  (BACKGROUND_W - 37)
#define PROGRESS_H		4

#define BTN_PROGRESS_W      12
#define BTN_PROGRESS_H		12
#define BTN_PROGRESS_X      (PROGRESS_X - (BTN_PROGRESS_W/2))
#define BTN_PROGRESS_Y		(PROGRESS_Y + (PROGRESS_H/2) - (BTN_PROGRESS_H/2))


#define TIMEOUT_BEFORE_HIDE		3000

struct _Button {
	FloatBar* floatbar;
	int type;
	int x, y, h, w;
	int active;
	HBITMAP bmp_normal;
	HBITMAP bmp_hover;

	/* Play Specified */
	HBITMAP bmp_pause;
	HBITMAP bmp_pause_hover;
	HBITMAP bmp_play;
	HBITMAP bmp_play_hover;

	// For progress
	int progress_x, progress_y, progress_w, progress_h;	// 底部进度条的位置和大小
	HBITMAP bmp_progress_played_left;
	int progress_played_left_w, progress_played_left_h;
	HBITMAP bmp_progress_played_bg;
	int progress_played_bg_w, progress_played_bg_h;
};

struct _FloatBar {
	HWND parent;
	HWND hwnd;
	RECT rect;
	LONG width;
	LONG height;
	wfContext* wfc;
	Button* buttons[BTN_MAX];
	Button* btn_play;
	Button* btn_progress;
	BOOL shown;

	BOOL stop_flag;
	BOOL playing;

	BOOL playing_before_drag_progress;
	int progress_x_before_drag;

	HDC memdc;
	HDC tmp_memdc;
	HDC memdc_bg;
	HBITMAP background;
	HBITMAP bg_tmp;

	HFONT hFontNormal;
	HFONT hFontMono;

	HMENU hSpeedMenu;
	int speed;


	HANDLE thread_handle;
	UINT thread_id;

	BOOL need_exit;
// 	BOOL need_reset_wfc;

	ex_u32 time_total;
	ex_u32 time_played;
	wchar_t sz_time_total[24];
	wchar_t sz_time_current[24];

	int percent;
};

static void _ms_to_str(FloatBar* floatbar, BOOL is_total, ex_u32 ms, wchar_t str[24])
{
	wchar_t _str[24] = { 0 };
	memset(_str, 0, sizeof(wchar_t) * 24);

	int _tmp = ms % 1000;
	int sec = ms / 1000;
	if (_tmp > 500)
		sec += 1;

	int h = sec / (3600);
	sec -= h * 3600;
	int m = sec / 60;
	sec -= m * 60;
	int s = sec;
	if (h > 99)
		h = 99;

	BOOL need_update = FALSE;
	if (is_total)
	{
		_snwprintf(_str, 24, L" / %02d:%02d:%02d", h, m, s);
		if (0 != lstrcmp(_str, floatbar->sz_time_total))
		{
			need_update = TRUE;
			lstrcpy(floatbar->sz_time_total, _str);
		}
	}
	else
	{
		_snwprintf(_str, 24, L"%02d:%02d:%02d", h, m, s);
		if (0 != lstrcmp(_str, floatbar->sz_time_current))
		{
			need_update = TRUE;
			lstrcpy(floatbar->sz_time_current, _str);
		}
	}

	if (need_update)
	{
		InvalidateRect(floatbar->hwnd, NULL, FALSE);
		UpdateWindow(floatbar->hwnd);
	}
}

static void _play_package(wfContext* wfc, TS_RECORD_PKG* pkg, const ex_u8* data, size_t size)
{
	if (pkg->type == 0x11)	// 服务端返回的数据包，可用于展示
	{
		if (!playRecord(wfc->instance, data, size))
			return;
	}
	else if (pkg->type == 0x10)	// 鼠标位置变化，需要绘制一个虚拟鼠标
	{
		if (size != sizeof(TS_RECORD_RDP_MOUSE_POS))
			return;
		TS_RECORD_RDP_MOUSE_POS* pos = (TS_RECORD_RDP_MOUSE_POS*)data;

		wfc->vcursor_x = pos->x;
		wfc->vcursor_y = pos->y;

		RECT _rc_old;
		_rc_old.left = wfc->vcursor_x_old - (VCURSOR_W/2);
		_rc_old.right = wfc->vcursor_x_old + (VCURSOR_W / 2);
		_rc_old.top = wfc->vcursor_y_old - (VCURSOR_H / 2);
		_rc_old.bottom = wfc->vcursor_y_old + (VCURSOR_H / 2);
		RECT _rc_new;
		_rc_new.left = wfc->vcursor_x - (VCURSOR_W / 2);
		_rc_new.right = wfc->vcursor_x + (VCURSOR_W / 2);
		_rc_new.top = wfc->vcursor_y - (VCURSOR_H / 2);
		_rc_new.bottom = wfc->vcursor_y + (VCURSOR_H / 2);

		InvalidateRect(wfc->hwnd, &_rc_old, FALSE);
		InvalidateRect(wfc->hwnd, &_rc_new, FALSE);
	}
}

static void* _player_thread(void* arg)
{
	FloatBar* floatbar = (FloatBar*)arg;

	//TS_RECORD_HEADER hdr;
	TS_RECORD_PKG pkg;

	ex_u8* buf_data = NULL;
	ex_u32 buf_size = 0;
	BOOL buf_played = FALSE;

	wfContext* wfc = floatbar->wfc;

	floatbar->time_total = wfc->record_hdr.info.time_ms;

	BOOL play_end = FALSE;

	FILE* f = NULL;
	char szFilename[1024] = { 0 };
	int file_size = 0;
	strcpy(szFilename, wfc->downloader.filename_base);
	strcat(szFilename, "\\tp-rdp.dat");

	for (;;)
	{
		buf_played = FALSE;
		if (NULL != buf_data)
		{
			free(buf_data);
			buf_data = NULL;
		}
		buf_size = 0;
		play_end = FALSE;

		if (NULL != f)
		{
			fclose(f);
			f = NULL;
		}

		floatbar->time_played = 0;

		_ms_to_str(floatbar, TRUE, floatbar->time_total,  floatbar->sz_time_total);
		_ms_to_str(floatbar, FALSE, 0, floatbar->sz_time_current);


		if (NULL == f)
		{
			// wait until file exists (downloaded)
			if (!wfc->downloader.data_file_downloaded)
			{
				wf_show_downloader(wfc);
				do {
					Sleep(500);
					if (floatbar->need_exit)
						break;
				} while (!wfc->downloader.data_file_downloaded);

				wf_hide_downloader(wfc);

				if (floatbar->need_exit)
					break;
			}

			f = fopen(szFilename, "rb");
			fseek(f, 0, SEEK_END);
			file_size = ftell(f);
			if (file_size < sizeof(TS_RECORD_PKG))
			{
				// Invalide RDP-record data file.
				floatbar->need_exit = TRUE;
				break;
			}

		}
		fseek(f, 0, SEEK_SET);

		ex_u32 pkg_played = 0;

		DWORD t_start = 0;
		DWORD t_now = 0;

		for (;;)
		{
			if (floatbar->need_exit || floatbar->stop_flag)
				break;

			if (!floatbar->playing)
			{
				Sleep(100);
				continue;
			}

			if (floatbar->need_exit || floatbar->stop_flag)
				break;

			t_start = GetTickCount();

			Sleep(40);

			t_now = GetTickCount();
			floatbar->time_played += t_now - t_start;
			t_start = t_now;

			_ms_to_str(floatbar, false, floatbar->time_played * floatbar->speed, floatbar->sz_time_current);

			// 计算进度条百分比
			int p = floatbar->time_played * floatbar->speed * 100 / floatbar->time_total;
			if (p != floatbar->percent)
			{
				floatbar->btn_progress->x = BTN_PROGRESS_X + (PROGRESS_W*p / 100);
				floatbar->percent = p;
				InvalidateRect(floatbar->hwnd, NULL, FALSE);
				UpdateWindow(floatbar->hwnd);
			}


			if (!buf_played && buf_data)
			{
				// 有已经读出的数据，且尚未播放
				if (pkg.time_ms <= floatbar->time_played * floatbar->speed)
				{
					// 播放这包数据
					_play_package(wfc, &pkg, buf_data, pkg.size);
					buf_played = TRUE;
					pkg_played++;

					if (pkg_played >= wfc->record_hdr.info.packages)
					{
						play_end = TRUE;
						break;
					}
				}
				else
				{
					continue;
				}
			}

			for (;;)
			{
// 				if (NULL == f)
// 				{
// //					sprintf(file_id, "%03d", file_count);
// 					char szFilename[1024] = { 0 };
// 					strcpy(szFilename, wfc->downloader.filename_base);
// 					strcat(szFilename, "\\tp-rdp.dat");
// //					strcat(szFilename, file_id);
// 
// 					// wait until file exists (downloaded)
// 					if (!PathFileExistsA(szFilename))
// 					{
// 						wf_show_downloader(wfc);
// 						do {
// 							Sleep(500);
// 							if (floatbar->need_exit)
// 								break;
// 						}// while (!PathFileExistsA(szFilename));
// 						while (wfc->downloader.downloaded_size < wfc->downloader.file_size);
// 
// 						wf_hide_downloader(wfc);
// 					}
// 
// 					if (floatbar->need_exit)
// 						break;
// 
// 					f = fopen(szFilename, "rb");
// 					fseek(f, 0, SEEK_END);
// 					int _file_size = ftell(f);
// 					if (_file_size < 4)
// 					{
// 						// Invalide RDP-record data file.
// 						floatbar->need_exit = TRUE;
// 						break;
// 					}
// 
// 					fseek(f, 0, SEEK_SET);
// 					ex_u32 file_size = 0;
// 					size_read = fread(&file_size, 1, sizeof(ex_u32), f);
// 					if (size_read != sizeof(ex_u32))
// 					{
// 						floatbar->need_exit = TRUE;
// 						break;
// 					}
// 
// 					if (file_size != _file_size - sizeof(ex_u32))
// 					{
// 						floatbar->need_exit = TRUE;
// 						break;
// 					}
// 				}

				if (floatbar->need_exit)
					break;

				int size_read = fread(&pkg, 1, sizeof(TS_RECORD_PKG), f);
				if (size_read != sizeof(TS_RECORD_PKG))
				{
					fclose(f);
					f = NULL;

					play_end = TRUE;

					// 计算进度条百分百
					int p = floatbar->time_played * floatbar->speed * 100 / floatbar->time_total;
					if (p != floatbar->percent)
					{
						//WLog_ERR("percent", "%d%%, time:%d/%d, pkg:%d/%d", p, floatbar->time_played * floatbar->speed, hdr.time_ms, pkg_played, hdr.packages);
						floatbar->btn_progress->x = BTN_PROGRESS_X + (PROGRESS_W*p / 100);
						floatbar->percent = p;
					}

					break;
				}

				if (NULL == buf_data || buf_size < pkg.size)
				{
					if (NULL != buf_data)
						free(buf_data);
					buf_data = (ex_u8*)calloc(1, pkg.size);
					buf_size = pkg.size;
					if (NULL == buf_data)
						break;
				}

				size_read = fread(buf_data, 1, pkg.size, f);
				if (size_read != pkg.size)
					break;

				buf_played = FALSE;

				// 读取到一包数据
				if (pkg.time_ms <= floatbar->time_played * floatbar->speed)
				{
					// 播放这包数据
					_play_package(wfc, &pkg, buf_data, pkg.size);
					buf_played = TRUE;
					pkg_played++;

					if (pkg_played >= wfc->record_hdr.info.packages)
					{
						play_end = TRUE;
						break;
					}
				}
				else
				{
					break;
				}
			}

			if (play_end)
				break;
		}

		if (play_end || floatbar->stop_flag)
		{
			floatbar->stop_flag = FALSE;

			floatbar->btn_play->bmp_normal = floatbar->btn_play->bmp_play;
			floatbar->btn_play->bmp_hover = floatbar->btn_play->bmp_play_hover;

			floatbar->playing = FALSE;// ~floatbar->playing;
			InvalidateRect(floatbar->hwnd, NULL, FALSE);
			UpdateWindow(floatbar->hwnd);

			// 等待下一次播放，或者程序退出
			for (;;)
			{
				if (floatbar->need_exit)
					break;
				if (!floatbar->playing)
				{
					Sleep(100);
					continue;
				}

				wf_reset(wfc);
				break;
			}

		}

		if (floatbar->need_exit)
			break;
	}

	if (NULL != buf_data)
		free(buf_data);

	if (NULL != f)
		fclose(f);

	ExitThread(0);

	return NULL;
}

BOOL _player_thread_start(FloatBar* floatbar)
{
	floatbar->thread_handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)_player_thread, floatbar, 0, &floatbar->thread_id);
	if (floatbar->thread_handle == NULL)
	{
		WLog_ERR("...", "Failed to create play thread.");
		return FALSE;
	}

	return TRUE;
}

static int button_hit(Button* button, int pos_x, int pos_y)
{
	FloatBar* floatbar = button->floatbar;

	switch (button->type)
	{
		case BUTTON_PLAY:
			if (!floatbar->playing)
			{
				button->bmp_normal = button->bmp_pause;
				button->bmp_hover = button->bmp_pause_hover;
			}
			else
			{
				button->bmp_normal = button->bmp_play;
				button->bmp_hover = button->bmp_play_hover;
			}

			floatbar->playing = ~floatbar->playing;
			InvalidateRect(button->floatbar->hwnd, NULL, FALSE);
			UpdateWindow(button->floatbar->hwnd);
			break;

		case BUTTON_STOP:
			if(!floatbar->stop_flag)
				floatbar->stop_flag = TRUE;
			break;

		case BUTTON_SPEED:
		{
			POINT pt = {pos_x, pos_y};
			ClientToScreen(floatbar->hwnd, &pt);
			TrackPopupMenu(floatbar->hSpeedMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON, pt.x, pt.y, 0, floatbar->hwnd, NULL);
		}
			break;

		case BUTTON_PROGRESS:
			//SendMessage(floatbar->parent, WM_DESTROY, 0 , 0);
			break;

		default:
			return 0;
	}

	return 0;
}

static int button_paint(Button* button, HDC hdc)
{
	FloatBar* floatbar = button->floatbar;
	HBITMAP old_bmp = NULL;

	if (button->type == BUTTON_PROGRESS)
	{
		// 需要先绘制进度条的背景
		old_bmp = SelectObject(floatbar->tmp_memdc, button->bmp_progress_played_left);
		BitBlt(hdc, button->progress_x, button->progress_y, button->progress_played_left_w, button->progress_played_left_h, floatbar->tmp_memdc, 0, 0, SRCCOPY);
		SelectObject(floatbar->tmp_memdc, old_bmp);

		if (button->x > PROGRESS_X + button->progress_played_left_w)
		{
			old_bmp = SelectObject(floatbar->tmp_memdc, button->bmp_progress_played_bg);
			StretchBlt(hdc,
				button->progress_x+ button->progress_played_left_w, button->progress_y,
				button->x - PROGRESS_X - button->progress_played_left_w, PROGRESS_H,
				floatbar->tmp_memdc, 0, 0, button->progress_played_bg_w, button->progress_played_bg_h, SRCCOPY);
			SelectObject(floatbar->tmp_memdc, old_bmp);
		}
	}

	old_bmp = SelectObject(floatbar->tmp_memdc, button->active ? button->bmp_hover : button->bmp_normal);
	StretchBlt(hdc, button->x, button->y, button->w, button->h, floatbar->tmp_memdc, 0, 0, button->w, button->h, SRCCOPY);
	SelectObject(floatbar->tmp_memdc, old_bmp);

	return 0;
}

static Button* floatbar_create_button(FloatBar* floatbar, int type, int resid, int resid_act, int x, int y, int w, int h)
{
	Button *button;

	button = (Button *)calloc(sizeof(Button), 1);

	if (!button)
		return NULL;

	button->floatbar = floatbar;
	button->type = type;
	button->x = x;
	button->y = y;
	button->w = w;
	button->h = h;
	button->active = FALSE;

	button->bmp_normal = (HBITMAP)LoadImage(floatbar->wfc->hInstance, MAKEINTRESOURCE(resid), IMAGE_BITMAP, w, h, LR_DEFAULTCOLOR);
	button->bmp_hover = (HBITMAP)LoadImage(floatbar->wfc->hInstance, MAKEINTRESOURCE(resid_act), IMAGE_BITMAP, w, h, LR_DEFAULTCOLOR);

	return button;
}

static Button* floatbar_create_play_button(FloatBar* floatbar,
									int resid_play, int resid_play_hover,
									int resid_pause, int resid_pause_hover,
									int x, int y, int w, int h)
{
	Button* button;

	button = floatbar_create_button(floatbar, BUTTON_PLAY, resid_play, resid_play_hover, x, y, w, h);

	if (!button)
		return NULL;

	button->bmp_play = button->bmp_normal;
	button->bmp_play_hover = button->bmp_hover;
	button->bmp_pause = (HBITMAP)LoadImage(floatbar->wfc->hInstance, MAKEINTRESOURCE(resid_pause), IMAGE_BITMAP, w, h, LR_DEFAULTCOLOR);
	button->bmp_pause_hover = (HBITMAP)LoadImage(floatbar->wfc->hInstance, MAKEINTRESOURCE(resid_pause_hover), IMAGE_BITMAP, w, h, LR_DEFAULTCOLOR);

	return button;
}

static Button* floatbar_create_progress_button(FloatBar* floatbar,
	int resid_normal, int resid_hover,
	int x, int y, int h, int w,
	int px, int py, int ph, int pw,
	int resid_played_left,
	int played_left_w, int played_left_h,
	int resid_played_bg,
	int played_bg_w, int played_bg_h
)
{
	Button* button;

	button = floatbar_create_button(floatbar, BUTTON_PROGRESS, resid_normal, resid_hover, x, y, w, h);

	if (!button)
		return NULL;

	button->progress_x = px;
	button->progress_y = py;
	button->progress_w = pw;
	button->progress_h = ph;

	button->progress_played_left_w = played_left_w;
	button->progress_played_left_h = played_left_h;
	button->progress_played_bg_w = played_bg_w;
	button->progress_played_bg_h = played_bg_h;

	button->bmp_progress_played_left = (HBITMAP)LoadImage(floatbar->wfc->hInstance, MAKEINTRESOURCE(resid_played_left), IMAGE_BITMAP, played_left_w, played_left_h, LR_DEFAULTCOLOR);
	button->bmp_progress_played_bg = (HBITMAP)LoadImage(floatbar->wfc->hInstance, MAKEINTRESOURCE(resid_played_bg), IMAGE_BITMAP, played_bg_w, played_bg_h, LR_DEFAULTCOLOR);

	return button;
}

static Button* floatbar_get_button(FloatBar* floatbar, int x, int y)
{
	int i;

	for (i = 0; i < BTN_MAX; i++)
	{
		if (
			x >= floatbar->buttons[i]->x && x <= floatbar->buttons[i]->x + floatbar->buttons[i]->w &&
			y >= floatbar->buttons[i]->y && y <= floatbar->buttons[i]->y + floatbar->buttons[i]->h
			)
		{
			return floatbar->buttons[i];
		}
	}

	return NULL;
}

static void floatbar_check_paint(FloatBar* floatbar, int pos_x, int pos_y)
{
	Button* button;
	BOOL need_paint = FALSE;
	int i = 0;

	Button* old_active_btn = NULL;

	for (i = 0; i < BTN_MAX; i++)
	{
		if (floatbar->buttons[i]->active)
		{
			old_active_btn = floatbar->buttons[i];
		}
	}

	if (pos_x < 0 || pos_y < 0)
	{
		if (old_active_btn != NULL)
		{
			old_active_btn->active = FALSE;
			need_paint = TRUE;
		}
	}
	else
	{
		button = floatbar_get_button(floatbar, pos_x, pos_y);

		if (button)
		{
			if (button != old_active_btn)
			{
				button->active = TRUE;
				need_paint = TRUE;

				if (old_active_btn != NULL)
				{
					old_active_btn->active = FALSE;
					need_paint = TRUE;
				}
			}
		}
		else
		{
			if (old_active_btn != NULL)
			{
				old_active_btn->active = FALSE;
				need_paint = TRUE;
			}
		}
	}

	if (need_paint)
	{
		InvalidateRect(floatbar->hwnd, NULL, FALSE);
		UpdateWindow(floatbar->hwnd);
	}
}

static int floatbar_paint(FloatBar* floatbar, HDC hdc)
{
	int i;

	/* paint background */
	StretchBlt(hdc, 0, 0, BACKGROUND_W, BACKGROUND_H, floatbar->memdc_bg, 0, 0, BACKGROUND_W, BACKGROUND_H, SRCCOPY);

	static t = 0;

	/* paint buttons */
	for (i = 0; i < BTN_MAX; i++)
	{
		button_paint(floatbar->buttons[i], hdc);
	}

	// 绘制速度说明
	RECT rc;
	rc.left = TXT_SPEED_X;
	rc.top = TXT_SPEED_Y;
	rc.right = rc.left + 80;
	rc.bottom = rc.top + BUTTON_HEIGHT;

	//LPCTSTR msg = L"正常";
	LPCTSTR msg = NULL;
	if (floatbar->speed == 1)
		msg = L"正常";
	else if (floatbar->speed == 2)
		msg = L"2倍";
	else if (floatbar->speed == 4)
		msg = L"4倍";
	else if (floatbar->speed == 8)
		msg = L"8倍";

	SetTextColor(hdc, RGB(128, 128, 128));
	SetBkMode(hdc, TRANSPARENT);
	SelectObject(hdc, floatbar->hFontNormal);
	DrawText(hdc, msg, lstrlen(msg), &rc, DT_VCENTER);

	// 绘制当前已播放时间
	static _txt_w = 0;
	static _txt_h = 0;
	if (_txt_w == 0)
	{
		//RECT rc;
		rc.left = 0;
		rc.top = 0;

		msg = L"00:00:00 / 00:00:00";
		SetTextColor(hdc, RGB(200, 200, 200));
		SetBkMode(hdc, TRANSPARENT);
		SelectObject(hdc, floatbar->hFontMono);
		DrawText(hdc, msg, lstrlen(msg), &rc, DT_CALCRECT);

		_txt_w = rc.right - rc.left;
		_txt_h = rc.bottom - rc.top;
	}

	//RECT rc;
	rc.left = BACKGROUND_W - _txt_w - 120;
	rc.top = TXT_SPEED_Y;
	rc.right = rc.left + _txt_w + 20;
	rc.bottom = rc.top + BUTTON_HEIGHT;
	SetTextColor(hdc, RGB(220, 220, 220));
	SetBkMode(hdc, TRANSPARENT);
	SelectObject(hdc, floatbar->hFontMono);
	DrawText(hdc, floatbar->sz_time_current, lstrlen(floatbar->sz_time_current), &rc, DT_LEFT);

	rc.left += (_txt_w / 19) * 8;
	SetTextColor(hdc, RGB(168, 168, 168));
	SetBkMode(hdc, TRANSPARENT);
	SelectObject(hdc, floatbar->hFontMono);
	DrawText(hdc, floatbar->sz_time_total, lstrlen(floatbar->sz_time_total), &rc, DT_LEFT);

	return 0;
}

static int floatbar_animation(FloatBar* floatbar, BOOL show)
{
	SetTimer(floatbar->hwnd, show ? TIMER_ANIMAT_SHOW : TIMER_ANIMAT_HIDE, 10, NULL);
	floatbar->shown = show;
	return 0;
}

LRESULT CALLBACK floatbar_proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	static int dragging_wnd = FALSE;
	static int dragging_progress = FALSE;
	static int lbtn_dwn = FALSE;
	static int btn_dwn_x = 0;
	static FloatBar* floatbar;
	static TRACKMOUSEEVENT tme;

	PAINTSTRUCT ps;
	Button* button;
	HDC hdc;
	int pos_x;
	int pos_y;

	//int xScreen = GetSystemMetrics(SM_CXSCREEN);

	switch(Msg)
	{
		case WM_CREATE:
			floatbar = (FloatBar *)((CREATESTRUCT *)lParam)->lpCreateParams;
			floatbar->hwnd = hWnd;
			floatbar->parent = GetParent(hWnd);

			GetWindowRect(floatbar->hwnd, &floatbar->rect);
			floatbar->width = floatbar->rect.right - floatbar->rect.left;
			floatbar->height = floatbar->rect.bottom - floatbar->rect.top;

			hdc = GetDC(hWnd);
			floatbar->tmp_memdc = CreateCompatibleDC(hdc);
			floatbar->memdc = CreateCompatibleDC(hdc);
			floatbar->memdc_bg = CreateCompatibleDC(hdc);
			SelectObject(floatbar->memdc, floatbar->bg_tmp);
			SelectObject(floatbar->memdc_bg, floatbar->background);
			ReleaseDC(hWnd, hdc);

			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hWnd;
			tme.dwHoverTime = HOVER_DEFAULT;

			_player_thread_start(floatbar);
			
			//SetTimer(hWnd, TIMER_HIDE, TIMEOUT_BEFORE_HIDE, NULL);
			break;

		case WM_COMMAND:
		{
			int old_speed = floatbar->speed;

			switch (LOWORD(wParam))
			{
			case IDM_SPEED_1X:
				floatbar->speed = 1;
				break;
			case IDM_SPEED_2X:
				floatbar->speed = 2;
				break;
			case IDM_SPEED_4X:
				floatbar->speed = 4;
				break;
			case IDM_SPEED_8X:
				floatbar->speed = 8;
				break;
			}

			if (old_speed != floatbar->speed)
			{
				InvalidateRect(floatbar->hwnd, NULL, FALSE);
				UpdateWindow(floatbar->hwnd);
			}
		}
			break;

		case WM_ERASEBKGND:
			return TRUE;

		case WM_PAINT:
			hdc = BeginPaint(hWnd, &ps);

			floatbar_paint(floatbar, floatbar->memdc);
			BitBlt(hdc, 0, 0, BACKGROUND_W, BACKGROUND_H, floatbar->memdc, 0, 0, SRCCOPY);

			EndPaint(hWnd, &ps);
			break;

		case WM_LBUTTONDOWN:
			pos_x = lParam & 0xffff;
			pos_y = (lParam >> 16) & 0xffff;

			button = floatbar_get_button(floatbar, pos_x, pos_y);
			if (!button)
			{
				SetCapture(hWnd);
				dragging_wnd = TRUE;
				btn_dwn_x = lParam & 0xffff;
			}
			else if (button->type == BUTTON_PROGRESS)
			{
				SetCapture(hWnd);
				dragging_progress = TRUE;
				btn_dwn_x = lParam & 0xffff;

				floatbar->playing_before_drag_progress = floatbar->playing;
				floatbar->progress_x_before_drag = floatbar->btn_progress->x;
				floatbar->playing = FALSE;
			}
			else
			{
				lbtn_dwn = TRUE;
			}

			break;

		case WM_LBUTTONUP:
			pos_x = lParam & 0xffff;
			pos_y = (lParam >> 16) & 0xffff;

			ReleaseCapture();
			if (dragging_wnd)
			{
				dragging_wnd = FALSE;
			}

			if (dragging_progress)
			{
				dragging_progress = FALSE;
				floatbar->playing = floatbar->playing_before_drag_progress;
			}

			if (lbtn_dwn)
			{
				button = floatbar_get_button(floatbar, pos_x, pos_y);
				if (button)
					button_hit(button, pos_x, pos_y);
				lbtn_dwn = FALSE;
			}
			break;

		case WM_MOUSEMOVE:
			KillTimer(hWnd, TIMER_HIDE);
			pos_x = lParam & 0xffff;
			pos_y = (lParam >> 16) & 0xffff;

			if (!floatbar->shown)
				floatbar_animation(floatbar, TRUE);

			if (dragging_wnd)
			{
				floatbar->rect.left = floatbar->rect.left + (lParam & 0xffff) - btn_dwn_x;

				if (floatbar->rect.left < 0)
					floatbar->rect.left = 0;
				else if (floatbar->rect.left > floatbar->wfc->width - floatbar->width)
					floatbar->rect.left = floatbar->wfc->width - floatbar->width;

				MoveWindow(hWnd, floatbar->rect.left, 0, floatbar->width, floatbar->height, TRUE);
			}
			else if (dragging_progress)
			{
				int new_x = pos_x - (BTN_PROGRESS_W / 2);
				if (new_x < floatbar->progress_x_before_drag)
					break;
				if (new_x > PROGRESS_X + PROGRESS_W - (BTN_PROGRESS_W / 2))
					break;

				// 计算出新位置的进度百分比
				int p = (new_x - BTN_PROGRESS_X) * 100 / PROGRESS_W;

				// 根据百分比计算出当前应该已经播放掉的时间
				floatbar->time_played = floatbar->time_total * p / 100;
				// 更新当前时间的显示
				_ms_to_str(floatbar, FALSE, floatbar->time_played, floatbar->sz_time_current);

				floatbar->btn_progress->x = new_x;
				InvalidateRect(floatbar->hwnd, NULL, FALSE);
				UpdateWindow(floatbar->hwnd);
			}
			else
			{
				floatbar_check_paint(floatbar, pos_x, pos_y);
			}

			TrackMouseEvent(&tme);
			break;

		case WM_CAPTURECHANGED:
			dragging_wnd = FALSE;
			break;

		case WM_MOUSELEAVE:
		{
			floatbar_check_paint(floatbar, -1, -1);

			SetTimer(hWnd, TIMER_HIDE, TIMEOUT_BEFORE_HIDE, NULL);
			break;
		}
		case WM_TIMER:
			switch (wParam)
			{
				case TIMER_HIDE:
				{
					KillTimer(hWnd, TIMER_HIDE);
					if (!floatbar->playing)
						floatbar_animation(floatbar, FALSE);
					break;
				}
				case TIMER_ANIMAT_SHOW:
				{
					static int y = 0;
					y += 2;
					MoveWindow(floatbar->hwnd, floatbar->rect.left, (y - floatbar->height), floatbar->width, floatbar->height, TRUE);
					if (y >= floatbar->height)
					{
						y = 0;
						MoveWindow(floatbar->hwnd, floatbar->rect.left, 0, floatbar->width, floatbar->height, TRUE);
						KillTimer(hWnd, wParam);
					}
					break;
				}
				case TIMER_ANIMAT_HIDE:
				{
					static int y = 0;

					y += 2;
					MoveWindow(floatbar->hwnd, floatbar->rect.left, -y, floatbar->width, floatbar->height, TRUE);
					if (y >= floatbar->height-4)
					{
						y = 0;
						MoveWindow(floatbar->hwnd, floatbar->rect.left, -floatbar->height+4, floatbar->width, floatbar->height, TRUE);
						KillTimer(hWnd, wParam);
					}
					break;
				}
				default:
					break;
			}
			break;

		case WM_DESTROY:
			floatbar->need_exit = TRUE;
			// TODO: wait for single object of the thread handle.
			Sleep(200);

			DeleteDC(floatbar->tmp_memdc);
			DeleteDC(floatbar->memdc);
			DeleteDC(floatbar->memdc_bg);

			DeleteObject(floatbar->background);
			DeleteObject(floatbar->bg_tmp);

			DeleteObject(floatbar->hFontNormal);
			DeleteObject(floatbar->hFontMono);

			PostQuitMessage(0);
			break;

		default:
			return DefWindowProc(hWnd, Msg, wParam, lParam);
	}
	return 0;
}

static FloatBar* floatbar_create(wfContext* wfc)
{
	FloatBar* floatbar;

	floatbar = (FloatBar *)calloc(1, sizeof(FloatBar));

	if (!floatbar)
		return NULL;

	floatbar->speed = 1;
	floatbar->playing = FALSE;
	floatbar->shown = TRUE;
	floatbar->hwnd = NULL;
	floatbar->parent = wfc->hwnd;
	floatbar->wfc = wfc;
	floatbar->memdc = NULL;
	floatbar->tmp_memdc = NULL;

	floatbar->background = (HBITMAP)LoadImage(wfc->hInstance, MAKEINTRESOURCE(IDB_PBG), IMAGE_BITMAP, BACKGROUND_W, BACKGROUND_H, LR_DEFAULTCOLOR);
	floatbar->bg_tmp = (HBITMAP)LoadImage(wfc->hInstance, MAKEINTRESOURCE(IDB_PBG), IMAGE_BITMAP, BACKGROUND_W, BACKGROUND_H, LR_DEFAULTCOLOR);
	floatbar->buttons[0] = floatbar_create_play_button(floatbar, IDB_PBTN_PLAY, IDB_PBTN_PLAY_HOVER, IDB_PBTN_PAUSE, IDB_PBTN_PAUSE_HOVER, BTN_PLAY_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
	floatbar->buttons[1] = floatbar_create_button(floatbar, BUTTON_STOP, IDB_PBTN_STOP, IDB_PBTN_STOP_HOVER, BTN_STOP_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT);
	floatbar->buttons[2] = floatbar_create_button(floatbar, BUTTON_SPEED, IDB_PBTN_SPEED, IDB_PBTN_SPEED_HOVER, BTN_SPEED_X, BUTTON_Y, BTN_SPEED_W, BUTTON_HEIGHT);
	floatbar->buttons[3] = floatbar_create_progress_button(floatbar, 
		IDB_PBAR_BTN, IDB_PBAR_BTN_HOVER, BTN_PROGRESS_X, BTN_PROGRESS_Y, BTN_PROGRESS_W, BTN_PROGRESS_H,
		PROGRESS_X, PROGRESS_Y, PROGRESS_W, PROGRESS_H,
		IDB_PBAR_LEFT, 4, 4,
		IDB_PBAR_BG, 24, 4
		);

	floatbar->btn_play = floatbar->buttons[0];
	floatbar->btn_progress = floatbar->buttons[3];

	floatbar->hFontNormal = CreateFont(14, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, \
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, \
		DEFAULT_PITCH | FF_SWISS, L"Arial");
	floatbar->hFontMono = CreateFont(14, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, \
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, \
		DEFAULT_PITCH | FF_SWISS, L"Consolas");
	if (NULL == floatbar->hFontMono)
	{
		floatbar->hFontMono = CreateFont(15, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, \
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, \
			DEFAULT_PITCH | FF_SWISS, L"Courier New");
	}

	return floatbar;
}

int floatbar_hide(FloatBar* floatbar)
{
	KillTimer(floatbar->hwnd, TIMER_HIDE);
	MoveWindow(floatbar->hwnd, floatbar->rect.left, -floatbar->height, floatbar->width, floatbar->height, TRUE);
	return 0;
}

int floatbar_show(FloatBar* floatbar)
{
	SetTimer(floatbar->hwnd, TIMER_HIDE, TIMEOUT_BEFORE_HIDE, NULL);
	MoveWindow(floatbar->hwnd, floatbar->rect.left, floatbar->rect.top, floatbar->width, floatbar->height, TRUE);
	return 0;
}

void floatbar_window_create(wfContext *wfc)
{
	WNDCLASSEX wnd_cls;
	HWND barWnd;

	//int x = (GetSystemMetrics(SM_CXSCREEN) - BACKGROUND_W) / 2;
	RECT rc;
	GetClientRect(wfc->hwnd, &rc);
	int x = (rc.right - rc.left - BACKGROUND_W) / 2;
	if (x < 0)
		x = 0;

	wnd_cls.cbSize        = sizeof(WNDCLASSEX);
	wnd_cls.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wnd_cls.lpfnWndProc   = floatbar_proc;
	wnd_cls.cbClsExtra    = 0;
	wnd_cls.cbWndExtra    = 0;
	wnd_cls.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
	wnd_cls.hCursor       = LoadCursor(wfc->hInstance, IDC_ARROW);
	wnd_cls.hbrBackground = NULL;
	wnd_cls.lpszMenuName  = NULL;
	wnd_cls.lpszClassName = L"floatbar";
	wnd_cls.hInstance     = wfc->hInstance;
	wnd_cls.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

	RegisterClassEx(&wnd_cls);

	wfc->floatbar = floatbar_create(wfc);

	barWnd = CreateWindowEx(WS_EX_TOPMOST, L"floatbar", L"floatbar", WS_CHILD, x, 0, BACKGROUND_W, BACKGROUND_H, wfc->hwnd, NULL, wfc->hInstance, wfc->floatbar);
	if (barWnd == NULL)
		return;


	wfc->floatbar->hSpeedMenu = CreatePopupMenu();
	AppendMenu(wfc->floatbar->hSpeedMenu, MF_STRING, IDM_SPEED_1X, L"正常速度");
	AppendMenu(wfc->floatbar->hSpeedMenu, MF_STRING, IDM_SPEED_2X, L"2倍速度");
	AppendMenu(wfc->floatbar->hSpeedMenu, MF_STRING, IDM_SPEED_4X, L"4倍速度");
	AppendMenu(wfc->floatbar->hSpeedMenu, MF_STRING, IDM_SPEED_8X, L"8倍速度");

	ShowWindow(barWnd, SW_SHOWNORMAL);
}

void floatbar_window_destroy(wfContext* wfc)
{
	if (NULL != wfc->floatbar)
	{
		DestroyWindow(wfc->floatbar->hwnd);
	}
}
