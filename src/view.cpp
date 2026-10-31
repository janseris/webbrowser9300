// Browser 9300 - the page on the screen: an off-screen bitmap with the header line, the visible
// part of the layout (fills, text, pictures, form fields), the focused link or field, a scroll
// bar, and a list drawn over the page (select options, page info).
//
// Keys: Up/Down move to the next link when it is on screen, otherwise scroll; Left/Right and
// Tab jump from link to link; Shift+Up/Down or Space page; Ctrl+Up/Down top/bottom;
// Enter opens / edits; Esc or Backspace go back.

#include <eikenv.h>
#include <fbs.h>
#include "browser.h"
#include "browser.hrh"

const TInt KScrollStep = 36;
const TInt KScrollBarW = 4;
const TInt KFullWidth = 1024;              // the original size: a desktop window's width
const TInt KScrollBarH = 3;                // the sideways bar at the bottom
const TInt KEdgeTop = 12;                  // the pointer resting this near the top scrolls up
const TInt KEdgeSide = 8;                  // the left edge (sideways at the original size)
const TInt KSideW = 100;                   // the bar at the right: title, link, status
const TInt KListMaxRows = 10;

static TRgb Rgb(TInt c) { return TRgb((c >> 16) & 255, (c >> 8) & 255, c & 255); }

CBrView* CBrView::NewL(const TRect& aRect, CBrAppUi& aAppUi, CImageLoader& aImages, CBrFonts& aFonts)
    {
    CBrView* self = new (ELeave) CBrView(aAppUi, aImages, aFonts);
    CleanupStack::PushL(self);
    self->ConstructL(aRect);
    CleanupStack::Pop(self);
    return self;
    }

CBrView::CBrView(CBrAppUi& aAppUi, CImageLoader& aImages, CBrFonts& aFonts)
    : iAppUi(aAppUi), iImages(aImages), iFonts(&aFonts), iFocus(-1), iForeground(ETrue), iPointerPos(-1, -1),
      iHoverTarget(-1), iHoverElem(-1), iHoverSent(-1), iPointerMinY(KMaxTInt), iFindBox(-1)
    {
    }

// Finds the text (case and accents ignored) in the laid-out text, from the screen's top or after
// the last match; wraps around. ETrue when found (it is then highlighted and scrolled to).
TBool CBrView::Find(const TDesC& aText, TBool aNext)
    {
    if (!iDoc || !iLayout || !aText.Length()) return EFalse;
    TInt m = aText.Length();
    HBufC* needle = HBufC::New(m);
    if (!needle) return EFalse;
    TPtr nd = needle->Des();
    for (TInt k = 0; k < m; k++) nd.Append((TChar)User::Fold(aText[k]));
    TInt n = (TInt)iLayout->boxCount;
    TInt start = aNext && iFindBox >= 0 && iFindBox < n ? iFindBox : (TInt)bw_layout_first_box(iLayout, iScrollY);
    TInt from = aNext && iFindBox >= 0 ? iFindOff + 1 : 0;
    TBool found = EFalse;
    for (TInt step = 0; step <= n && !found; step++)
        {
        TInt i = (start + step) % (n ? n : 1);
        const BwBox& b = iLayout->boxes[i];
        if (b.kind != BW_BOX_TEXT) { from = 0; continue; }
        const unsigned short* t = iDoc->pool + b.off;
        for (TInt p = from; p + m <= (TInt)b.len; p++)
            {
            TInt k = 0;
            while (k < m && (TUint)User::Fold(t[p + k]) == (TUint)nd[k]) k++;
            if (k == m)
                {
                found = ETrue;
                iFindBox = i;
                iFindOff = p;
                iFindLen = m;
                TInt h = PageHeightOnScreen();
                if (b.y < iScrollY || b.y + b.h > iScrollY + h) iScrollY = b.y - h / 3;
                if (b.x < iScrollX || b.x + b.w > iScrollX + PageWidthOnScreen()) iScrollX = b.x - 20;
                ClampScroll();
                break;
                }
            }
        from = 0;
        }
    delete needle;
    if (!found) iFindBox = -1;
    Render();
    return found;
    }

void CBrView::ConstructL(const TRect& aRect)
    {
    CreateWindowL();
    iSideW = KSideW;
    iHeaderH = iEikonEnv->DenseFont()->HeightInPixels() + 5;
    iRelayout = new (ELeave) CAsyncCallBack(TCallBack(RelayoutCallback, this), CActive::EPriorityIdle);
    iPointerTimer = CPeriodic::NewL(CActive::EPriorityLow);
    // moves too, not only taps
    Window().PointerFilter(EPointerFilterMove | EPointerFilterDrag, 0);
    SetRect(aRect);
    ActivateL();
    }

CBrView::~CBrView()
    {
    delete iRelayout;
    if (iPointerTimer) iPointerTimer->Cancel();
    delete iPointerTimer;
    bw_layout_free(iLayout);
    bw_doc_free(iDoc);
    delete iBufferGc;
    delete iBufferDev;
    delete iBuffer;
    iListItems.ResetAndDestroy();
    }

void CBrView::EnsureBufferL()
    {
    TSize s = Rect().Size();
    if (iBuffer && iBuffer->SizeInPixels() == s) return;
    delete iBufferGc; iBufferGc = NULL;
    delete iBufferDev; iBufferDev = NULL;
    delete iBuffer; iBuffer = NULL;
    iBuffer = new (ELeave) CFbsBitmap;
    User::LeaveIfError(iBuffer->Create(s, EColor64K));
    iBufferDev = CFbsBitmapDevice::NewL(iBuffer);
    User::LeaveIfError(iBufferDev->CreateContext(iBufferGc));
    }

void CBrView::SizeChanged()
    {
    TRAPD(err, EnsureBufferL());
    (void)err;
    if (iDoc) Relayout();
    else Render();
    }

void CBrView::SetFonts(CBrFonts& aFonts)
    {
    iFonts = &aFonts;
    if (iDoc) Relayout();
    }

// ------------------------------------------------------------------ the document

void CBrView::ClearDocument()
    {
    bw_layout_free(iLayout);
    iLayout = NULL;
    bw_doc_free(iDoc);
    iDoc = NULL;
    iScrollY = 0;
    iFocus = -1;
    Render();
    }

void CBrView::SetDocumentL(BwDoc* aDoc, TInt aScrollY, TInt aFocus)
    {
    bw_layout_free(iLayout);
    iLayout = NULL;
    bw_doc_free(iDoc);
    iDoc = aDoc;
    iScrollY = 0;
    iFocus = -1;
    iListOpen = EFalse;
    Relayout();
    iScrollY = aScrollY;
    ClampScroll();
    if (aFocus >= 0 && iDoc && (TUint)aFocus < iDoc->targetCount && TargetVisible(aFocus, EFalse)) iFocus = aFocus;
    // the link under the pointer in the new layout (a :hover style may have moved things)
    iHoverTarget = -1;
    if (iPointerOn && iPointerPos.iX >= 0) HoverAt(iPointerPos, EFalse);
    Render();
    }

void CBrView::Relayout()
    {
    iRelayoutPending = EFalse;
    if (!iDoc) { Render(); return; }
    TInt width = iDoc->fullSize ? KFullWidth : PageAreaW() - KScrollBarW;
    if (!iDoc->fullSize) iScrollX = 0;
    // keep the first text on the screen where it is
    TInt anchorOff = -1, anchorDy = 0;
    if (iLayout && iScrollY > 0)
        {
        for (TUint i = bw_layout_first_box(iLayout, iScrollY); i < iLayout->boxCount; i++)
            {
            const BwBox& b = iLayout->boxes[i];
            if (b.kind == BW_BOX_TEXT && b.y >= iScrollY) { anchorOff = (TInt)b.off; anchorDy = b.y - iScrollY; break; }
            }
        }
    iFindBox = -1;
    TUint32 t0 = TickMs();
    BwLayout* l = bw_layout(iDoc, width, &iFonts->Metrics());
    iLayoutMs = (TInt)(TickMs() - t0);
    if (!l) { Render(); return; }
    bw_layout_free(iLayout);
    iLayout = l;
    iLayoutW = width;
    iImages.SetMaxSize(TSize(width - 8, 1200));
    if (anchorOff >= 0)
        {
        for (TUint i = 0; i < iLayout->boxCount; i++)
            {
            const BwBox& b = iLayout->boxes[i];
            if (b.kind == BW_BOX_TEXT && (TInt)b.off <= anchorOff && anchorOff < (TInt)(b.off + b.len))
                {
                iScrollY = b.y - anchorDy;
                break;
                }
            }
        }
    ClampScroll();
    if (iFocus >= 0 && (TUint)iFocus >= iDoc->targetCount) iFocus = -1;
    Render();
    }

TInt CBrView::RelayoutCallback(TAny* aSelf)
    {
    ((CBrView*)aSelf)->Relayout();
    return 0;
    }

void CBrView::ImageArrived(const TDesC8& aUrl, TBool aOk, TSize aFullSize)
    {
    if (!iDoc) return;
    TBool sizeChanged = EFalse, any = EFalse;
    for (TUint i = 0; i < iDoc->imageCount; i++)
        {
        BwImage& im = iDoc->images[i];
        if (!im.src) continue;
        TPtrC8 src((const TUint8*)im.src);
        if (src != aUrl) continue;
        any = ETrue;
        if (!aOk)
            {
            if (!im.failed) { im.failed = 1; if (!(im.attrW && im.attrH)) sizeChanged = ETrue; }
            }
        else if (im.natW != aFullSize.iWidth || im.natH != aFullSize.iHeight)
            {
            im.natW = aFullSize.iWidth;
            im.natH = aFullSize.iHeight;
            if (!(im.attrW && im.attrH)) sizeChanged = ETrue;
            }
        }
    if (!any) return;
    if (sizeChanged)
        {
        if (!iRelayout->IsActive()) iRelayout->CallBack();
        iRelayoutPending = ETrue;
        }
    else if (!iRelayoutPending) Render();
    }

void CBrView::SetStats(const TDesC& aText)
    {
    if (iStats == aText.Left(iStats.MaxLength())) return;
    iStats.Copy(aText.Left(iStats.MaxLength()));
    // only the bar: the page stays as it is in the buffer
    if (!iBufferGc || iListOpen) return;
    iBufferGc->Reset();
    if (iSideBar) RenderSide(*iBufferGc); else RenderHeader(*iBufferGc);
    iBufferGc->DiscardFont();
    DrawDeferred();
    }

void CBrView::SetStatus(const TDesC& aText)
    {
    if (iStatusText == aText.Left(iStatusText.MaxLength())) return;
    iStatusText.Copy(aText.Left(iStatusText.MaxLength()));
    Render();
    }

void CBrView::SetTitle(const TDesC& aText)
    {
    iTitle.Copy(aText.Left(iTitle.MaxLength()));
    Render();
    }

void CBrView::SetLoading(TBool aLoading)
    {
    iLoading = aLoading;
    Render();
    }

void CBrView::ClampScroll()
    {
    TInt h = PageHeightOnScreen();
    TInt max = iLayout ? iLayout->height - h : 0;
    if (iScrollY > max) iScrollY = max;
    if (iScrollY < 0) iScrollY = 0;
    TInt maxX = iLayout ? iLayoutW - PageWidthOnScreen() : 0;
    if (iScrollX > maxX) iScrollX = maxX;
    if (iScrollX < 0) iScrollX = 0;
    }

TInt CBrView::PageWidthOnScreen() const
    {
    return PageAreaW() - KScrollBarW;
    }

void CBrView::ScrollXBy(TInt aDx)
    {
    iScrollX += aDx;
    ClampScroll();
    Render();
    }

void CBrView::ScrollTo(TInt aY)
    {
    iScrollY = aY;
    ClampScroll();
    Render();
    }

void CBrView::ScrollToAnchor(const TDesC8& aName)
    {
    if (!iDoc || !iLayout) return;
    HBufC8* z = HBufC8::New(aName.Length() + 1);
    if (!z) return;
    z->Des().Copy(aName);
    z->Des().ZeroTerminate();
    TInt a = bw_doc_find_anchor(iDoc, (const char*)z->Ptr());
    delete z;
    if (a >= 0) { iFocus = -1; ScrollTo(iLayout->anchorY[a] - 2); }
    else if (aName.Length() == 0 || aName == _L8("top")) ScrollTo(0);
    }

const TInt KPointerTickUs = 120000;
const TInt KHoverRestTicks = 3;            // ~0.35 s at rest before the page is styled for :hover

void CBrView::SetPointerOn(TBool aOn)
    {
    iPointerOn = aOn;
    if (!aOn && iHoverTarget >= 0) { iHoverTarget = -1; Render(); }
    UpdatePointerTimer();
    }

void CBrView::SetForeground(TBool aForeground)
    {
    iForeground = aForeground;
    UpdatePointerTimer();
    }

void CBrView::ResetHover()
    {
    iHoverTarget = -1;
    iHoverElem = -1;
    iHoverSent = -1;
    iHoverQuiet = 0;
    iScrollX = 0;
    }

void CBrView::UpdatePointerTimer()
    {
    if (!iPointerTimer) return;
    TBool run = iPointerOn && iForeground;
    if (run && !iPointerTimer->IsActive()) iPointerTimer->Start(KPointerTickUs, KPointerTickUs, TCallBack(PointerTick, this));
    else if (!run) iPointerTimer->Cancel();
    }

TInt CBrView::PointerTick(TAny* aSelf)
    {
    CBrView* v = (CBrView*)aSelf;
    if (!v->IsVisible() || v->iListOpen) return 1;
    TPoint p = v->iCoeEnv->WsSession().PointerCursorPosition() - v->PositionRelativeToScreen();
    TRAPD(err, v->HoverAt(p, ETrue));
    (void)err;
    return 1;
    }

// What is under the pointer: the link is highlighted and its address shown in the header; resting
// on an element styles the page for :hover; resting at the top or bottom edge scrolls, faster and
// faster.
void CBrView::HoverAt(TPoint aPos, TBool aTick)
    {
    iPointerPos = aPos;
    if (aPos.iY < iPointerMinY) iPointerMinY = aPos.iY;
    TInt w = PageAreaW();
    TInt hgt = Rect().Height();
    TBool redraw = EFalse;
    if (aTick && iLayout)
        {
        TInt dir = 0;
        // the cursor stops a few pixels short of the screen's top, so the top strip is 12 px (the
        // side bar's top included); the bottom one is reached
        if (aPos.iX >= 0 && aPos.iX < Rect().Width())
            {
            // the top: the strip, or as high as the cursor goes (if that is in the top quarter)
            TBool atTop = aPos.iY < PageTop() + KEdgeTop ||
                          (iPointerMinY < hgt / 4 && aPos.iY <= iPointerMinY + 2);
            if (atTop) dir = -1;
            else if (aPos.iY >= hgt - 3 && aPos.iX < w) dir = 1;
            }
        // sideways at the original size: the left and right edges
        TInt dirX = 0;
        if (iLayoutW > PageWidthOnScreen() && aPos.iY >= PageTop() + KEdgeTop && aPos.iY < hgt - 3)
            {
            if (aPos.iX < KEdgeSide && iScrollX > 0) dirX = -1;
            else if (aPos.iX >= w - 3 && iScrollX + PageWidthOnScreen() < iLayoutW) dirX = 1;
            }
        if (dirX)
            {
            iEdgeTicks++;
            if (iEdgeTicks > 2)
                {
                TInt step = 8 + (iEdgeTicks - 3) * 8;
                if (step > 120) step = 120;
                iScrollX += dirX * step;
                ClampScroll();
                redraw = ETrue;
                }
            }
        else if (dir && ((dir < 0 && iScrollY > 0) || (dir > 0 && iScrollY + PageHeightOnScreen() < iLayout->height)))
            {
            iEdgeTicks++;
            if (iEdgeTicks > 2)
                {
                TInt step = 8 + (iEdgeTicks - 3) * 6;
                if (step > 90) step = 90;
                iScrollY += dir * step;
                ClampScroll();
                redraw = ETrue;
                }
            }
        else iEdgeTicks = 0;
        }
    TInt t = -1;
    TInt elem = -1;
    if (iLayout && !iListOpen && aPos.iY >= PageTop() && aPos.iY < hgt && aPos.iX >= 0 && aPos.iX < w - KScrollBarW - 4)
        {
        TInt y = aPos.iY - PageTop() + iScrollY;
        t = bw_layout_hit(iLayout, aPos.iX + iScrollX, y);
        TInt bi = bw_layout_hit_box(iLayout, aPos.iX + iScrollX, y);
        if (bi >= 0) elem = iLayout->boxes[bi].elem;
        }
    if (t != iHoverTarget) { iHoverTarget = t; redraw = ETrue; }
    if (elem != iHoverElem) { iHoverElem = elem; iHoverQuiet = 0; }
    else if (aTick && elem != iHoverSent && ++iHoverQuiet >= KHoverRestTicks && iEdgeTicks <= 2)
        {
        iHoverSent = elem;
        if (redraw) Render();
        iAppUi.HoverElementL(elem);       // may lay the page out again
        return;
        }
    if (redraw) Render();
    }

// ------------------------------------------------------------------ drawing

void CBrView::Draw(const TRect& /*aRect*/) const
    {
    CWindowGc& gc = SystemGc();
    if (iBuffer) gc.BitBlt(Rect().iTl, iBuffer);
    else
        {
        gc.SetBrushColor(KRgbWhite);
        gc.Clear(Rect());
        }
    }

void CBrView::Render()
    {
    TRAPD(err, EnsureBufferL());
    if (err != KErrNone || !iBufferGc) return;
    CFbsBitGc& gc = *iBufferGc;
    gc.Reset();
    RenderPage(gc);
    if (iSideBar) RenderSide(gc); else RenderHeader(gc);
    if (iListOpen) RenderList(gc);
    gc.DiscardFont();
    DrawDeferred();
    }

void CBrView::RenderHeader(CFbsBitGc& aGc)
    {
    TRect r(TPoint(0, 0), TSize(Rect().Width(), iHeaderH));
    aGc.SetPenStyle(CGraphicsContext::ENullPen);
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    aGc.SetBrushColor(iLoading ? TRgb(0x60, 0x30, 0x10) : TRgb(0x1C, 0x3A, 0x6E));
    aGc.DrawRect(r);
    aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
    const CFont* f = iEikonEnv->DenseFont();
    aGc.UseFont(f);
    TInt base = (iHeaderH - f->HeightInPixels()) / 2 + f->AscentInPixels();
    // the status parts in one row
    TBuf<240> status;
    for (TInt i = 0; i < iStatusText.Length() && status.Length() < 236; i++)
        {
        if (iStatusText[i] == '\n') status.Append(_L(" \x00B7 "));
        else status.Append(iStatusText[i]);
        }
    if (iStats.Length() && status.Length() < 150)
        {
        if (status.Length()) status.Append(_L(" \x00B7 "));
        for (TInt i = 0; i < iStats.Length() && status.Length() < 236; i++) status.Append(iStats[i] == '\n' ? (TText)' ' : iStats[i]);
        }
    TInt statusW = status.Length() ? f->TextWidthInPixels(status) + 8 : 0;
    if (statusW > r.Width() / 2) statusW = r.Width() / 2;
    // left: the address of the link under the pointer (or the focused one), else the title
    TBuf<160> left;
    TInt shown = iHoverTarget >= 0 ? iHoverTarget : iFocus;
    if (!iLoading && iDoc && shown >= 0 && (TUint)shown < iDoc->targetCount && iDoc->targets[shown].kind == BW_TGT_LINK)
        {
        TPtrC8 h((const TUint8*)iDoc->links[iDoc->targets[shown].index].href);
        left.Copy(h.Left(left.MaxLength()));
        }
    else left.Copy(iTitle);
    TInt n = f->TextCount(left, r.Width() - statusW - 6);
    aGc.SetPenStyle(CGraphicsContext::ESolidPen);
    aGc.SetPenColor(KRgbWhite);
    aGc.DrawText(left.Left(n), TPoint(3, base));
    if (statusW)
        {
        aGc.SetPenColor(TRgb(0xFF, 0xE0, 0x80));
        TInt sn = f->TextCount(status, statusW - 4);
        aGc.DrawText(status.Left(sn), TPoint(r.Width() - statusW + 2, base));
        }
    }

void CBrView::SetBar(TInt aSideW)
    {
    TBool side = aSideW > 0;
    if (side == iSideBar && (!side || aSideW == iSideW)) return;
    iSideBar = side;
    if (side) iSideW = aSideW;
    // the page's width changed
    if (iDoc) Relayout(); else Render();
    }

// The bar at the right edge (where the side buttons are): the title at the top, the address of the
// link under the pointer (or the focused one) under it, the status at the bottom. The page keeps
// the whole height of the 200 px screen.
void CBrView::RenderSide(CFbsBitGc& aGc)
    {
    TRect r(PageAreaW(), 0, Rect().Width(), Rect().Height());
    aGc.SetPenStyle(CGraphicsContext::ENullPen);
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    aGc.SetBrushColor(iLoading ? TRgb(0x60, 0x30, 0x10) : TRgb(0x1C, 0x3A, 0x6E));
    aGc.DrawRect(r);
    aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
    const CFont* f = iEikonEnv->DenseFont();
    aGc.UseFont(f);
    TInt lh = f->HeightInPixels() + 1;
    TInt x = r.iTl.iX + 3;
    TInt w = iSideW - 5;
    TInt maxLines = (r.Height() - 4) / lh;
    // the status from the bottom up (up to 3 lines)
    TInt statusLines = 0;
    if (iStatusText.Length())
        {
        TInt pos = 0;
        while (pos < iStatusText.Length() && statusLines < 5)
            {
            TPtrC rest = iStatusText.Mid(pos);
            TInt nl = rest.Locate('\n');
            TInt n = f->TextCount(nl >= 0 ? rest.Left(nl) : rest, w);
            if (nl >= 0 && n >= nl) n = nl + 1;
            if (n <= 0) break;
            pos += n;
            statusLines++;
            }
        }
    // CPU and RAM at the very bottom
    TInt statsLines = 0;
    if (iStats.Length()) { statsLines = 1; for (TInt i = 0; i < iStats.Length(); i++) if (iStats[i] == '\n') statsLines++; }
    TInt statsY = r.iBr.iY - 2 - statsLines * lh;
    TInt bottomY = statsY - (statsLines ? 3 : 0) - statusLines * lh;
    // the link's address
    TBuf<200> link;
    TInt shown = iHoverTarget >= 0 ? iHoverTarget : iFocus;
    if (!iLoading && iDoc && shown >= 0 && (TUint)shown < iDoc->targetCount && iDoc->targets[shown].kind == BW_TGT_LINK)
        {
        TPtrC8 h((const TUint8*)iDoc->links[iDoc->targets[shown].index].href);
        link.Copy(h.Left(link.MaxLength()));
        }
    TInt free = maxLines - statusLines - statsLines;
    TInt linkLines = link.Length() ? Min(4, free / 2) : 0;
    TInt titleLines = free - linkLines;
    if (link.Length() && titleLines > 3) titleLines = 3;
    TInt y = r.iTl.iY + 2;
    aGc.SetPenStyle(CGraphicsContext::ESolidPen);
    aGc.SetPenColor(KRgbWhite);
    y = DrawWrapped(aGc, f, iTitle, x, y, w, titleLines);
    if (link.Length() && linkLines > 0)
        {
        aGc.SetPenColor(TRgb(0x80, 0x90, 0xB0));
        aGc.DrawLine(TPoint(x, y + 1), TPoint(x + w - 2, y + 1));
        y += 3;
        aGc.SetPenColor(TRgb(0xA0, 0xD0, 0xFF));
        DrawWrapped(aGc, f, link, x, y, w, linkLines);
        }
    if (statusLines)
        {
        aGc.SetPenColor(TRgb(0xFF, 0xE0, 0x80));
        DrawWrapped(aGc, f, iStatusText, x, bottomY, w, statusLines);
        }
    if (statsLines)
        {
        aGc.SetPenColor(TRgb(0x50, 0x68, 0x98));
        aGc.DrawLine(TPoint(x, statsY - 2), TPoint(x + w - 2, statsY - 2));
        aGc.SetPenColor(TRgb(0xB0, 0xBC, 0xD8));
        DrawWrapped(aGc, f, iStats, x, statsY, w, statsLines);
        }
    }

// Draws the text in lines of at most aW pixels (breaking after a space when there is one in the
// line, else anywhere, as for addresses); the last line that fits ends in "..." if text is left.
// The y below the last line.
TInt CBrView::DrawWrapped(CFbsBitGc& aGc, const CFont* aFont, const TDesC& aText, TInt aX, TInt aY, TInt aW, TInt aMaxLines)
    {
    TInt lh = aFont->HeightInPixels() + 1;
    TInt pos = 0;
    TInt len = aText.Length();
    for (TInt line = 0; line < aMaxLines && pos < len; line++)
        {
        while (pos < len && aText[pos] == ' ') pos++;
        TPtrC all = aText.Mid(pos);
        // an explicit line break ends the line
        TInt nl = all.Locate('\n');
        TPtrC rest = nl >= 0 ? all.Left(nl) : all;
        TInt n = aFont->TextCount(rest, aW);
        if (n <= 0 && nl == 0) { pos++; line--; continue; }
        if (n <= 0) break;
        if (n >= rest.Length() && nl >= 0)
            {
            aGc.DrawText(rest, TPoint(aX, aY + aFont->AscentInPixels()));
            aY += lh;
            pos += nl + 1;
            continue;
            }
        if (n < rest.Length() || (nl >= 0 && line == aMaxLines - 1))
            {
            if (line == aMaxLines - 1)
                {
                // the last line: cut with an ellipsis
                _LIT(KDots, "...");
                TInt m = aFont->TextCount(rest, aW - aFont->TextWidthInPixels(KDots));
                TBuf<120> t;
                t.Copy(rest.Left(Min(m, 116)));
                t.Append(KDots);
                aGc.DrawText(t, TPoint(aX, aY + aFont->AscentInPixels()));
                return aY + lh;
                }
            TInt sp = rest.Left(n).LocateReverse(' ');
            if (sp > n / 3) n = sp + 1;
            }
        aGc.DrawText(rest.Left(n), TPoint(aX, aY + aFont->AscentInPixels()));
        aY += lh;
        pos += n;
        }
    return aY;
    }

void CBrView::DrawImage(CFbsBitGc& aGc, const BwBox& aBox, const TRect& aRect, TInt aPriority)
    {
    const BwImage& im = iDoc->images[aBox.ref];
    CFbsBitmap* bmp = im.src && !im.failed ? iImages.Get(im.src, aPriority) : NULL;
    if (bmp)
        {
        TSize s = bmp->SizeInPixels();
        if (s == aRect.Size()) aGc.BitBlt(aRect.iTl, bmp);
        else aGc.DrawBitmap(aRect, bmp);
        if (aBox.target >= 0 && aBox.target == iFocus)
            {
            aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
            aGc.SetPenStyle(CGraphicsContext::ESolidPen);
            aGc.SetPenColor(TRgb(0x00, 0x60, 0xFF));
            aGc.SetPenSize(TSize(2, 2));
            aGc.DrawRect(aRect);
            aGc.SetPenSize(TSize(1, 1));
            }
        else if (aBox.target >= 0 && aBox.target == iHoverTarget)
            {
            aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
            aGc.SetPenStyle(CGraphicsContext::ESolidPen);
            aGc.SetPenColor(TRgb(0x40, 0x80, 0xFF));
            aGc.DrawRect(aRect);
            }
        return;
        }
    // a frame with the alt text while it loads
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    aGc.SetBrushColor(aBox.target >= 0 && aBox.target == iFocus ? TRgb(0xCC, 0xE0, 0xFF) : TRgb(0xEE, 0xEE, 0xEE));
    aGc.SetPenStyle(CGraphicsContext::ESolidPen);
    aGc.SetPenColor(aBox.target >= 0 ? TRgb(0x40, 0x60, 0xC0) : TRgb(0xAA, 0xAA, 0xAA));
    aGc.DrawRect(aRect);
    aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
    if (im.altLen && aRect.Height() >= 10)
        {
        const CFont* f = iFonts->Font(aBox.font & ~(BW_FONT_BOLD | BW_FONT_ITALIC));
        aGc.UseFont(f);
        aGc.SetClippingRect(aRect);
        aGc.SetPenColor(TRgb(0x55, 0x55, 0x55));
        TPtrC alt((const TUint16*)iDoc->pool + im.altOff, im.altLen);
        aGc.DrawText(alt, TPoint(aRect.iTl.iX + 3, aRect.iTl.iY + 2 + f->AscentInPixels()));
        aGc.CancelClippingRect();
        }
    }

void CBrView::DrawControl(CFbsBitGc& aGc, const BwBox& aBox, const TRect& aRect, TBool aFocused)
    {
    const BwControl& c = iDoc->controls[aBox.ref];
    const CFont* f = iFonts->Font(aBox.font);
    aGc.UseFont(f);
    TRgb border = aFocused ? TRgb(0x00, 0x60, 0xFF) : TRgb(0x80, 0x80, 0x80);
    aGc.SetPenStyle(CGraphicsContext::ESolidPen);
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    if (aFocused) aGc.SetPenSize(TSize(2, 2));
    TInt base = aRect.iTl.iY + (aRect.Height() - f->HeightInPixels()) / 2 + f->AscentInPixels();
    switch (c.type)
        {
        case BW_CTL_CHECKBOX:
            aGc.SetBrushColor(KRgbWhite);
            aGc.SetPenColor(border);
            aGc.DrawRect(aRect);
            if (c.checked)
                {
                aGc.SetPenSize(TSize(2, 2));
                aGc.SetPenColor(KRgbBlack);
                TRect in(aRect);
                in.Shrink(3, 3);
                aGc.DrawLine(TPoint(in.iTl.iX, in.iTl.iY + in.Height() / 2), TPoint(in.iTl.iX + in.Width() / 3, in.iBr.iY - 1));
                aGc.DrawLine(TPoint(in.iTl.iX + in.Width() / 3, in.iBr.iY - 1), TPoint(in.iBr.iX - 1, in.iTl.iY));
                }
            break;
        case BW_CTL_RADIO:
            aGc.SetBrushColor(KRgbWhite);
            aGc.SetPenColor(border);
            aGc.DrawEllipse(aRect);
            if (c.checked)
                {
                TRect in(aRect);
                in.Shrink(aRect.Width() / 4 + 1, aRect.Height() / 4 + 1);
                aGc.SetPenSize(TSize(1, 1));
                aGc.SetPenColor(KRgbBlack);
                aGc.SetBrushColor(KRgbBlack);
                aGc.DrawEllipse(in);
                }
            break;
        case BW_CTL_SUBMIT: case BW_CTL_BUTTON: case BW_CTL_RESET: case BW_CTL_IMAGE: case BW_CTL_FILE:
            {
            aGc.SetBrushColor(aFocused ? TRgb(0xD8, 0xE6, 0xFF) : TRgb(0xE0, 0xE0, 0xE0));
            aGc.SetPenColor(border);
            aGc.DrawRect(aRect);
            unsigned int n = 0;
            const unsigned short* t = bw_control_text(&c, &n);
            TPtrC label((const TUint16*)t, n);
            aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
            aGc.SetPenColor(c.type == BW_CTL_FILE ? TRgb(0x88, 0x88, 0x88) : KRgbBlack);
            aGc.SetClippingRect(aRect);
            TInt tw = f->TextWidthInPixels(label);
            aGc.DrawText(label, TPoint(aRect.iTl.iX + Max(2, (aRect.Width() - tw) / 2), base));
            aGc.CancelClippingRect();
            }
            break;
        default:
            {
            // text, password, textarea, select
            aGc.SetBrushColor(c.disabled == 1 ? TRgb(0xEE, 0xEE, 0xEE) : KRgbWhite);
            aGc.SetPenColor(border);
            aGc.DrawRect(aRect);
            aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
            TRect clip(aRect);
            clip.Shrink(3, 2);
            if (c.type == BW_CTL_SELECT) clip.iBr.iX -= 12;
            aGc.SetClippingRect(clip);
            unsigned int n = 0;
            const unsigned short* t = bw_control_text(&c, &n);
            TBool placeholder = EFalse;
            if (!n && c.labelLen && (c.type == BW_CTL_TEXT || c.type == BW_CTL_TEXTAREA)) { t = c.label; n = c.labelLen; placeholder = ETrue; }
            aGc.SetPenColor(placeholder || c.disabled == 1 ? TRgb(0x99, 0x99, 0x99) : KRgbBlack);
            if (c.type == BW_CTL_PASSWORD)
                {
                TBuf<40> stars;
                for (unsigned int i = 0; i < n && i < 40; i++) stars.Append('*');
                aGc.DrawText(stars, TPoint(clip.iTl.iX, base));
                }
            else if (c.type == BW_CTL_TEXTAREA)
                {
                TInt y = aRect.iTl.iY + 3 + f->AscentInPixels();
                TInt lh = f->HeightInPixels() + 2;
                unsigned int s = 0;
                while (s < n && y < aRect.iBr.iY)
                    {
                    unsigned int e = s;
                    while (e < n && t[e] != '\n') e++;
                    TPtrC line((const TUint16*)t + s, e - s);
                    aGc.DrawText(line, TPoint(clip.iTl.iX, y));
                    y += lh;
                    s = e + 1;
                    }
                }
            else if (t)
                {
                TPtrC v((const TUint16*)t, n);
                // a long value shows its end, as an edit field would
                TInt w = f->TextWidthInPixels(v);
                TInt x = clip.iTl.iX;
                if (w > clip.Width() && !placeholder) x = clip.iBr.iX - w;
                aGc.DrawText(v, TPoint(x, base));
                }
            aGc.CancelClippingRect();
            if (c.type == BW_CTL_SELECT)
                {
                TInt cx = aRect.iBr.iX - 9, cy = aRect.iTl.iY + aRect.Height() / 2 - 2;
                aGc.SetPenSize(TSize(1, 1));
                aGc.SetPenColor(KRgbBlack);
                for (TInt k = 0; k < 4; k++) aGc.DrawLine(TPoint(cx - 3 + k, cy + k), TPoint(cx + 4 - k, cy + k));
                }
            }
            break;
        }
    aGc.SetPenSize(TSize(1, 1));
    aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
    }

void CBrView::RenderPage(CFbsBitGc& aGc)
    {
    TInt top = PageTop();
    TInt h = PageHeightOnScreen();
    TInt w = PageAreaW();
    TRect area(TPoint(0, top), TSize(w, h));
    aGc.SetPenStyle(CGraphicsContext::ENullPen);
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    aGc.SetBrushColor(iDoc ? Rgb(iDoc->bgColor) : KRgbWhite);
    aGc.DrawRect(area);
    if (!iDoc || !iLayout) return;
    aGc.SetClippingRect(area);
    // block backgrounds
    for (TUint i = 0; i < iLayout->fillCount; i++)
        {
        const BwFill& f = iLayout->fills[i];
        if (f.h <= 0 || f.y + f.h <= iScrollY || f.y >= iScrollY + h) continue;
        if (f.x + f.w <= iScrollX || f.x - iScrollX >= w) continue;
        TRect fr(TPoint(f.x - iScrollX, top + f.y - iScrollY), TSize(f.w, f.h));
        if (f.outline)
            {
            aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
            aGc.SetPenStyle(CGraphicsContext::ESolidPen);
            aGc.SetPenColor(Rgb(f.color));
            aGc.DrawRect(fr);
            aGc.SetPenStyle(CGraphicsContext::ENullPen);
            aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
            }
        else
            {
            aGc.SetBrushColor(Rgb(f.color));
            aGc.DrawRect(fr);
            }
        }
    const CFont* current = NULL;
    TInt currentId = -1;
    iImages.BeginPass();
    TInt viewBottom = iScrollY + h;
    // by lines (a table row is one line, its cells side by side): from a screen above (their
    // pictures load too) to two screens below
    TUint line = bw_layout_first_line(iLayout, iScrollY - h);
    TUint lineEnd = line < iLayout->lineCount ? iLayout->lines[line].first : iLayout->boxCount;
    for (TUint i = lineEnd; i < iLayout->boxCount; i++)
        {
        while (line < iLayout->lineCount && i >= (line + 1 < iLayout->lineCount ? iLayout->lines[line + 1].first : iLayout->boxCount)) line++;
        if (line < iLayout->lineCount && iLayout->lines[line].top > viewBottom + 2 * h) break;
        const BwBox& b = iLayout->boxes[i];
        TBool visible = b.y < viewBottom && b.y + b.h > iScrollY && b.x + b.w > iScrollX && b.x - iScrollX < w;
        if (!visible)
            {
            // near the screen: only ask for its picture, farther = later
            if (b.kind == BW_BOX_IMAGE)
                {
                const BwImage& im = iDoc->images[b.ref];
                if (im.src && !im.failed)
                    {
                    TInt d = b.y >= viewBottom ? b.y - viewBottom : iScrollY - (b.y + b.h);
                    iImages.Get(im.src, 1000 + d);
                    }
                }
            continue;
            }
        TRect r(TPoint(b.x - iScrollX, top + b.y - iScrollY), TSize(b.w, b.h));
        TBool focused = b.target >= 0 && b.target == iFocus;
        switch (b.kind)
            {
            case BW_BOX_TEXT:
                {
                if (b.font != currentId || !current)
                    {
                    current = iFonts->Font(b.font);
                    currentId = b.font;
                    aGc.UseFont(current);
                    }
                if (focused)
                    {
                    aGc.SetPenStyle(CGraphicsContext::ENullPen);
                    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
                    aGc.SetBrushColor(TRgb(0xC4, 0xDA, 0xFF));
                    TRect fr(r);
                    fr.Grow(1, 0);
                    aGc.DrawRect(fr);
                    }
                TPtrC t((const TUint16*)iDoc->pool + b.off, b.len);
                if (b.bg >= 0 && !focused)
                    {
                    // an inline element's background (a label, a highlighted word)
                    aGc.SetPenStyle(CGraphicsContext::ENullPen);
                    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
                    aGc.SetBrushColor(Rgb(b.bg));
                    aGc.DrawRect(r);
                    }
                if ((TInt)i == iFindBox && iFindOff + iFindLen <= (TInt)b.len)
                    {
                    // the found text on yellow
                    TInt fx = r.iTl.iX + current->TextWidthInPixels(t.Left(iFindOff));
                    TInt fw = current->TextWidthInPixels(t.Mid(iFindOff, iFindLen));
                    aGc.SetPenStyle(CGraphicsContext::ENullPen);
                    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
                    aGc.SetBrushColor(TRgb(0xFF, 0xE0, 0x40));
                    aGc.DrawRect(TRect(TPoint(fx, r.iTl.iY), TSize(fw, r.Height())));
                    }
                aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
                aGc.SetPenStyle(CGraphicsContext::ESolidPen);
                aGc.SetPenColor(focused ? TRgb(0x00, 0x00, 0x60) : Rgb(b.color));
                TInt base = r.iTl.iY + current->AscentInPixels() + 1;
                aGc.DrawText(t, TPoint(r.iTl.iX, base));
                if ((b.flags & BW_OPF_UNDERLINE) || (b.target >= 0 && b.target == iHoverTarget))
                    aGc.DrawLine(TPoint(r.iTl.iX, base + 1), TPoint(r.iBr.iX, base + 1));
                if (b.flags & BW_OPF_STRIKE)
                    {
                    TInt my = base - current->AscentInPixels() / 3;
                    aGc.DrawLine(TPoint(r.iTl.iX, my), TPoint(r.iBr.iX, my));
                    }
                }
                break;
            case BW_BOX_IMAGE:
                DrawImage(aGc, b, r, Abs(b.y - iScrollY) / 8);
                currentId = -1;
                break;
            case BW_BOX_CONTROL:
                DrawControl(aGc, b, r, focused);
                currentId = -1;
                break;
            case BW_BOX_RULE:
                aGc.SetPenStyle(CGraphicsContext::ENullPen);
                aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
                aGc.SetBrushColor(Rgb(b.color));
                aGc.DrawRect(r);
                aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
                break;
            default:
                break;
            }
        }
    iImages.EndPass();
    aGc.CancelClippingRect();
    // the scroll bar
    if (iLayout->height > h)
        {
        TInt th = h * h / iLayout->height;
        if (th < 10) th = 10;
        TInt ty = top + (h - th) * iScrollY / Max(1, iLayout->height - h);
        aGc.SetPenStyle(CGraphicsContext::ENullPen);
        aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        aGc.SetBrushColor(TRgb(0xDD, 0xDD, 0xDD));
        aGc.DrawRect(TRect(TPoint(w - KScrollBarW, top), TSize(KScrollBarW, h)));
        aGc.SetBrushColor(TRgb(0x60, 0x70, 0x90));
        aGc.DrawRect(TRect(TPoint(w - KScrollBarW, ty), TSize(KScrollBarW, th)));
        }
    // sideways
    TInt pw = PageWidthOnScreen();
    if (iLayoutW > pw)
        {
        TInt tw = pw * pw / iLayoutW;
        TInt tx = (pw - tw) * iScrollX / Max(1, iLayoutW - pw);
        aGc.SetPenStyle(CGraphicsContext::ENullPen);
        aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        aGc.SetBrushColor(TRgb(0xDD, 0xDD, 0xDD));
        aGc.DrawRect(TRect(TPoint(0, top + h - KScrollBarH), TSize(pw, KScrollBarH)));
        aGc.SetBrushColor(TRgb(0x60, 0x70, 0x90));
        aGc.DrawRect(TRect(TPoint(tx, top + h - KScrollBarH), TSize(tw, KScrollBarH)));
        }
    }

void CBrView::RenderList(CFbsBitGc& aGc)
    {
    const CFont* f = iEikonEnv->NormalFont();
    TInt lh = f->HeightInPixels() + 4;
    TInt rows = iListItems.Count();
    TInt maxRows = (Rect().Height() - 24) / lh;
    if (maxRows > KListMaxRows) maxRows = KListMaxRows;
    if (rows > maxRows) rows = maxRows;
    if (rows < 1) rows = 1;
    TInt w = Rect().Width() * 4 / 5;
    TInt hgt = rows * lh + lh + 6;
    TPoint tl((Rect().Width() - w) / 2, (Rect().Height() - hgt) / 2);
    TRect box(tl, TSize(w, hgt));
    aGc.SetPenStyle(CGraphicsContext::ESolidPen);
    aGc.SetPenColor(TRgb(0x30, 0x30, 0x30));
    aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
    aGc.SetBrushColor(TRgb(0xF4, 0xF4, 0xF4));
    aGc.DrawRect(box);
    aGc.SetBrushColor(TRgb(0x1C, 0x3A, 0x6E));
    aGc.DrawRect(TRect(tl, TSize(w, lh + 2)));
    aGc.UseFont(f);
    aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
    aGc.SetPenColor(KRgbWhite);
    TBuf<100> title(iListTitle);
    if (iListItems.Count() > rows) { title.AppendFormat(_L("  (%d/%d)"), iListSel + 1, iListItems.Count()); }
    aGc.DrawText(title.Left(f->TextCount(title, w - 8)), TPoint(tl.iX + 4, tl.iY + 2 + f->AscentInPixels()));
    if (iListSel < iListFirst) iListFirst = iListSel;
    if (iListSel >= iListFirst + rows) iListFirst = iListSel - rows + 1;
    for (TInt i = 0; i < rows && iListFirst + i < iListItems.Count(); i++)
        {
        TInt idx = iListFirst + i;
        TRect row(TPoint(tl.iX + 2, tl.iY + lh + 4 + i * lh), TSize(w - 4, lh));
        if (idx == iListSel)
            {
            aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
            aGc.SetBrushColor(TRgb(0x30, 0x60, 0xC0));
            aGc.SetPenStyle(CGraphicsContext::ENullPen);
            aGc.DrawRect(row);
            aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
            aGc.SetPenStyle(CGraphicsContext::ESolidPen);
            aGc.SetPenColor(KRgbWhite);
            }
        else aGc.SetPenColor(KRgbBlack);
        const TDesC& t = *iListItems[idx];
        aGc.DrawText(t.Left(f->TextCount(t, w - 12)), TPoint(row.iTl.iX + 4, row.iTl.iY + 2 + f->AscentInPixels()));
        }
    }

// ------------------------------------------------------------------ the list

void CBrView::ShowListL(TInt aId, const TDesC& aTitle, const RPointerArray<HBufC>& aItems, TInt aSelected, MListObserver& aObserver)
    {
    iListItems.ResetAndDestroy();
    for (TInt i = 0; i < aItems.Count(); i++)
        {
        HBufC* c = aItems[i]->AllocLC();
        User::LeaveIfError(iListItems.Append(c));
        CleanupStack::Pop(c);
        }
    iListId = aId;
    iListTitle.Copy(aTitle.Left(iListTitle.MaxLength()));
    iListSel = aSelected >= 0 && aSelected < iListItems.Count() ? aSelected : 0;
    iListFirst = 0;
    iListObserver = &aObserver;
    iListOpen = ETrue;
    Render();
    }

TKeyResponse CBrView::ListKeyL(const TKeyEvent& aKeyEvent)
    {
    TInt n = iListItems.Count();
    TBool shift = (aKeyEvent.iModifiers & EModifierShift) != 0;
    switch (aKeyEvent.iCode)
        {
        case EKeyUpArrow: iListSel -= shift ? KListMaxRows : 1; if (iListSel < 0) iListSel = shift ? 0 : n - 1; break;
        case EKeyDownArrow: iListSel += shift ? KListMaxRows : 1; if (iListSel >= n) iListSel = shift ? n - 1 : 0; break;
        case EKeyEnter: case EKeyDevice3: case ' ':
            {
            iListOpen = EFalse;
            Render();
            MListObserver* o = iListObserver;
            if (o) o->ListChosen(iListId, n ? iListSel : -1);
            return EKeyWasConsumed;
            }
        case EKeyEscape: case EKeyBackspace: case EKeyLeftArrow:
            {
            iListOpen = EFalse;
            Render();
            MListObserver* o = iListObserver;
            if (o) o->ListChosen(iListId, -1);
            return EKeyWasConsumed;
            }
        default:
            return EKeyWasConsumed;
        }
    Render();
    return EKeyWasConsumed;
    }

// ------------------------------------------------------------------ focus and scrolling

TBool CBrView::TargetVisible(TInt aTarget, TBool aFully) const
    {
    if (!iLayout || aTarget < 0 || !iDoc || (TUint)aTarget >= iDoc->targetCount) return EFalse;
    const BwTargetPos& p = iLayout->targets[aTarget];
    if (p.y0 < 0) return EFalse;
    TInt h = PageHeightOnScreen();
    if (aFully) return p.y0 >= iScrollY && p.y1 <= iScrollY + h;
    return p.y1 > iScrollY && p.y0 < iScrollY + h;
    }

void CBrView::SetFocusTarget(TInt aTarget)
    {
    iFocus = aTarget;
    iAppUi.LinkFocused(aTarget);
    }

void CBrView::EnsureFocusVisible()
    {
    if (iFocus < 0 || !iLayout) return;
    const BwTargetPos& p = iLayout->targets[iFocus];
    if (p.y0 < 0) return;
    TInt h = PageHeightOnScreen();
    if (p.y0 < iScrollY) iScrollY = p.y0 - 8;
    else if (p.y1 > iScrollY + h)
        {
        iScrollY = p.y1 - h + 8;
        if (iScrollY > p.y0 - 8) iScrollY = p.y0 - 8;
        }
    // sideways: its first box
    if (iLayoutW > PageWidthOnScreen())
        {
        for (TUint i = bw_layout_first_box(iLayout, p.y0); i < iLayout->boxCount && iLayout->boxes[i].y <= p.y1; i++)
            {
            const BwBox& b = iLayout->boxes[i];
            if (b.target != iFocus) continue;
            TInt pw = PageWidthOnScreen();
            if (b.x < iScrollX || b.x + Min((TInt)b.w, pw - 16) > iScrollX + pw) iScrollX = b.x - 16;
            break;
            }
        }
    ClampScroll();
    }

void CBrView::MoveFocus(TInt aDir)
    {
    if (!iDoc || !iLayout) return;
    TInt h = PageHeightOnScreen();
    TInt n = (TInt)iDoc->targetCount;
    if (iFocus >= 0 && !TargetVisible(iFocus, EFalse)) iFocus = -1;
    TInt cand = -1;
    if (iFocus < 0)
        {
        // the first (last) link that is fully on the screen
        if (aDir > 0) { for (TInt t = 0; t < n; t++) if (TargetVisible(t, ETrue)) { cand = t; break; } }
        else { for (TInt t = n - 1; t >= 0; t--) if (TargetVisible(t, ETrue)) { cand = t; break; } }
        }
    else
        {
        for (TInt t = iFocus + aDir; t >= 0 && t < n; t += aDir)
            {
            const BwTargetPos& p = iLayout->targets[t];
            if (p.y0 < 0) continue;
            // the next one counts if its top (bottom) is on the screen
            if (aDir > 0 && p.y0 >= iScrollY && p.y0 < iScrollY + h - 4) cand = t;
            if (aDir < 0 && p.y1 > iScrollY + 4 && p.y1 <= iScrollY + h) cand = t;
            break;
            }
        }
    if (cand >= 0)
        {
        SetFocusTarget(cand);
        // if it reaches past the edge, bring it in, but not more than one step
        const BwTargetPos& p = iLayout->targets[cand];
        if (aDir > 0 && p.y1 > iScrollY + h) iScrollY += Min(KScrollStep, p.y1 - (iScrollY + h) + 4);
        if (aDir < 0 && p.y0 < iScrollY) iScrollY -= Min(KScrollStep, iScrollY - p.y0 + 4);
        EnsureFocusVisible();
        Render();
        return;
        }
    TInt before = iScrollY;
    iScrollY += aDir * KScrollStep;
    ClampScroll();
    if (iFocus >= 0 && !TargetVisible(iFocus, EFalse)) SetFocusTarget(-1);
    if (iScrollY != before || cand < 0) Render();
    }

void CBrView::JumpFocus(TInt aDir)
    {
    if (!iDoc || !iLayout || !iDoc->targetCount) return;
    TInt n = (TInt)iDoc->targetCount;
    TInt start = iFocus;
    if (start < 0 || !TargetVisible(start, EFalse))
        {
        // from the screen: the first link below its top (above its bottom)
        TInt h = PageHeightOnScreen();
        start = -1;
        if (aDir > 0)
            {
            for (TInt t = 0; t < n; t++) { const BwTargetPos& p = iLayout->targets[t]; if (p.y0 >= iScrollY) { SetFocusTarget(t); EnsureFocusVisible(); Render(); return; } }
            return;
            }
        for (TInt t = n - 1; t >= 0; t--) { const BwTargetPos& p = iLayout->targets[t]; if (p.y0 >= 0 && p.y1 <= iScrollY + h) { SetFocusTarget(t); EnsureFocusVisible(); Render(); return; } }
        return;
        }
    for (TInt t = start + aDir; t >= 0 && t < n; t += aDir)
        {
        if (iLayout->targets[t].y0 < 0) continue;
        SetFocusTarget(t);
        EnsureFocusVisible();
        Render();
        return;
        }
    }

// ------------------------------------------------------------------ input

// Keys: Up/Down go to the next / previous link or field when it is on the screen, else scroll a
// step (as the 9300's own Opera does); Ctrl+Up/Down scroll a step; Shift+Up/Down, Space/Shift+Space
// and PgUp/PgDn a page; Tab/Shift+Tab and Left/Right the next/previous link wherever it is
// (Left/Right scroll sideways at the original size); Enter opens the focused one, Space ticks a
// focused box or presses a focused button; Ctrl+Left/Right back/forward; Ctrl+L the address; a
// letter typed with a text field focused goes into it; + / - zoom (Ctrl+0: normal size).
TKeyResponse CBrView::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
    {
    if (aType != EEventKey) return EKeyWasNotConsumed;
    // which keys reach the page while the virtual cursor is on (the log, when it is on)
    if (aKeyEvent.iCode >= EKeyLeftArrow && aKeyEvent.iCode <= EKeyDownArrow)
        IgLogF2(_L("key %d mods %d"), (TInt)aKeyEvent.iCode, (TInt)aKeyEvent.iModifiers);
    if (iListOpen) return ListKeyL(aKeyEvent);
    TBool shift = (aKeyEvent.iModifiers & EModifierShift) != 0;
    TBool ctrl = (aKeyEvent.iModifiers & EModifierCtrl) != 0;
    TInt h = PageHeightOnScreen();
    TInt page = h - 20;
    TInt end = iLayout ? iLayout->height : 0;
    TBool fieldFocused = EFalse;
    const BwControl* fc = NULL;
    if (iDoc && iFocus >= 0 && (TUint)iFocus < iDoc->targetCount && iDoc->targets[iFocus].kind == BW_TGT_CONTROL)
        {
        fc = &iDoc->controls[iDoc->targets[iFocus].index];
        fieldFocused = fc->type == BW_CTL_TEXT || fc->type == BW_CTL_PASSWORD || fc->type == BW_CTL_TEXTAREA;
        }
    switch (aKeyEvent.iCode)
        {
        case EKeyDownArrow:
            if (shift) ScrollTo(iScrollY + page);
            else if (ctrl) ScrollTo(iScrollY + KScrollStep);
            else MoveFocus(1);
            return EKeyWasConsumed;
        case EKeyUpArrow:
            if (shift) ScrollTo(iScrollY - page);
            else if (ctrl) ScrollTo(iScrollY - KScrollStep);
            else MoveFocus(-1);
            return EKeyWasConsumed;
        case EKeyPageDown:
            ScrollTo(iScrollY + page);
            return EKeyWasConsumed;
        case EKeyPageUp:
            ScrollTo(iScrollY - page);
            return EKeyWasConsumed;
        case EKeyHome:
            iScrollX = 0;
            ScrollTo(0);
            return EKeyWasConsumed;
        case EKeyEnd:
            ScrollTo(end);
            return EKeyWasConsumed;
        case EKeyRightArrow:
            if (ctrl) iAppUi.HandleCommandL(EBrCmdForward);
            else if (iLayoutW > PageWidthOnScreen()) ScrollXBy(shift ? PageWidthOnScreen() - 40 : KScrollStep * 2);
            else JumpFocus(1);
            return EKeyWasConsumed;
        case EKeyLeftArrow:
            if (ctrl) iAppUi.HandleCommandL(EBrCmdBack);
            else if (iLayoutW > PageWidthOnScreen()) ScrollXBy(-(shift ? PageWidthOnScreen() - 40 : KScrollStep * 2));
            else JumpFocus(-1);
            return EKeyWasConsumed;
        case EKeyTab:
            JumpFocus(shift ? -1 : 1);
            return EKeyWasConsumed;
        case ' ':
            if (fc && !fieldFocused && !shift && fc->type != BW_CTL_SELECT)
                {
                // a box ticks, a button presses
                iAppUi.ActivateTargetL(iFocus);
                return EKeyWasConsumed;
                }
            if (fieldFocused && !shift && TargetVisible(iFocus, EFalse)) break;   // typed into the field
            ScrollTo(iScrollY + (shift ? -page : page));
            return EKeyWasConsumed;
        case EKeyEnter: case EKeyDevice3:
            if (iFocus >= 0)
                {
                if (!TargetVisible(iFocus, EFalse)) { EnsureFocusVisible(); Render(); }
                iAppUi.ActivateTargetL(iFocus);
                }
            else if (iHoverTarget >= 0) iAppUi.ActivateTargetL(iHoverTarget);
            return EKeyWasConsumed;
        default:
            break;
        }
    TUint code = aKeyEvent.iCode;
    if (ctrl)
        {
        // Ctrl+L: the address (the code may come as the letter or as control-L)
        if (code == 'l' || code == 'L' || code == 12) { iAppUi.GoToDialogL(); return EKeyWasConsumed; }
        // Ctrl+Plus / Ctrl+Minus / Ctrl+0: zoom (the text size steps)
        if (code == '+' || code == '=') { iAppUi.ZoomL(1); return EKeyWasConsumed; }
        if (code == '-' || code == '_') { iAppUi.ZoomL(-1); return EKeyWasConsumed; }
        if (code == '0') { iAppUi.ZoomL(0); return EKeyWasConsumed; }
        return EKeyWasNotConsumed;
        }
    if (code >= ' ' && code < 0xF700 && code != 0x7F && !(aKeyEvent.iModifiers & EModifierFunc))
        {
        // typed into the focused text field (if it is on the screen)
        if (fieldFocused && TargetVisible(iFocus, EFalse))
            {
            if (fc->disabled) iAppUi.ActivateTargetL(iFocus);    // says why not
            else iAppUi.EditControlL(iDoc->targets[iFocus].index, code);
            return EKeyWasConsumed;
            }
        // + / - (Shift+plus on the 9300) zoom; other keys do nothing on the page
        if (code == '+') { iAppUi.ZoomL(1); return EKeyWasConsumed; }
        if (code == '-') { iAppUi.ZoomL(-1); return EKeyWasConsumed; }
        return EKeyWasNotConsumed;
        }
    return EKeyWasNotConsumed;
    }

void CBrView::HandlePointerEventL(const TPointerEvent& aPointerEvent)
    {
    TPoint p = aPointerEvent.iPosition - Rect().iTl;
    if (aPointerEvent.iType == TPointerEvent::EMove || aPointerEvent.iType == TPointerEvent::EDrag)
        {
        if (iPointerOn) HoverAt(p, EFalse);
        return;
        }
    if (aPointerEvent.iType != TPointerEvent::EButton1Up) return;
    TInt h = PageHeightOnScreen();
    if (iListOpen) return;
    if (p.iY < PageTop()) { ScrollTo(iScrollY - h + 20); return; }        // the top bar: page up
    if (p.iX >= PageAreaW())
        {
        // the side bar: its top half pages up, the bottom half down
        ScrollTo(iScrollY + (p.iY < Rect().Height() / 2 ? -(h - 20) : h - 20));
        return;
        }
    if (p.iX >= PageAreaW() - KScrollBarW - 4)
        {
        // the scroll bar: jump there
        if (iLayout) ScrollTo((p.iY - PageTop()) * iLayout->height / Max(1, h) - h / 2);
        return;
        }
    if (!iLayout) return;
    TInt t = bw_layout_hit(iLayout, p.iX + iScrollX, p.iY - PageTop() + iScrollY);
    if (t >= 0)
        {
        SetFocusTarget(t);
        Render();
        iAppUi.ActivateTargetL(t);
        }
    else if (p.iY > Rect().Height() - 14) ScrollTo(iScrollY + h - 20);   // the bottom strip: page down
    }
