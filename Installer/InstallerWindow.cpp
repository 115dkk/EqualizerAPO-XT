/*
	This file is part of EqualizerAPO-XT.

	Direct2D implementation of the installer window declared in
	InstallerWindow.h. One render function draws the InstallerUi::Model for
	both the live HWND target and the --ui-shot WIC target, so the preview
	PNGs are pixel-exact evidence of what users see.
*/

#include "InstallerWindow.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../version.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwmapi.lib")

// Older SDKs miss the Win11 attributes; the numeric values are stable.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

namespace InstallerUi
{
namespace
{
// Fixed window size in DIPs; the render target scales it per monitor.
constexpr float kClientWidth = 500.0f;
constexpr float kClientHeight = 416.0f;
constexpr float kMargin = 32.0f;

constexpr UINT kMsgRepaint = WM_APP + 1;
constexpr UINT kMsgFinish = WM_APP + 2;
constexpr UINT_PTR kAnimTimer = 1;
constexpr UINT_PTR kCloseTimer = 2;
constexpr UINT_PTR kCancelWatchdogTimer = 3;

// GitHub dark palette; proven legible on dark and consistent with where the
// download actually comes from.
constexpr D2D1_COLOR_F rgb(UINT32 hex)
{
	return D2D1_COLOR_F{
		((hex >> 16) & 0xFF) / 255.0f,
		((hex >> 8) & 0xFF) / 255.0f,
		(hex & 0xFF) / 255.0f,
		1.0f };
}

const D2D1_COLOR_F kColorBackground = rgb(0x15171B);
const D2D1_COLOR_F kColorHairline = rgb(0x262B33);
const D2D1_COLOR_F kColorTextPrimary = rgb(0xE8EBF1);
const D2D1_COLOR_F kColorTextSecondary = rgb(0x949BA8);
const D2D1_COLOR_F kColorTextDim = rgb(0x575E6B);
const D2D1_COLOR_F kColorAccent = rgb(0x58A6FF);
const D2D1_COLOR_F kColorSuccess = rgb(0x3FB950);
const D2D1_COLOR_F kColorError = rgb(0xF85149);
const D2D1_COLOR_F kColorBarTrack = rgb(0x2A303A);
const D2D1_COLOR_F kColorButtonFill = rgb(0x21262E);
const D2D1_COLOR_F kColorButtonHover = rgb(0x2B323D);
const D2D1_COLOR_F kColorButtonBorder = rgb(0x363D49);

const wchar_t* kWindowClassName = L"EqualizerAPOXTSetupWindow";
const wchar_t* kWindowTitle = L"EqualizerAPO-XT Setup";
const wchar_t* kRepoUrl = EAPO_REPO_URL_W;
const wchar_t* kRepoLinkText = L"github.com/" EAPO_REPO_SLUG_W;
const wchar_t* kReleasesUrl = EAPO_REPO_URL_W L"/releases/latest";
const wchar_t* kTrustLine =
	L"Each download is verified against the release's SHA-256 checksums before it runs.";

// Interactive rectangles one render pass laid out, in DIPs.
struct InteractiveLayout
{
	D2D1_RECT_F releasesButton = {};
	D2D1_RECT_F closeButton = {};
	D2D1_RECT_F repoLink = {};
	bool errorPanel = false;
};

struct RenderContext
{
	ID2D1RenderTarget* target = nullptr;
	ID2D1Factory* factory = nullptr;
	IDWriteFactory* dwrite = nullptr;
	const TextFormats* formats = nullptr;
	float animPhase = 0.0f;
	int hoverTarget = 0;
};

std::wstring preferredFontFamily(IDWriteFactory* dwrite)
{
	winutil::ComPtr<IDWriteFontCollection> fonts;
	if (SUCCEEDED(dwrite->GetSystemFontCollection(fonts.put(), FALSE)) && fonts)
	{
		UINT32 index = 0;
		BOOL exists = FALSE;
		if (SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists)
			return L"Segoe UI Variable Text";
	}
	return L"Segoe UI";
}

bool makeFormat(IDWriteFactory* dwrite, const std::wstring& family, float size,
	DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment, bool wrap,
	winutil::ComPtr<IDWriteTextFormat>& out)
{
	if (FAILED(dwrite->CreateTextFormat(family.c_str(), nullptr, weight,
			DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out.put())))
	{
		return false;
	}
	out->SetTextAlignment(alignment);
	out->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
	return true;
}

bool createTextFormats(IDWriteFactory* dwrite, TextFormats& formats)
{
	const std::wstring family = preferredFontFamily(dwrite);
	return makeFormat(dwrite, family, 23.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
			   DWRITE_TEXT_ALIGNMENT_LEADING, false, formats.title)
		   && makeFormat(dwrite, family, 13.0f, DWRITE_FONT_WEIGHT_NORMAL,
			   DWRITE_TEXT_ALIGNMENT_TRAILING, false, formats.titleTag)
		   && makeFormat(dwrite, family, 12.5f, DWRITE_FONT_WEIGHT_NORMAL,
			   DWRITE_TEXT_ALIGNMENT_LEADING, false, formats.subtitle)
		   && makeFormat(dwrite, family, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
			   DWRITE_TEXT_ALIGNMENT_LEADING, false, formats.stepTitle)
		   && makeFormat(dwrite, family, 11.5f, DWRITE_FONT_WEIGHT_NORMAL,
			   DWRITE_TEXT_ALIGNMENT_LEADING, false, formats.stepDetail)
		   && makeFormat(dwrite, family, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
			   DWRITE_TEXT_ALIGNMENT_TRAILING, false, formats.stepDetailRight)
		   && makeFormat(dwrite, family, 11.0f, DWRITE_FONT_WEIGHT_NORMAL,
			   DWRITE_TEXT_ALIGNMENT_LEADING, true, formats.footer)
		   && makeFormat(dwrite, family, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
			   DWRITE_TEXT_ALIGNMENT_CENTER, false, formats.button);
}

void drawString(const RenderContext& ctx, const std::wstring& text, IDWriteTextFormat* format,
	D2D1_RECT_F rect, ID2D1SolidColorBrush* brush, DWRITE_TEXT_METRICS* outMetrics = nullptr)
{
	winutil::ComPtr<IDWriteTextLayout> layout;
	if (FAILED(ctx.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
			format, rect.right - rect.left, rect.bottom - rect.top, layout.put())))
	{
		return;
	}
	ctx.target->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.get(), brush,
		D2D1_DRAW_TEXT_OPTIONS_NONE);
	if (outMetrics != nullptr)
		layout->GetMetrics(outMetrics);
}

// The active step's spinner: a quarter arc orbiting a dim track.
void drawSpinner(const RenderContext& ctx, D2D1_POINT_2F center, float radius,
	ID2D1SolidColorBrush* brush)
{
	brush->SetColor(kColorBarTrack);
	ctx.target->DrawEllipse(D2D1::Ellipse(center, radius, radius), brush, 1.8f);

	const float startAngle = ctx.animPhase * 2.0f * 3.14159265f;
	const float sweep = 0.55f * 3.14159265f;
	const D2D1_POINT_2F start = D2D1::Point2F(
		center.x + radius * std::cos(startAngle), center.y + radius * std::sin(startAngle));
	const D2D1_POINT_2F end = D2D1::Point2F(
		center.x + radius * std::cos(startAngle + sweep),
		center.y + radius * std::sin(startAngle + sweep));

	winutil::ComPtr<ID2D1PathGeometry> geometry;
	if (FAILED(ctx.factory->CreatePathGeometry(geometry.put())))
		return;
	winutil::ComPtr<ID2D1GeometrySink> sink;
	if (FAILED(geometry->Open(sink.put())))
		return;
	sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
	sink->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(radius, radius), 0.0f,
		D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
	sink->EndFigure(D2D1_FIGURE_END_OPEN);
	sink->Close();

	brush->SetColor(kColorAccent);
	ctx.target->DrawGeometry(geometry.get(), brush, 2.2f);
}

void drawStepGlyph(const RenderContext& ctx, StepState state, D2D1_POINT_2F center,
	ID2D1SolidColorBrush* brush)
{
	const float radius = 9.0f;
	winutil::ComPtr<ID2D1StrokeStyle> roundCap;
	D2D1_STROKE_STYLE_PROPERTIES capProps = D2D1::StrokeStyleProperties(
		D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
		D2D1_LINE_JOIN_ROUND);
	ctx.factory->CreateStrokeStyle(capProps, nullptr, 0, roundCap.put());

	switch (state)
	{
	case StepState::Pending:
		brush->SetColor(kColorTextDim);
		ctx.target->DrawEllipse(D2D1::Ellipse(center, radius, radius), brush, 1.5f);
		break;
	case StepState::Active:
		drawSpinner(ctx, center, radius, brush);
		break;
	case StepState::Done:
		brush->SetColor(kColorSuccess);
		ctx.target->FillEllipse(D2D1::Ellipse(center, radius, radius), brush);
		brush->SetColor(kColorBackground);
		ctx.target->DrawLine(D2D1::Point2F(center.x - 4.0f, center.y + 0.5f),
			D2D1::Point2F(center.x - 1.5f, center.y + 3.2f), brush, 2.0f, roundCap.get());
		ctx.target->DrawLine(D2D1::Point2F(center.x - 1.5f, center.y + 3.2f),
			D2D1::Point2F(center.x + 4.5f, center.y - 3.5f), brush, 2.0f, roundCap.get());
		break;
	case StepState::Failed:
		brush->SetColor(kColorError);
		ctx.target->FillEllipse(D2D1::Ellipse(center, radius, radius), brush);
		brush->SetColor(kColorBackground);
		ctx.target->DrawLine(D2D1::Point2F(center.x - 3.4f, center.y - 3.4f),
			D2D1::Point2F(center.x + 3.4f, center.y + 3.4f), brush, 2.0f, roundCap.get());
		ctx.target->DrawLine(D2D1::Point2F(center.x + 3.4f, center.y - 3.4f),
			D2D1::Point2F(center.x - 3.4f, center.y + 3.4f), brush, 2.0f, roundCap.get());
		break;
	}
}

void drawProgressBar(const RenderContext& ctx, D2D1_RECT_F rect, double fraction,
	ID2D1SolidColorBrush* brush)
{
	const float radius = (rect.bottom - rect.top) / 2.0f;
	brush->SetColor(kColorBarTrack);
	ctx.target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);

	brush->SetColor(kColorAccent);
	if (fraction < 0.0)
	{
		// Size unknown: a sliding segment covering 30 % of the track.
		const float width = rect.right - rect.left;
		const float segment = width * 0.3f;
		const float travel = width + segment;
		float x = rect.left - segment + ctx.animPhase * travel;
		D2D1_RECT_F fill = D2D1::RectF(
			(std::max)(rect.left, x), rect.top, (std::min)(rect.right, x + segment), rect.bottom);
		if (fill.right > fill.left)
			ctx.target->FillRoundedRectangle(D2D1::RoundedRect(fill, radius, radius), brush);
	}
	else if (fraction > 0.0)
	{
		D2D1_RECT_F fill = rect;
		fill.right = rect.left + static_cast<float>(fraction) * (rect.right - rect.left);
		if (fill.right - fill.left >= 2.0f * radius)
			ctx.target->FillRoundedRectangle(D2D1::RoundedRect(fill, radius, radius), brush);
	}
}

void drawButton(const RenderContext& ctx, D2D1_RECT_F rect, const std::wstring& label,
	bool hovered, bool accentText, ID2D1SolidColorBrush* brush)
{
	brush->SetColor(hovered ? kColorButtonHover : kColorButtonFill);
	ctx.target->FillRoundedRectangle(D2D1::RoundedRect(rect, 6.0f, 6.0f), brush);
	brush->SetColor(kColorButtonBorder);
	ctx.target->DrawRoundedRectangle(D2D1::RoundedRect(rect, 6.0f, 6.0f), brush, 1.0f);

	brush->SetColor(accentText ? kColorAccent : kColorTextPrimary);
	D2D1_RECT_F textRect = rect;
	textRect.top += ((rect.bottom - rect.top) - 16.0f) / 2.0f;
	drawString(ctx, label, ctx.formats->button.get(), textRect, brush);
}

void renderInstaller(const RenderContext& ctx, const Model& model, InteractiveLayout* layoutOut)
{
	ID2D1RenderTarget* rt = ctx.target;
	rt->Clear(kColorBackground);
	rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

	winutil::ComPtr<ID2D1SolidColorBrush> brushPtr;
	if (FAILED(rt->CreateSolidColorBrush(kColorTextPrimary, brushPtr.put())))
		return;
	ID2D1SolidColorBrush* brush = brushPtr.get();

	const float right = kClientWidth - kMargin;

	// Header.
	brush->SetColor(kColorTextPrimary);
	drawString(ctx, L"EqualizerAPO-XT", ctx.formats->title.get(),
		D2D1::RectF(kMargin, 26.0f, right, 58.0f), brush);
	brush->SetColor(kColorTextSecondary);
	drawString(ctx, L"Setup", ctx.formats->titleTag.get(),
		D2D1::RectF(kMargin, 36.0f, right, 54.0f), brush);
	drawString(ctx, L"Installs the best build for this computer's CPU.",
		ctx.formats->subtitle.get(), D2D1::RectF(kMargin, 62.0f, right, 80.0f), brush);
	brush->SetColor(kColorHairline);
	rt->FillRectangle(D2D1::RectF(kMargin, 96.0f, right, 97.0f), brush);

	// Step timeline.
	const float stepsTop = 114.0f;
	const float rowHeight = 56.0f;
	const float glyphX = kMargin + 9.0f;
	const float textX = kMargin + 36.0f;
	for (int i = 0; i < kStepCount; i++)
	{
		const float rowTop = stepsTop + static_cast<float>(i) * rowHeight;
		const D2D1_POINT_2F glyphCenter = D2D1::Point2F(glyphX, rowTop + 11.0f);

		// Connector to the next step's glyph.
		if (i + 1 < kStepCount)
		{
			brush->SetColor(model.states[i] == StepState::Done ? kColorSuccess : kColorHairline);
			rt->FillRectangle(D2D1::RectF(glyphX - 0.5f, glyphCenter.y + 14.0f,
				glyphX + 0.5f, glyphCenter.y + rowHeight - 14.0f), brush);
		}

		drawStepGlyph(ctx, model.states[i], glyphCenter, brush);

		brush->SetColor(model.states[i] == StepState::Pending ? kColorTextDim : kColorTextPrimary);
		drawString(ctx, stepTitle(i), ctx.formats->stepTitle.get(),
			D2D1::RectF(textX, rowTop + 2.0f, right - 70.0f, rowTop + 22.0f), brush);

		if (!model.details[i].empty())
		{
			brush->SetColor(model.states[i] == StepState::Failed ? kColorError : kColorTextSecondary);
			drawString(ctx, model.details[i], ctx.formats->stepDetail.get(),
				D2D1::RectF(textX, rowTop + 23.0f, right, rowTop + 39.0f), brush);
		}

		if (i == kStepDownload && model.states[i] == StepState::Active)
		{
			const double fraction = downloadFraction(model.downloadedBytes, model.totalBytes);
			if (fraction >= 0.0)
			{
				wchar_t percent[16] = {};
				swprintf(percent, 16, L"%d%%", static_cast<int>(fraction * 100.0));
				brush->SetColor(kColorAccent);
				drawString(ctx, percent, ctx.formats->stepDetailRight.get(),
					D2D1::RectF(right - 68.0f, rowTop + 2.0f, right, rowTop + 22.0f), brush);
			}
			drawProgressBar(ctx, D2D1::RectF(textX, rowTop + 43.0f, right, rowTop + 48.0f),
				fraction, brush);
		}
	}

	// Footer: hairline, then either the trust line + repository link or the
	// error panel with its actions.
	brush->SetColor(kColorHairline);
	rt->FillRectangle(D2D1::RectF(kMargin, 344.0f, right, 345.0f), brush);

	InteractiveLayout layout;
	layout.errorPanel = !model.errorText.empty();
	if (layout.errorPanel)
	{
		brush->SetColor(kColorError);
		drawString(ctx, model.errorText, ctx.formats->footer.get(),
			D2D1::RectF(kMargin, 352.0f, right, 380.0f), brush);

		layout.closeButton = D2D1::RectF(right - 84.0f, 384.0f, right, 410.0f);
		layout.releasesButton = D2D1::RectF(right - 84.0f - 10.0f - 150.0f, 384.0f,
			right - 84.0f - 10.0f, 410.0f);
		drawButton(ctx, layout.releasesButton, L"Open releases page",
			ctx.hoverTarget == 1, true, brush);
		drawButton(ctx, layout.closeButton, L"Close", ctx.hoverTarget == 2, false, brush);
	}
	else
	{
		brush->SetColor(kColorTextDim);
		drawString(ctx, kTrustLine, ctx.formats->footer.get(),
			D2D1::RectF(kMargin, 356.0f, right, 372.0f), brush);

		brush->SetColor(ctx.hoverTarget == 3 ? kColorAccent : kColorTextSecondary);
		DWRITE_TEXT_METRICS metrics = {};
		drawString(ctx, kRepoLinkText, ctx.formats->footer.get(),
			D2D1::RectF(kMargin, 380.0f, right, 396.0f), brush, &metrics);
		layout.repoLink = D2D1::RectF(kMargin, 380.0f, kMargin + metrics.width, 396.0f);
		if (ctx.hoverTarget == 3)
		{
			rt->FillRectangle(D2D1::RectF(layout.repoLink.left, 396.0f,
				layout.repoLink.right, 397.0f), brush);
		}
	}

	if (layoutOut != nullptr)
		*layoutOut = layout;
}
} // namespace

InstallerWindow::InstallerWindow() = default;

InstallerWindow::~InstallerWindow() = default;

bool InstallerWindow::create(HINSTANCE instance)
{
	if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
			nullptr, reinterpret_cast<void**>(d2dFactory.put()))))
	{
		return false;
	}
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(dwriteFactory.put()))))
	{
		return false;
	}
	if (!createTextFormats(dwriteFactory.get(), formats))
		return false;

	WNDCLASSW windowClass = {};
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = &InstallerWindow::wndProc;
	windowClass.hInstance = instance;
	windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
	windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	// A background brush in the window's own color, so the first frame before
	// WM_PAINT never flashes white.
	windowClass.hbrBackground = CreateSolidBrush(RGB(0x15, 0x17, 0x1B));
	windowClass.lpszClassName = kWindowClassName;
	if (RegisterClassW(&windowClass) == 0)
		return false;

	const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	hwnd = CreateWindowExW(0, kWindowClassName, kWindowTitle, style,
		CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance, this);
	if (hwnd == nullptr)
		return false;

	dpi = GetDpiForWindow(hwnd);
	RECT bounds = { 0, 0,
		static_cast<LONG>(kClientWidth * dpi / 96.0f + 0.5f),
		static_cast<LONG>(kClientHeight * dpi / 96.0f + 0.5f) };
	AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi);
	RECT workArea = { 0, 0, 800, 600 };
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
	const int width = bounds.right - bounds.left;
	const int height = bounds.bottom - bounds.top;
	SetWindowPos(hwnd, nullptr,
		workArea.left + ((workArea.right - workArea.left) - width) / 2,
		workArea.top + ((workArea.bottom - workArea.top) - height) / 2,
		width, height, SWP_NOZORDER | SWP_NOACTIVATE);

	// Dark caption and a frame that blends into the client area. Win11-only
	// attributes fail harmlessly on Win10, where the dark-mode caption alone
	// still matches.
	BOOL darkMode = TRUE;
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
	COLORREF chrome = RGB(0x15, 0x17, 0x1B);
	DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &chrome, sizeof(chrome));
	DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &chrome, sizeof(chrome));
	COLORREF captionText = RGB(0xE8, 0xEB, 0xF1);
	DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &captionText, sizeof(captionText));

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	return true;
}

int InstallerWindow::runMessageLoop()
{
	MSG message = {};
	while (GetMessageW(&message, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	return exitCode;
}

void InstallerWindow::update(const std::function<void(Model&)>& mutate)
{
	{
		std::lock_guard<std::mutex> lock(modelMutex);
		mutate(model);
	}
	PostMessageW(hwnd, kMsgRepaint, 0, 0);
}

void InstallerWindow::finish(int code, unsigned closeDelayMs)
{
	PostMessageW(hwnd, kMsgFinish, static_cast<WPARAM>(code),
		static_cast<LPARAM>(closeDelayMs));
}

bool InstallerWindow::isCancelRequested() const
{
	return cancelRequested.load();
}

LRESULT CALLBACK InstallerWindow::wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_NCCREATE)
	{
		const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(create->lpCreateParams));
	}
	InstallerWindow* self =
		reinterpret_cast<InstallerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (self == nullptr)
		return DefWindowProcW(hwnd, message, wParam, lParam);
	if (self->hwnd == nullptr)
		self->hwnd = hwnd;
	return self->handleMessage(message, wParam, lParam);
}

LRESULT InstallerWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
	{
		PAINTSTRUCT ps = {};
		BeginPaint(hwnd, &ps);
		paint();
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		if (renderTarget)
			renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
		return 0;
	case WM_DPICHANGED:
	{
		dpi = HIWORD(wParam);
		const RECT* suggested = reinterpret_cast<RECT*>(lParam);
		SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
			suggested->right - suggested->left, suggested->bottom - suggested->top,
			SWP_NOZORDER | SWP_NOACTIVATE);
		if (renderTarget)
			renderTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}
	case kMsgRepaint:
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case kMsgFinish:
		exitCode = static_cast<int>(wParam);
		sessionFinished.store(true);
		if (cancelRequested.load())
			DestroyWindow(hwnd);
		else if (lParam > 0)
			SetTimer(hwnd, kCloseTimer, static_cast<UINT>(lParam), nullptr);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_TIMER:
		if (wParam == kAnimTimer)
		{
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		else if (wParam == kCloseTimer || wParam == kCancelWatchdogTimer)
		{
			KillTimer(hwnd, wParam);
			DestroyWindow(hwnd);
		}
		return 0;
	case WM_MOUSEMOVE:
	{
		const int target = hitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		if (target != hoverTarget)
		{
			hoverTarget = target;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
		TrackMouseEvent(&track);
		return 0;
	}
	case WM_MOUSELEAVE:
		if (hoverTarget != kHitNone)
		{
			hoverTarget = kHitNone;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && hoverTarget != kHitNone)
		{
			SetCursor(LoadCursorW(nullptr, IDC_HAND));
			return TRUE;
		}
		break;
	case WM_LBUTTONUP:
		switch (hitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
		{
		case kHitReleasesButton:
			ShellExecuteW(nullptr, L"open", kReleasesUrl, nullptr, nullptr, SW_SHOWNORMAL);
			DestroyWindow(hwnd);
			break;
		case kHitCloseButton:
			DestroyWindow(hwnd);
			break;
		case kHitRepoLink:
			ShellExecuteW(nullptr, L"open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
			break;
		default:
			break;
		}
		return 0;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE)
			PostMessageW(hwnd, WM_CLOSE, 0, 0);
		return 0;
	case WM_CLOSE:
		if (sessionFinished.load())
		{
			DestroyWindow(hwnd);
		}
		else
		{
			// The worker is mid-download: hide immediately, let it notice the
			// cancel flag and post its finish. The watchdog covers a worker
			// stuck in a blocking WinHTTP call.
			cancelRequested.store(true);
			ShowWindow(hwnd, SW_HIDE);
			SetTimer(hwnd, kCancelWatchdogTimer, 10000, nullptr);
		}
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool InstallerWindow::createDeviceResources()
{
	if (renderTarget)
		return true;
	RECT client = {};
	GetClientRect(hwnd, &client);
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
		static_cast<float>(dpi), static_cast<float>(dpi));
	const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
		hwnd, D2D1::SizeU(client.right - client.left, client.bottom - client.top));
	return SUCCEEDED(d2dFactory->CreateHwndRenderTarget(props, hwndProps, renderTarget.put()));
}

void InstallerWindow::paint()
{
	if (!createDeviceResources())
		return;

	Model snapshot;
	{
		std::lock_guard<std::mutex> lock(modelMutex);
		snapshot = model;
	}

	RenderContext ctx;
	ctx.target = renderTarget.get();
	ctx.factory = d2dFactory.get();
	ctx.dwrite = dwriteFactory.get();
	ctx.formats = &formats;
	ctx.animPhase = static_cast<float>(GetTickCount64() % 1200) / 1200.0f;
	ctx.hoverTarget = hoverTarget;

	InteractiveLayout layout;
	renderTarget->BeginDraw();
	renderInstaller(ctx, snapshot, &layout);
	const HRESULT hr = renderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		renderTarget.reset();
		InvalidateRect(hwnd, nullptr, FALSE);
		return;
	}

	releasesButtonRect = { layout.releasesButton.left, layout.releasesButton.top,
		layout.releasesButton.right, layout.releasesButton.bottom };
	closeButtonRect = { layout.closeButton.left, layout.closeButton.top,
		layout.closeButton.right, layout.closeButton.bottom };
	repoLinkRect = { layout.repoLink.left, layout.repoLink.top,
		layout.repoLink.right, layout.repoLink.bottom };
	errorPanelVisible = layout.errorPanel;

	bool anyActive = false;
	for (int i = 0; i < kStepCount; i++)
	{
		if (snapshot.states[i] == StepState::Active)
			anyActive = true;
	}
	updateAnimationTimer(anyActive);
}

int InstallerWindow::hitTest(int xPx, int yPx) const
{
	const float x = static_cast<float>(xPx) * 96.0f / static_cast<float>(dpi);
	const float y = static_cast<float>(yPx) * 96.0f / static_cast<float>(dpi);
	const auto contains = [x, y](const RectF& rect)
	{
		return rect.right > rect.left && x >= rect.left && x < rect.right
			   && y >= rect.top && y < rect.bottom;
	};
	if (errorPanelVisible)
	{
		if (contains(releasesButtonRect))
			return kHitReleasesButton;
		if (contains(closeButtonRect))
			return kHitCloseButton;
	}
	else if (contains(repoLinkRect))
	{
		return kHitRepoLink;
	}
	return kHitNone;
}

void InstallerWindow::updateAnimationTimer(bool anyStepActive)
{
	if (anyStepActive && !animationTimerRunning)
	{
		SetTimer(hwnd, kAnimTimer, 33, nullptr);
		animationTimerRunning = true;
	}
	else if (!anyStepActive && animationTimerRunning)
	{
		KillTimer(hwnd, kAnimTimer);
		animationTimerRunning = false;
	}
}

namespace
{
bool savePng(IWICImagingFactory* wic, IWICBitmap* bitmap, const std::wstring& path)
{
	winutil::ComPtr<IWICStream> stream;
	if (FAILED(wic->CreateStream(stream.put())))
		return false;
	if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
		return false;
	winutil::ComPtr<IWICBitmapEncoder> encoder;
	if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())))
		return false;
	if (FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)))
		return false;
	winutil::ComPtr<IWICBitmapFrameEncode> frame;
	if (FAILED(encoder->CreateNewFrame(frame.put(), nullptr)))
		return false;
	if (FAILED(frame->Initialize(nullptr)))
		return false;
	if (FAILED(frame->WriteSource(bitmap, nullptr)))
		return false;
	return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

struct ShotFixture
{
	const wchar_t* name = L"";
	Model model;
};

std::vector<ShotFixture> shotFixtures()
{
	std::vector<ShotFixture> fixtures;

	Model detecting;
	startStep(detecting, kStepDetect, L"Reading CPUID and the OS-enabled register state");
	fixtures.push_back({ L"01-detecting", detecting });

	Model downloading = detecting;
	finishStep(downloading, kStepDetect,
		describeChannel(L"x64-avx2") + L" \u2014 x64-avx2 build");
	downloading.downloadedBytes = 116686848ULL;
	downloading.totalBytes = 276824064ULL;
	startStep(downloading, kStepDownload,
		formatDownloadDetail(downloading.downloadedBytes, downloading.totalBytes));
	fixtures.push_back({ L"02-downloading", downloading });

	Model verifying = downloading;
	finishStep(verifying, kStepDownload,
		formatByteSize(verifying.totalBytes) + L" downloaded");
	startStep(verifying, kStepVerify,
		L"Computing SHA-256 and matching the release checksums");
	fixtures.push_back({ L"03-verifying", verifying });

	Model handoff = verifying;
	finishStep(handoff, kStepVerify,
		L"SHA-256 matches the published checksum (" + shortHash(
			L"91fb5d115b20f8d78fb8e3ff4f0e81334122b9819fda29923d0e4c9be27e9757") + L")");
	finishStep(handoff, kStepLaunch,
		L"Velopack installer started \u2014 this window closes itself");
	handoff.completed = true;
	fixtures.push_back({ L"04-handoff", handoff });

	Model downloadError = detecting;
	finishStep(downloadError, kStepDetect,
		describeChannel(L"x64-avx2") + L" \u2014 x64-avx2 build");
	startStep(downloadError, kStepDownload, L"");
	failStep(downloadError, kStepDownload,
		L"The matching installer was not found on the release page (HTTP 404)."
		L" You can download a build manually from the releases page.");
	fixtures.push_back({ L"05-error-download", downloadError });

	Model verifyError = verifying;
	failStep(verifyError, kStepVerify,
		L"The downloaded installer failed its integrity check and was deleted."
		L" Please try again or download a build manually from the releases page.");
	fixtures.push_back({ L"06-error-verify", verifyError });

	return fixtures;
}
} // namespace

bool InstallerWindow::renderShots(const std::wstring& outDir)
{
	CreateDirectoryW(outDir.c_str(), nullptr);

	winutil::ComPtr<ID2D1Factory> factory;
	if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
			nullptr, reinterpret_cast<void**>(factory.put()))))
	{
		return false;
	}
	winutil::ComPtr<IDWriteFactory> dwrite;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(dwrite.put()))))
	{
		return false;
	}
	winutil::ComPtr<IWICImagingFactory> wic;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(wic.put()))))
	{
		return false;
	}
	TextFormats shotFormats;
	if (!createTextFormats(dwrite.get(), shotFormats))
		return false;

	for (const ShotFixture& fixture : shotFixtures())
	{
		for (int scale = 1; scale <= 2; scale++)
		{
			winutil::ComPtr<IWICBitmap> bitmap;
			if (FAILED(wic->CreateBitmap(
					static_cast<UINT>(kClientWidth) * scale,
					static_cast<UINT>(kClientHeight) * scale,
					GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, bitmap.put())))
			{
				return false;
			}
			const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
				96.0f * scale, 96.0f * scale);
			winutil::ComPtr<ID2D1RenderTarget> target;
			if (FAILED(factory->CreateWicBitmapRenderTarget(bitmap.get(), props, target.put())))
				return false;

			RenderContext ctx;
			ctx.target = target.get();
			ctx.factory = factory.get();
			ctx.dwrite = dwrite.get();
			ctx.formats = &shotFormats;
			ctx.animPhase = 0.35f;
			ctx.hoverTarget = 0;

			target->BeginDraw();
			renderInstaller(ctx, fixture.model, nullptr);
			if (FAILED(target->EndDraw()))
				return false;

			std::wstring path = outDir + L"\\" + fixture.name
				+ (scale == 2 ? L"@2x.png" : L".png");
			if (!savePng(wic.get(), bitmap.get(), path))
				return false;
		}
	}
	return true;
}
}
