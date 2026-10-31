// Browser 9300 - the application: commands and menus, navigation with a history that keeps
// the last pages in RAM (Back without the network), bookmarks, the internal about: pages,
// forms, downloads, and the PC helpers (update from the PC, log and screenshot upload).

#include <eikenv.h>
#include <eikmenup.h>
#include <eikmenub.h>
#include <eikbtgpc.h>
#include <eikedwin.h>
#include <eikseced.h>
#include <baclipb.h>
#include <txtetext.h>
#include <eikvcurs.h>
#include <apgcli.h>
#include <hal.h>
#include <fbs.h>
#include <browser9300.rsg>
#include "browser.hrh"
#include "browser.h"

_LIT(KBookmarksFile, "C:\\System\\Data\\browser9300\\bookmarks.txt");
_LIT(KCookiesFile, "C:\\System\\Data\\browser9300\\cookies.txt");
_LIT(KUpdateFile, "C:\\System\\Temp\\browser9300_update.sis");
_LIT8(KUpdatePath, "/browser9300/browser9300.sis");

const TInt KHistoryMax = 40;
// Pages kept in RAM for Back / Forward and stylesheets for the next pages of a site: as much as
// the free RAM allows (pictures, which keep 3 MB free, give way first), up to these caps.
const TInt KCacheMaxBytes = 16 * 1024 * 1024;
const TInt KCacheOneMax = 4 * 1024 * 1024;
const TInt KCssCacheMax = 4 * 1024 * 1024;
const TInt KCssCacheMin = 900 * 1024;             // kept even when RAM is short (as before 0.12)
const TInt KCacheRamReserve = 2560 * 1024;        // the free RAM the caches leave for the system
const TInt KStatsTickUs = 1000000;

enum { EListSelect = 1, EListInfo = 2 };
enum { EHelperNone = 0, EHelperUpdate, EHelperLog, EHelperShot };

// ------------------------------------------------------------------ the text dialog

void CTextDialog::PreLayoutDynInitL()
    {
    SetTitleL(iTitle);
    CEikEdwin* ed = (CEikEdwin*)Control(EBrTextField);
    ed->SetTextL(&iText);
    // the address bar: what is there is selected, so typing replaces it
    if (iSelectAll && iText.Length()) ed->SelectAllL();
    else ed->SetCursorPosL(iText.Length(), EFalse);
    }

TKeyResponse CTextDialog::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
    {
    // one line (the address, a text field, Find): Enter is OK, as in a desktop browser
    if (!iMulti && aType == EEventKey && (aKeyEvent.iCode == EKeyEnter || aKeyEvent.iCode == EKeyLineFeed))
        {
        TryExitL(EEikBidOk);
        return EKeyWasConsumed;
        }
    return CEikDialog::OfferKeyEventL(aKeyEvent, aType);
    }

TBool CTextDialog::OkToExitL(TInt aButtonId)
    {
    if (aButtonId == EEikBidOk)
        {
        CEikEdwin* ed = (CEikEdwin*)Control(EBrTextField);
        ed->GetText(iText);
        }
    return ETrue;
    }

void CSecretDialog::PreLayoutDynInitL()
    {
    SetTitleL(iTitle);
    CEikSecretEditor* ed = (CEikSecretEditor*)Control(EBrTextField);
    ed->SetMaxLength(Min(iText.MaxLength(), 128));
    ed->SetText(iText.Left(128));
    }

TBool CSecretDialog::OkToExitL(TInt aButtonId)
    {
    if (aButtonId == EEikBidOk)
        {
        CEikSecretEditor* ed = (CEikSecretEditor*)Control(EBrTextField);
        TBuf<128> t;
        ed->GetText(t);
        iText.Copy(t.Left(iText.MaxLength()));
        }
    return ETrue;
    }

TBool CBrAppUi::TextDialogL(const TDesC& aTitle, TDes& aText)
    {
    CTextDialog* d = new (ELeave) CTextDialog(aText, aTitle);
    TInt res = aText.MaxLength() > 2000 || aText.Locate('\n') >= 0 ? R_BR_TEXT_DIALOG_MULTI : R_BR_TEXT_DIALOG;
    return d->ExecuteLD(res) == EEikBidOk;
    }

// ------------------------------------------------------------------ a few helpers

// (7.0s has no RPointerArray::AppendL nor CleanupResetAndDestroyPushL)
template <class T> void PtrAppendL(RPointerArray<T>& aArr, T* aPtr)
    {
    User::LeaveIfError(aArr.Append(aPtr));
    }

static void DestroyTextArray(TAny* aArr)
    {
    ((RPointerArray<HBufC>*)aArr)->ResetAndDestroy();
    }

static void AddItemL(RPointerArray<HBufC>& aItems, const TDesC& aText)
    {
    HBufC* c = aText.AllocLC();
    User::LeaveIfError(aItems.Append(c));
    CleanupStack::Pop(c);
    }

// a descriptor as a NUL-terminated copy for the core
static HBufC8* ZeroTermLC(const TDesC8& aText)
    {
    HBufC8* z = HBufC8::NewLC(aText.Length() + 1);
    z->Des().Copy(aText);
    z->Des().ZeroTerminate();
    return z;
    }

static void AppendHtmlEscaped(TDes8& aOut, const TDesC8& aText)
    {
    for (TInt i = 0; i < aText.Length() && aOut.Length() + 6 < aOut.MaxLength(); i++)
        {
        TUint8 c = aText[i];
        if (c == '<') aOut.Append(_L8("&lt;"));
        else if (c == '>') aOut.Append(_L8("&gt;"));
        else if (c == '&') aOut.Append(_L8("&amp;"));
        else if (c == '"') aOut.Append(_L8("&quot;"));
        else aOut.Append(c);
        }
    }

static void AppendHtmlEscaped(TDes8& aOut, const TDesC& aText)
    {
    HBufC8* u = NULL;
    TRAPD(err, u = UnicodeToUtf8L(aText));
    if (err == KErrNone && u) { AppendHtmlEscaped(aOut, *u); delete u; }
    }

// a 24-bit .bmp of a bitmap (from Instagram 9300's helper)
static HBufC8* BitmapToBmpLC(CFbsBitmap& aBitmap)
    {
    TSize s = aBitmap.SizeInPixels();
    TInt rowBytes = (s.iWidth * 3 + 3) & ~3;
    TInt dataSize = rowBytes * s.iHeight;
    HBufC8* out = HBufC8::NewLC(54 + dataSize);
    TPtr8 p = out->Des();
    TUint8 hdr[54];
    Mem::FillZ(hdr, 54);
    hdr[0] = 'B'; hdr[1] = 'M';
    TUint32 fileSize = 54 + dataSize;
    hdr[2] = (TUint8)fileSize; hdr[3] = (TUint8)(fileSize >> 8); hdr[4] = (TUint8)(fileSize >> 16); hdr[5] = (TUint8)(fileSize >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (TUint8)s.iWidth; hdr[19] = (TUint8)(s.iWidth >> 8);
    hdr[22] = (TUint8)s.iHeight; hdr[23] = (TUint8)(s.iHeight >> 8);
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = (TUint8)dataSize; hdr[35] = (TUint8)(dataSize >> 8); hdr[36] = (TUint8)(dataSize >> 16);
    p.Append(hdr, 54);
    HBufC8* row = HBufC8::NewLC(rowBytes);
    for (TInt y = s.iHeight - 1; y >= 0; y--)
        {
        TPtr8 rp = row->Des();
        rp.Zero();
        aBitmap.GetScanLine(rp, TPoint(0, y), s.iWidth, EColor16M);
        rp.SetLength(s.iWidth * 3);
        while (rp.Length() < rowBytes) rp.Append((TUint8)0);
        p.Append(rp);
        }
    CleanupStack::PopAndDestroy(row);
    return out;
    }

// ------------------------------------------------------------------ start and end

void CBrAppUi::ConstructL()
    {
    BaseConstructL();
    RFs& fs = iCoeEnv->FsSession();
    fs.MkDirAll(KBrDataDir);
    iSettings = CBrSettings::NewL(fs);
    TRAPD(err, iSettings->LoadL());
    (void)err;
    IgLogRotate();
    IgLogEnable(iSettings->iLogOn);
    {
    TBuf<80> t;
    t.Format(_L("Browser 9300 %S starting"), &KBrVersion);
    IgLog(t);
    TInt freeRam = 0;
    HAL::Get(HALData::EMemoryRAMFree, freeRam);
    IgLogF(_L("RAM free: %d KB"), freeRam / 1024);
    }
    bw_cookies_init(&iCookies);
    LoadCookies();
    TRAP(err, LoadBookmarksL());
    iFonts = CBrFonts::NewL(iSettings->iTextSize);
    iImages = CImageLoader::NewL(*this, iCookies);
    iImages->SetEnabled(iSettings->iImages);
    iLoader = CPageLoader::NewL(*this, iCookies);
    iLoader->Http().SetPromptForAccessPoint(iSettings->iPromptAccessPoint);
    iHelperHttp = CHttpClient::NewL();
    iStyles = CStyleFetcher::NewL(*this, iCookies);
    iRefreshTimer = CPeriodic::NewL(CActive::EPriorityStandard);
    iStatsTimer = CPeriodic::NewL(CActive::EPriorityLow);
    {
    // the idle thread's CPU time is the idle time (if this kernel keeps CPU times)
    TFullName name;
    TFindThread ft(_L("*Null*"));
    if (ft.Next(name) == KErrNone && iNullThread.Open(ft) == KErrNone) iNullOpen = ETrue;
    else
        {
        TFindThread ft2(_L("*NULL*"));
        if (ft2.Next(name) == KErrNone && iNullThread.Open(ft2) == KErrNone) iNullOpen = ETrue;
        }
    if (iNullOpen) IgLog(name); else IgLog(_L("stats: no idle thread found"));
    }
    iCurrentUrl = HBufC8::NewL(1);
    iView = CBrView::NewL(ClientRect(), *this, *iImages, *iFonts);
    AddToStackL(iView);
    iHistoryPos = -1;
    iSelectControl = -1;
    iForceCs = -1;
    ApplySettingsL();
    IgLogFlush();
    HomeL();
    }

CBrAppUi::~CBrAppUi()
    {
    if (iSettings) iSettings->Save();
    SaveCookies();
    if (iView) RemoveFromStack(iView);
    delete iView;
    if (iRefreshTimer) iRefreshTimer->Cancel();
    delete iRefreshTimer;
    if (iStatsTimer) iStatsTimer->Cancel();
    delete iStatsTimer;
    if (iNullOpen) iNullThread.Close();
    if (iProbeOn)
        {
        iProbe->iQuit = ETrue;
        iProbe->iGo.Signal();
        iProbeThread.Kill(0);
        iProbeThread.Close();
        iProbe->iGo.Close();
        }
    delete iProbe;
    delete iRefreshUrl;
    delete iExitCallback;
    delete iStyles;
    iCssUrls.ResetAndDestroy();
    iCssTexts.ResetAndDestroy();
    delete iFindText;
    delete iLoader;
    delete iImages;
    delete iHelperHttp;
    delete iFonts;
    delete iSettings;
    delete iCurrentUrl;
    for (TInt i = 0; i < iHistory.Count(); i++) FreeHistoryEntry(iHistory[i]);
    iHistory.Close();
    iBookmarkUrls.ResetAndDestroy();
    iBookmarkTitles.ResetAndDestroy();
    bw_cookies_free(&iCookies);
    IgLog(_L("exit"));
    IgLogClose();
    }

void CBrAppUi::HandleForegroundEventL(TBool aForeground)
    {
    CEikAppUi::HandleForegroundEventL(aForeground);
    if (!aForeground) { IgLogFlush(); SaveCookies(); }
    if (iView) iView->SetForeground(aForeground);
    UpdateStatsTimer(aForeground);
    // the cursor's state is the window group's: switch it on again whenever we are in front
    if (aForeground && iSettings && iSettings->iMouseOn) ApplyCursor();
    }

// ------------------------------------------------------------------ CPU and RAM

void CBrAppUi::UpdateStatsTimer(TBool aForeground)
    {
    if (!iStatsTimer || !iView) return;
    if (aForeground && iSettings->iStatsOn)
        {
        if (!iStatsTimer->IsActive())
            {
            iStatsStarted = EFalse;
            iStatsTimer->Start(0, KStatsTickUs, TCallBack(StatsCallback, this));
            }
        }
    else
        {
        iStatsTimer->Cancel();
        iView->SetStats(KNullDesC);
        }
    }

const TInt KProbeTicks = 6;                       // the probe's window: 6 system ticks (~0.1 s)

TInt CBrAppUi::ProbeThread(TAny* aProbe)
    {
    TCpuProbe* p = (TCpuProbe*)aProbe;
    for (;;)
        {
        p->iGo.Wait();
        if (p->iQuit) return 0;
        // from a tick's edge, count until KProbeTicks more have passed
        TUint start = User::TickCount();
        while (User::TickCount() == start) { }
        start = User::TickCount();
        TInt n = 0;
        while (User::TickCount() - start < (TUint)KProbeTicks) n++;
        p->iCount = n;
        p->iDone++;
        }
    }

void CBrAppUi::StartProbeL()
    {
    iProbe = new (ELeave) TCpuProbe;
    Mem::FillZ(iProbe, sizeof(TCpuProbe));
    User::LeaveIfError(iProbe->iGo.CreateLocal(0));
    TInt err = iProbeThread.Create(_L("br9300cpuprobe"), ProbeThread, 4096, &User::Heap(), iProbe);
    if (err != KErrNone) { iProbe->iGo.Close(); User::Leave(err); }
    iProbeThread.SetPriority(EPriorityAbsoluteVeryLow);
    iProbeThread.Resume();
    iProbeOn = ETrue;
    }

TInt CBrAppUi::StatsCallback(TAny* aSelf)
    {
    ((CBrAppUi*)aSelf)->StatsTick();
    return 1;
    }

static void AppendMb(TDes& aT, TInt aBytes)
    {
    TInt kb = aBytes / 1024;
    aT.AppendFormat(_L("%d.%d MB"), kb / 1024, (kb % 1024) * 10 / 1024);
    }

// CPU: the whole phone (100% less the idle thread's share of the time) and this app's thread,
// over the last second; RAM: free of total, and what the browser holds (its heap's allocations,
// the pictures in the bitmap server).
void CBrAppUi::StatsTick()
    {
    TTime now;
    now.UniversalTime();
    TInt64 wall = now.Int64();
    TTimeIntervalMicroSeconds appT(0), idleT(0);
    TInt appErr = RThread().GetCpuTime(appT);
    TInt idleErr = iNullOpen ? iNullThread.GetCpuTime(idleT) : KErrNotFound;
    TBuf<80> t;
    if (iStatsStarted)
        {
        TInt64 dWall = wall - iStatsWall;
        if (dWall < TInt64(1)) dWall = TInt64(1);
        t.Append(_L("CPU "));
        if (idleErr == KErrNone && idleT.Int64() != iStatsIdle)
            {
            TInt idlePct = ((idleT.Int64() - iStatsIdle) * TInt64(100) / dWall).GetTInt();
            if (idlePct > 100) idlePct = 100;
            if (idlePct < 0) idlePct = 0;
            t.AppendFormat(_L("%d%%"), 100 - idlePct);
            }
        else if (iProbeOn && iProbe->iDone != iProbeSeen && iProbeMax > 0)
            {
            TInt busy = 100 - iProbe->iCount * 100 / iProbeMax;
            if (busy < 0) busy = 0;
            t.AppendFormat(_L("%d%%"), busy);
            }
        else t.Append(_L("..."));
        if (appErr == KErrNone && appT.Int64() != iStatsApp)
            {
            TInt appPct = ((appT.Int64() - iStatsApp) * TInt64(100) / dWall).GetTInt();
            if (appPct > 100) appPct = 100;
            if (appPct < 0) appPct = 0;
            t.AppendFormat(_L(", app %d%%"), appPct);
            }
        }
    else
        {
        t.Append(_L("CPU ..."));
        IgLogF2(_L("stats: cpu time errors app %d, idle %d"), appErr, idleErr);
        }
    // the probe: its last window, then the next one
    if (iProbeOn)
        {
        if (iProbe->iDone != iProbeSeen)
            {
            iProbeSeen = iProbe->iDone;
            if (iProbe->iCount > iProbeMax) iProbeMax = iProbe->iCount;
            }
        iProbe->iGo.Signal();
        }
    else if (!iProbe && iStatsStarted && !(idleErr == KErrNone && idleT.Int64() != iStatsIdle))
        {
        TRAPD(perr, StartProbeL());
        IgLogF(_L("stats: cpu probe thread %d"), perr);
        if (perr != KErrNone && iProbe) { delete iProbe; iProbe = NULL; }
        }
    iStatsStarted = ETrue;
    iStatsWall = wall;
    iStatsApp = appT.Int64();
    iStatsIdle = idleT.Int64();
    // RAM
    TInt freeRam = BrFreeRam(), total = 0;
    HAL::Get(HALData::EMemoryRAM, total);
    t.Append(_L("\nRAM free "));
    AppendMb(t, freeRam);
    t.Append(_L("\nApp "));
    TInt heapAlloc = 0;
    User::AllocSize(heapAlloc);
    AppendMb(t, heapAlloc + iImages->CachedKb() * 1024);
    t.AppendFormat(_L(" of %d"), total / (1024 * 1024));
    iView->SetStats(t);
    }

void CBrAppUi::ApplyCursor()
    {
    TEikVirtualCursor& vc = iEikonEnv->VirtualCursor();
    TRAPD(err, vc.SetCursorStateL(iSettings->iMouseOn ? TEikVirtualCursor::EOn : TEikVirtualCursor::EOff, *iEikonEnv));
    IgLogF2(_L("cursor: asked %d, error %d"), iSettings->iMouseOn ? 1 : 0, err);
    IgLogF2(_L("cursor: state now %d, pointer cursor mode %d"), (TInt)vc.CursorState(*iEikonEnv), (TInt)iCoeEnv->WsSession().PointerCursorMode());
    }

void CBrAppUi::ApplySettingsL()
    {
    ApplyFullScreenL();
    ApplyCursor();
    UpdateStatsTimer(ETrue);
    iView->SetPointerOn(iSettings->iMouseOn);
    }

void CBrAppUi::ApplyFullScreenL()
    {
    CEikButtonGroupContainer* cba = CEikButtonGroupContainer::Current();
    if (iSettings->iFullScreen)
        {
        if (cba) cba->MakeVisible(EFalse);
        iView->SetBar(iSettings->iBar);
        iView->SetRect(ApplicationRect());
        }
    else
        {
        if (cba) cba->MakeVisible(ETrue);
        iView->SetBar(iSettings->iBar);
        iView->SetRect(ClientRect());
        }
    }

void CBrAppUi::Say(const TDesC& aText)
    {
    iView->SetStatus(aText);
    iEikonEnv->InfoMsg(aText);
    IgLog(aText);
    }

void CBrAppUi::UpdateTitle()
    {
    BwDoc* d = iView->Doc();
    if (d && d->titleLen)
        {
        TPtrC t((const TUint16*)d->title, d->titleLen);
        iView->SetTitle(t);
        }
    else
        {
        TBuf<160> t;
        t.Copy(iCurrentUrl->Left(160));
        iView->SetTitle(t);
        }
    }

// ------------------------------------------------------------------ commands

void CBrAppUi::HandleCommandL(TInt aCommand)
    {
    switch (aCommand)
        {
        case EEikCmdExit:
        case EEikCmdCanceled:
            if (aCommand == EEikCmdExit) Exit();
            break;
        case EBrCmdGoTo: GoToDialogL(); break;
        case EBrCmdBack: BackL(); break;
        case EBrCmdForward: ForwardL(); break;
        case EBrCmdReload: ReloadL(); break;
        case EBrCmdStop: StopL(); break;
        case EBrCmdHome: HomeL(); break;
        case EBrCmdStartPage: OpenUrlL(_L8("about:start")); break;
        case EBrCmdBookmarks: OpenUrlL(_L8("about:bookmarks")); break;
        case EBrCmdHistory: OpenUrlL(_L8("about:history")); break;
        case EBrCmdAddBookmark: AddBookmarkL(); break;
        case EBrCmdPageInfo: PageInfoL(); break;
        case EBrCmdFind: FindL(EFalse); break;
        case EBrCmdCopyLink: CopyAddressL(); break;
        case EBrCmdCsAuto: case EBrCmdCsUtf8: case EBrCmdCs1250: case EBrCmdCs88592: case EBrCmdCs1252:
            {
            static const TInt kCs[] = { -1, BW_CS_UTF8, BW_CS_1250, BW_CS_8859_2, BW_CS_1252 };
            iForceCs = kCs[aCommand - EBrCmdCsAuto];
            // the page again from its kept copy, in that encoding
            if (iHistoryPos >= 0 && iHistoryPos < iHistory.Count() && iHistory[iHistoryPos].iBody)
                {
                THistoryEntry& e = iHistory[iHistoryPos];
                HBufC8* ct = e.iContentType ? e.iContentType->AllocLC() : KNullDesC8().AllocLC();
                ShowPageL(*e.iUrl, *ct, *e.iBody, iView->ScrollY(), -1, ETrue);
                CleanupStack::PopAndDestroy(ct);
                }
            }
            break;
        case EBrCmdFindNext: FindL(ETrue); break;
        case EBrCmdStyles:
            iSettings->iStyles = !iSettings->iStyles;
            iSettings->Save();
            Say(iSettings->iStyles ? _L("Stylesheets on (from the next page)") : _L("Stylesheets off (from the next page)"));
            break;
        case EBrCmdSetHome: HomeDialogL(); break;
        case EBrCmdImages:
            iSettings->iImages = !iSettings->iImages;
            iImages->SetEnabled(iSettings->iImages);
            iSettings->Save();
            Say(iSettings->iImages ? _L("Pictures on") : _L("Pictures off"));
            iView->ScrollTo(iView->ScrollY());
            break;
        case EBrCmdFullScreen:
            iSettings->iFullScreen = !iSettings->iFullScreen;
            iSettings->Save();
            ApplyFullScreenL();
            break;
        case EBrCmdFullSize:
            iSettings->iFullSize = !iSettings->iFullSize;
            iSettings->Save();
            iHoverCount = 0;
            ReparseCurrentL();
            Say(iSettings->iFullSize ? _L("Original size: Left / Right scroll sideways") : _L("Fitted to the screen"));
            break;
        case EBrCmdBarTop: case EBrCmdBarNarrow: case EBrCmdBarSide: case EBrCmdBarWide:
            iSettings->iBar = aCommand == EBrCmdBarTop ? 0 : aCommand == EBrCmdBarNarrow ? 80 : aCommand == EBrCmdBarWide ? 130 : 100;
            iSettings->Save();
            iView->SetBar(iSettings->iBar);
            break;
        case EBrCmdStats:
            iSettings->iStatsOn = !iSettings->iStatsOn;
            iSettings->Save();
            UpdateStatsTimer(ETrue);
            break;
        case EBrCmdMouse:
            iSettings->iMouseOn = !iSettings->iMouseOn;
            iSettings->Save();
            ApplySettingsL();
            break;
        case EBrCmdTextSmaller: case EBrCmdTextNormal: case EBrCmdTextBigger:
            {
            TInt step = aCommand == EBrCmdTextSmaller ? -1 : aCommand == EBrCmdTextBigger ? 1 : 0;
            if (step == iSettings->iTextSize) break;
            iSettings->iTextSize = step;
            iSettings->Save();
            CBrFonts* f = CBrFonts::NewL(step);
            iView->SetFonts(*f);
            delete iFonts;
            iFonts = f;
            }
            break;
        case EBrCmdTop: iView->ScrollTo(0); break;
        case EBrCmdBottom: iView->ScrollTo(iView->PageHeight()); break;
        case EBrCmdUpdate: UpdateFromPcL(); break;
        case EBrCmdSendLog: SendLogL(); break;
        case EBrCmdSendPrevLog:
            {
            iHelperOp = EHelperLog;
            // (SendLogL with the previous file)
            RFs& fs = iCoeEnv->FsSession();
            RFile f;
            if (f.Open(fs, LogPrevFileName(), EFileRead | EFileShareAny) != KErrNone) { Say(_L("There is no previous log")); iHelperOp = EHelperNone; break; }
            TInt size = 0;
            f.Size(size);
            if (size > 200 * 1024) size = 200 * 1024;
            HBufC8* d = HBufC8::NewLC(size);
            TPtr8 dp = d->Des();
            f.Read(dp);
            f.Close();
            TBuf8<120> path;
            path.Copy(_L8("/results?name=browser9300_log_prev&ext=txt"));
            iHelperHttp->RequestL(iSettings->iServerHost, _L8("POST"), path, _L8("Content-Type: application/octet-stream\r\n"), *d, *this, EFalse, iSettings->iServerPort);
            CleanupStack::PopAndDestroy(d);
            Say(_L("Sending the previous log..."));
            }
            break;
        case EBrCmdScreenshot: SendScreenshotL(); break;
        case EBrCmdServer: ServerDialogL(); break;
        case EBrCmdLogging:
            iSettings->iLogOn = !iSettings->iLogOn;
            IgLogEnable(iSettings->iLogOn);
            iSettings->Save();
            Say(iSettings->iLogOn ? _L("Logging on") : _L("Logging off"));
            break;
        case EBrCmdPromptAp:
            iSettings->iPromptAccessPoint = !iSettings->iPromptAccessPoint;
            iLoader->Http().SetPromptForAccessPoint(iSettings->iPromptAccessPoint);
            iSettings->Save();
            break;
        case EBrCmdDropConnection:
            iLoader->Stop();
            iLoader->Http().CloseConnection();
            iImages->ClearAll();
            Say(_L("Connection dropped"));
            break;
        case EBrCmdClearCookies:
            bw_cookies_free(&iCookies);
            bw_cookies_init(&iCookies);
            iCoeEnv->FsSession().Delete(KCookiesFile);
            Say(_L("Cookies deleted"));
            break;
        case EBrCmdAbout:
            {
            TBuf<80> t;
            t.Format(_L("Browser 9300 %S"), &KBrVersion);
            iEikonEnv->InfoWinL(t, _L("HTML + CSS on the phone, no JavaScript"));
            }
            break;
        default:
            break;
        }
    }

void CBrAppUi::DynInitMenuPaneL(TInt aResourceId, CEikMenuPane* aMenuPane)
    {
    if (aResourceId == R_BR_MENU_VIEW)
        {
        aMenuPane->SetItemButtonState(EBrCmdImages, iSettings->iImages ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdStyles, iSettings->iStyles ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        {
        static const TInt kCs[] = { -1, BW_CS_UTF8, BW_CS_1250, BW_CS_8859_2, BW_CS_1252 };
        for (TInt k = 0; k < 5; k++)
            aMenuPane->SetItemButtonState(EBrCmdCsAuto + k, iForceCs == kCs[k] ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        }
        aMenuPane->SetItemButtonState(EBrCmdFullScreen, iSettings->iFullScreen ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdFullSize, iSettings->iFullSize ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdMouse, iSettings->iMouseOn ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdStats, iSettings->iStatsOn ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdBarTop, iSettings->iBar == 0 ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdBarNarrow, iSettings->iBar == 80 ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdBarSide, iSettings->iBar == 100 ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdBarWide, iSettings->iBar == 130 ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdTextSmaller, iSettings->iTextSize < 0 ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdTextNormal, iSettings->iTextSize == 0 ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdTextBigger, iSettings->iTextSize > 0 ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        }
    else if (aResourceId == R_BR_MENU_TOOLS)
        {
        aMenuPane->SetItemButtonState(EBrCmdLogging, iSettings->iLogOn ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        aMenuPane->SetItemButtonState(EBrCmdPromptAp, iSettings->iPromptAccessPoint ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
        }
    else if (aResourceId == R_BR_MENU_PAGE)
        {
        aMenuPane->SetItemDimmed(EBrCmdBack, iHistoryPos <= 0 && !iLoader->Busy());
        aMenuPane->SetItemDimmed(EBrCmdForward, iHistoryPos + 1 >= iHistory.Count());
        aMenuPane->SetItemDimmed(EBrCmdStop, !iLoader->Busy());
        }
    }

TKeyResponse CBrAppUi::HandleKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
    {
    if (aType != EEventKey) return EKeyWasNotConsumed;
    if (aKeyEvent.iCode == EKeyEscape || aKeyEvent.iCode == EKeyBackspace)
        {
        if (iLoader->Busy()) StopL();
        else BackL();
        return EKeyWasConsumed;
        }
    return EKeyWasNotConsumed;
    }

// ------------------------------------------------------------------ navigation

// the address bar: the page's address selected, or (typed on the page) the first character
void CBrAppUi::GoToDialogL(TUint aFirstChar)
    {
    HBufC* buf = HBufC::NewLC(1000);
    TPtr t = buf->Des();
    if (aFirstChar) t.Append((TChar)aFirstChar);
    else if (iCurrentUrl->Left(6) != _L8("about:")) t.Copy(iCurrentUrl->Left(1000));
    TBool ok;
    {
    CTextDialog* d = new (ELeave) CTextDialog(t, _L("Address"), aFirstChar == 0);
    ok = d->ExecuteLD(R_BR_TEXT_DIALOG) == EEikBidOk;
    }
    for (TInt i = 0; i < t.Length(); i++) if (t[i] == 0x2029) t[i] = ' ';
    t.Trim();
    if (ok && t.Length())
        {
        HBufC8* u8 = UnicodeToUtf8L(t);
        CleanupStack::PushL(u8);
        HBufC8* z = ZeroTermLC(*u8);
        // an address goes there; anything else (words, a single name) is searched for
        const char* typed = (const char*)z->Ptr();
        char* url = bw_input_is_address(typed) ? bw_url_from_input(typed) : bw_search_url(typed);
        CleanupStack::PopAndDestroy(2);
        if (url)
            {
            TPtrC8 u((const TUint8*)url);
            TRAPD(err, OpenUrlL(u));
            ig_free(url);
            User::LeaveIfError(err);
            }
        }
    CleanupStack::PopAndDestroy(buf);
    }

void CBrAppUi::SaveHistoryPosition()
    {
    if (iHistoryPos >= 0 && iHistoryPos < iHistory.Count())
        {
        iHistory[iHistoryPos].iScrollY = iView->ScrollY();
        iHistory[iHistoryPos].iFocus = iView->Focus();
        }
    }

void CBrAppUi::OpenUrlL(const TDesC8& aUrl, const TDesC8* aPost, TBool aAddHistory)
    {
    iRefreshTimer->Cancel();
    if (aUrl.Left(11).CompareF(_L8("javascript:")) == 0) { Say(_L("JavaScript links do not work here")); return; }
    if (aUrl.Left(7).CompareF(_L8("mailto:")) == 0 || aUrl.Left(4).CompareF(_L8("tel:")) == 0)
        {
        Say(_L("This kind of link cannot be opened"));
        return;
        }
    // a jump inside this page
    if (!aPost && iView->Doc())
        {
        HBufC8* a = ZeroTermLC(*iCurrentUrl);
        HBufC8* b = ZeroTermLC(aUrl);
        TBool same = bw_url_same_page((const char*)a->Ptr(), (const char*)b->Ptr());
        CleanupStack::PopAndDestroy(2);
        if (same)
            {
            TInt h = aUrl.Locate('#');
            iView->ScrollToAnchor(aUrl.Mid(h + 1));
            return;
            }
        }
    SaveHistoryPosition();
    iForceCs = -1;
    iNavigatingHistory = !aAddHistory;
    iRestoreScroll = 0;
    iRestoreFocus = -1;
    if (aUrl.Left(6).CompareF(_L8("about:")) == 0)
        {
        iLoader->Stop();
        ShowInternalL(aUrl);
        return;
        }
    iLoadStartMs = (TInt)TickMs();
    HBufC8* referer = iCurrentUrl->AllocLC();
    if (referer->Left(6) == _L8("about:")) referer->Des().Zero();
    iLoader->LoadL(aUrl, aPost, *referer);
    CleanupStack::PopAndDestroy(referer);
    iView->SetLoading(ETrue);
    TBuf<160> t;
    t.Copy(aUrl.Left(160));
    iView->SetTitle(t);
    iPageStatus.Copy(_L("Loading..."));
    UpdateStatus();
    }

void CBrAppUi::HomeL()
    {
    HBufC8* h = UnicodeToUtf8L(iSettings->iHome);
    CleanupStack::PushL(h);
    if (h->Length()) OpenUrlL(*h);
    else OpenUrlL(_L8("about:start"));
    CleanupStack::PopAndDestroy(h);
    }

void CBrAppUi::StopL()
    {
    iRefreshTimer->Cancel();
    if (!iLoader->Busy()) return;
    iLoader->Stop();
    iView->SetLoading(EFalse);
    iPageStatus.Copy(_L("Stopped"));
    UpdateStatus();
    UpdateTitle();
    }

void CBrAppUi::ReloadL()
    {
    if (iHistoryPos < 0 || iHistoryPos >= iHistory.Count()) return;
    SaveHistoryPosition();
    THistoryEntry& e = iHistory[iHistoryPos];
    delete e.iBody; e.iBody = NULL;
    TInt scroll = e.iScrollY, focus = e.iFocus;
    HBufC8* url = e.iUrl->AllocLC();
    OpenUrlL(*url, NULL, EFalse);
    CleanupStack::PopAndDestroy(url);
    iRestoreScroll = scroll;
    iRestoreFocus = focus;
    }

// Back and Forward show the kept copy of the page when there is one

void CBrAppUi::BackL()
    {
    if (iLoader->Busy()) { StopL(); return; }
    if (iView->ListOpen()) return;
    if (iHistoryPos <= 0) { Say(_L("No earlier page")); return; }
    SaveHistoryPosition();
    iForceCs = -1;
    iHistoryPos--;
    THistoryEntry& e = iHistory[iHistoryPos];
    if (e.iUrl->Left(6) == _L8("about:")) { iNavigatingHistory = ETrue; ShowInternalL(*e.iUrl); iView->ScrollTo(e.iScrollY); return; }
    if (e.iBody)
        {
        HBufC8* ct = e.iContentType ? e.iContentType->AllocLC() : KNullDesC8().AllocLC();
        ShowPageL(*e.iUrl, *ct, *e.iBody, e.iScrollY, e.iFocus);
        CleanupStack::PopAndDestroy(ct);
        return;
        }
    TInt scroll = e.iScrollY, focus = e.iFocus;
    HBufC8* url = e.iUrl->AllocLC();
    OpenUrlL(*url, NULL, EFalse);
    CleanupStack::PopAndDestroy(url);
    iRestoreScroll = scroll;
    iRestoreFocus = focus;
    }

void CBrAppUi::ForwardL()
    {
    if (iLoader->Busy()) return;
    if (iHistoryPos + 1 >= iHistory.Count()) { Say(_L("No later page")); return; }
    SaveHistoryPosition();
    iForceCs = -1;
    iHistoryPos++;
    THistoryEntry& e = iHistory[iHistoryPos];
    if (e.iUrl->Left(6) == _L8("about:")) { iNavigatingHistory = ETrue; ShowInternalL(*e.iUrl); iView->ScrollTo(e.iScrollY); return; }
    if (e.iBody)
        {
        HBufC8* ct = e.iContentType ? e.iContentType->AllocLC() : KNullDesC8().AllocLC();
        ShowPageL(*e.iUrl, *ct, *e.iBody, e.iScrollY, e.iFocus);
        CleanupStack::PopAndDestroy(ct);
        return;
        }
    TInt scroll = e.iScrollY, focus = e.iFocus;
    HBufC8* url = e.iUrl->AllocLC();
    OpenUrlL(*url, NULL, EFalse);
    CleanupStack::PopAndDestroy(url);
    iRestoreScroll = scroll;
    iRestoreFocus = focus;
    }

void CBrAppUi::FreeHistoryEntry(THistoryEntry& aEntry)
    {
    delete aEntry.iUrl; aEntry.iUrl = NULL;
    delete aEntry.iTitle; aEntry.iTitle = NULL;
    delete aEntry.iBody; aEntry.iBody = NULL;
    delete aEntry.iContentType; aEntry.iContentType = NULL;
    }

// a new page: the entries after the current one go, the new one is appended
void CBrAppUi::AddHistoryL(const TDesC8& aUrl)
    {
    if (iNavigatingHistory && iHistoryPos >= 0 && iHistoryPos < iHistory.Count())
        {
        // replace the current entry (reload, back/forward without a copy, meta refresh)
        THistoryEntry& e = iHistory[iHistoryPos];
        HBufC8* u = aUrl.AllocL();
        delete e.iUrl;
        e.iUrl = u;
        return;
        }
    while (iHistory.Count() > iHistoryPos + 1)
        {
        FreeHistoryEntry(iHistory[iHistory.Count() - 1]);
        iHistory.Remove(iHistory.Count() - 1);
        }
    THistoryEntry e;
    Mem::FillZ(&e, sizeof(e));
    e.iUrl = aUrl.AllocL();
    e.iFocus = -1;
    TInt err = iHistory.Append(e);
    if (err != KErrNone) { delete e.iUrl; User::Leave(err); }
    while (iHistory.Count() > KHistoryMax)
        {
        FreeHistoryEntry(iHistory[0]);
        iHistory.Remove(0);
        }
    iHistoryPos = iHistory.Count() - 1;
    }

void CBrAppUi::TrimHistoryCache()
    {
    // newest first, as many as the free RAM allows: what they hold now plus what is free, less
    // the reserve, at most KCacheMaxBytes
    TInt held = 0;
    for (TInt i = 0; i < iHistory.Count(); i++) if (iHistory[i].iBody && i != iHistoryPos) held += iHistory[i].iBody->Length();
    TInt budget = held + BrFreeRam() - KCacheRamReserve;
    if (budget > KCacheMaxBytes) budget = KCacheMaxBytes;
    TInt bytes = 0, dropped = 0;
    for (TInt i = iHistory.Count() - 1; i >= 0; i--)
        {
        THistoryEntry& e = iHistory[i];
        if (!e.iBody || i == iHistoryPos) continue;
        if (e.iBody->Length() > KCacheOneMax || bytes + e.iBody->Length() > budget) { delete e.iBody; e.iBody = NULL; dropped++; continue; }
        bytes += e.iBody->Length();
        }
    if (dropped) { User::Heap().Compress(); IgLogF2(_L("history: %d pages dropped, %d KB kept"), dropped, bytes / 1024); }
    }

// ------------------------------------------------------------------ pages

// The external stylesheets of a parsed page from the cache: parse again with them. The ones
// not cached yet are fetched (unless this is the parse after they came).
BwDoc* CBrAppUi::ApplyStylesL(BwDoc* aDoc, const TDesC8& aBody, const char* aUrl, const char* aCt, TBool aReparse)
    {
    iStylesMissing = 0;
    if (!aReparse) iStylesGot = 0;
    if (!iSettings->iStyles || !aDoc->styleCount) return aDoc;
    const char* css[BW_MAX_STYLESHEETS];
    unsigned int len[BW_MAX_STYLESHEETS];
    TInt cached = 0;
    for (TUint i = 0; i < aDoc->styleCount; i++)
        {
        css[i] = NULL;
        len[i] = 0;
        TPtrC8 u((const TUint8*)aDoc->styleUrls[i]);
        TInt k = FindCss(u);
        if (k >= 0)
            {
            // used: to the end (the newest)
            HBufC8* cu = iCssUrls[k]; HBufC8* ct = iCssTexts[k];
            iCssUrls.Remove(k); iCssTexts.Remove(k);
            iCssUrls.Append(cu); iCssTexts.Append(ct);
            k = iCssUrls.Count() - 1;
            if (iCssTexts[k]->Length()) { css[i] = (const char*)iCssTexts[k]->Ptr(); len[i] = iCssTexts[k]->Length(); cached++; }
            }
        else if (!aReparse)
            {
            iStylesMissing++;
            TRAPD(err, iStyles->AddL(u));
            (void)err;
            }
        }
    if (iStylesMissing)
        {
        TRAPD(err, iStyles->StartL());
        (void)err;
        }
    if (!cached) return aDoc;
    BwDoc* d = bw_doc_parse_hover((const char*)aBody.Ptr(), aBody.Length(), aUrl, aCt, iForceCs, css, len, aDoc->styleCount,
                                  (const int*)iHover, iHoverCount, iSettings->iFullSize ? BW_PARSE_FULLSIZE : 0);
    if (!d) return aDoc;
    bw_doc_free(aDoc);
    return d;
    }

TInt CBrAppUi::FindCss(const TDesC8& aUrl) const
    {
    for (TInt i = 0; i < iCssUrls.Count(); i++) if (*iCssUrls[i] == aUrl) return i;
    return -1;
    }

void CBrAppUi::StyleArrived(const TDesC8& aUrl, const TDesC8* aCss)
    {
    // kept even when it failed (empty): no new attempt this run
    HBufC8* u = aUrl.Alloc();
    HBufC8* t = aCss ? aCss->Alloc() : HBufC8::New(1);
    if (!u || !t || iCssUrls.Append(u) != KErrNone) { delete u; delete t; return; }
    if (iCssTexts.Append(t) != KErrNone) { iCssUrls.Remove(iCssUrls.Count() - 1); delete u; delete t; return; }
    iCssBytes += t->Length();
    if (aCss) iStylesGot++;
    TInt cssBudget = iCssBytes + BrFreeRam() - KCacheRamReserve;
    if (cssBudget > KCssCacheMax) cssBudget = KCssCacheMax;
    if (cssBudget < KCssCacheMin) cssBudget = KCssCacheMin;
    while (iCssBytes > cssBudget && iCssUrls.Count() > 1)
        {
        iCssBytes -= iCssTexts[0]->Length();
        delete iCssUrls[0]; delete iCssTexts[0];
        iCssUrls.Remove(0); iCssTexts.Remove(0);
        }
    UpdateStatus();
    }

void CBrAppUi::StylesDone()
    {
    // the page on the screen again, with them (if it is still the page that asked)
    if (iHistoryPos < 0 || iHistoryPos >= iHistory.Count()) return;
    THistoryEntry& e = iHistory[iHistoryPos];
    if (!iStylesGot || !e.iBody || *e.iUrl != *iCurrentUrl || iLoader->Busy()) return;
    iStylesGot = 0;
    TInt scroll = iView->ScrollY();
    TInt focus = iView->Focus();
    HBufC8* ct = e.iContentType ? e.iContentType->Alloc() : KNullDesC8().Alloc();
    if (!ct) return;
    TRAPD(err, ShowPageL(*e.iUrl, *ct, *e.iBody, scroll, focus, ETrue));
    delete ct;
    if (err == KErrNone) iView->SetStatus(_L("Stylesheets applied"));
    }

void CBrAppUi::ShowPageL(const TDesC8& aUrl, const TDesC8& aContentType, const TDesC8& aBody, TInt aScrollY, TInt aFocus, TBool aReparse)
    {
    if (!aReparse) { iStyles->Stop(); iHoverCount = 0; iView->ResetHover(); }
    TUint32 t0 = TickMs();
    HBufC8* zurl = ZeroTermLC(aUrl);
    HBufC8* zct = ZeroTermLC(aContentType);
    BwDoc* doc = NULL;
    if (aContentType.Left(6).CompareF(_L8("image/")) == 0)
        {
        HBufC8* html = HBufC8::NewLC(aUrl.Length() * 2 + 100);
        TPtr8 h = html->Des();
        h.Append(_L8("<html><body style='text-align:center'><img src=\""));
        AppendHtmlEscaped(h, aUrl);
        h.Append(_L8("\"></body></html>"));
        doc = bw_doc_parse((const char*)html->Ptr(), html->Length(), (const char*)zurl->Ptr(), "text/html; charset=utf-8", -1);
        CleanupStack::PopAndDestroy(html);
        }
    else if (aContentType.Left(10).CompareF(_L8("text/plain")) == 0 || aContentType.Left(8).CompareF(_L8("text/css")) == 0 ||
             aContentType.FindF(_L8("javascript")) >= 0)
        doc = bw_doc_plain((const char*)aBody.Ptr(), aBody.Length(), (const char*)zurl->Ptr(), iForceCs >= 0 ? iForceCs : bw_charset_sniff((const char*)zct->Ptr(), "", 0));
    else
        {
        doc = bw_doc_parse_hover((const char*)aBody.Ptr(), aBody.Length(), (const char*)zurl->Ptr(), (const char*)zct->Ptr(), iForceCs,
                                 NULL, NULL, 0, (const int*)iHover, iHoverCount, iSettings->iFullSize ? BW_PARSE_FULLSIZE : 0);
        if (doc) doc = ApplyStylesL(doc, aBody, (const char*)zurl->Ptr(), (const char*)zct->Ptr(), aReparse);
        }
    CleanupStack::PopAndDestroy(2);
    if (!doc) { Say(_L("Out of memory: the page cannot be shown")); return; }
    iParseMs = (TInt)(TickMs() - t0);
    if (IgLogEnabled())
        {
        _LIT(KOom, " (out of memory)");
        TBuf<160> l;
        l.Format(_L("parsed %d KB in %d ms: %d ops, %d links, %d images, %d css rules"), aBody.Length() / 1024, iParseMs,
                 doc->opCount, doc->linkCount, doc->imageCount, doc->cssRules);
        if (doc->failed) l.Append(KOom);
        IgLog(l);
        }
    if (!aReparse) iImages->ClearAll();
    // the sizes of pictures already decoded (a new layout of the same page)
    for (TUint i = 0; i < doc->imageCount; i++)
        {
        TSize full;
        if (doc->images[i].src && iImages->FullSize(doc->images[i].src, full)) { doc->images[i].natW = full.iWidth; doc->images[i].natH = full.iHeight; }
        }
    HBufC8* cur = aUrl.AllocL();
    delete iCurrentUrl;
    iCurrentUrl = cur;
    iView->SetLoading(EFalse);
    // the same page again: what was typed and ticked stays
    if (aReparse && iView->Doc()) bw_doc_copy_form_state(doc, iView->Doc());
    iView->SetDocumentL(doc, aScrollY, aFocus);
    IgLogF2(_L("layout %d ms, %d boxes"), iView->LayoutMs(), iView->BoxCount());
    UpdateTitle();
    // a #fragment
    if (aScrollY == 0 && !aReparse)
        {
        TInt h = aUrl.Locate('#');
        if (h >= 0) iView->ScrollToAnchor(aUrl.Mid(h + 1));
        }
    // <meta http-equiv=refresh>
    if (!aReparse && doc->refreshUrl && doc->refreshDelay <= 15)
        {
        TPtrC8 r((const TUint8*)doc->refreshUrl);
        if (r != aUrl)
            {
            delete iRefreshUrl;
            iRefreshUrl = r.AllocL();
            TInt ms = doc->refreshDelay * 1000;
            if (ms < 300) ms = 300;
            iRefreshTimer->Cancel();
            iRefreshTimer->Start(ms * 1000, ms * 1000, TCallBack(RefreshCallback, this));
            }
        }
    IgLogFlush();
    }

TInt CBrAppUi::RefreshCallback(TAny* aSelf)
    {
    CBrAppUi* self = (CBrAppUi*)aSelf;
    self->iRefreshTimer->Cancel();
    if (!self->iRefreshUrl) return 0;
    HBufC8* u = self->iRefreshUrl;
    self->iRefreshUrl = NULL;
    TRAPD(err, self->OpenUrlL(*u, NULL, EFalse));
    delete u;
    (void)err;
    return 0;
    }

// the received page into the history (its copy kept for Back) and on the screen; takes aBody
// (sets it to NULL when the history keeps it)
void CBrAppUi::ShowLoadedL(const TDesC8& aUrl, const TDesC8& aContentType, HBufC8*& aBody)
    {
    AddHistoryL(aUrl);
    TInt scroll = iRestoreScroll, focus = iRestoreFocus;
    iRestoreScroll = 0;
    iRestoreFocus = -1;
    iNavigatingHistory = EFalse;
    THistoryEntry& e = iHistory[iHistoryPos];
    delete e.iContentType;
    e.iContentType = aContentType.AllocL();
    // the page on the screen keeps its copy (stylesheets lay it out again; Back shows it)
    delete e.iBody;
    e.iBody = aBody;
    aBody = NULL;
    ShowPageL(aUrl, aContentType, *iHistory[iHistoryPos].iBody, scroll, focus);
    THistoryEntry& e2 = iHistory[iHistoryPos];
    delete e2.iTitle;
    e2.iTitle = NULL;
    BwDoc* d = iView->Doc();
    if (d && d->titleLen) e2.iTitle = TPtrC((const TUint16*)d->title, d->titleLen).Alloc();
    TrimHistoryCache();
    }

void CBrAppUi::PageLoaded(const TDesC8& aUrl, const TDesC8& aContentType, HBufC8* aBody, TInt aStatus)
    {
    iDownloadMs = (TInt)TickMs() - iLoadStartMs;
    iPageBytes = aBody->Length();
    IgLogF2(_L("page: HTTP %d, %d bytes"), aStatus, aBody->Length());
    TRAPD(err, ShowLoadedL(aUrl, aContentType, aBody));
    delete aBody;
    if (err != KErrNone)
        {
        TBuf<60> t;
        t.Format(_L("Cannot show the page (%d)"), err);
        Say(t);
        return;
        }
    TBuf<80> t;
    if (aStatus != 200) t.Format(_L("HTTP %d, "), aStatus);
    t.AppendFormat(_L("Page: %d KB, %d.%d s"), iPageBytes / 1024, iDownloadMs / 1000, (iDownloadMs % 1000) / 100);
    iPageStatus.Copy(t.Left(iPageStatus.MaxLength()));
    UpdateStatus();
    }

void CBrAppUi::PageDownloaded(const TDesC& aFile, const TDesC8& aContentType)
    {
    iView->SetLoading(EFalse);
    UpdateTitle();
    IgLog(_L("download saved"));
    TBuf<200> q;
    q.Copy(aFile.Left(150));
    TBuf<40> ct;
    ct.Copy(aContentType.Left(40));
    if (!iEikonEnv->QueryWinL(_L("Open the downloaded file?"), q)) { Say(_L("Saved in C:\\System\\Temp")); return; }
    RApaLsSession ls;
    TInt err = ls.Connect();
    if (err == KErrNone)
        {
        TThreadId tid;
        err = ls.StartDocument(aFile, tid);
        ls.Close();
        }
    if (err != KErrNone)
        {
        TBuf<80> t;
        t.Format(_L("No app opens it (%d): "), err);
        t.Append(ct);
        Say(t);
        }
    }

void CBrAppUi::PageFailed(const TDesC& aText)
    {
    iView->SetLoading(EFalse);
    iNavigatingHistory = EFalse;
    UpdateTitle();
    Say(aText);
    IgLogFlush();
    }

void CBrAppUi::PageProgress(const TDesC& aText)
    {
    iPageStatus.Copy(aText.Left(iPageStatus.MaxLength()));
    UpdateStatus();
    }

// One line per part (the side bar shows them as lines, the top bar in one row):
//   the page:        "Connecting to the server...", "12 / 40 KB, 30%, 9 KB/s", "34 KB, 1.2 s"
//   stylesheets:     "Styles: 2 to load"
//   pictures:        "Pictures: 5, 3 to load, 1 failed" (the ones near the screen are asked for)
void CBrAppUi::UpdateStatus()
    {
    TBuf<200> t;
    t.Copy(iPageStatus);
    if (iStyles && iStyles->Waiting())
        {
        if (t.Length()) t.Append('\n');
        t.AppendFormat(_L("Styles: %d to load"), iStyles->Waiting());
        }
    if (iImages && iSettings->iImages)
        {
        TInt done = iImages->LoadedCount(), left = iImages->Pending(), bad = iImages->FailedCount();
        if (done || left || bad)
            {
            if (t.Length()) t.Append('\n');
            t.AppendFormat(_L("Pictures: %d"), done);
            if (left) t.AppendFormat(_L(", %d to load"), left);
            if (bad) t.AppendFormat(_L(", %d failed"), bad);
            }
        }
    iView->SetStatus(t);
    }

void CBrAppUi::ImageArrived(const TDesC8& aUrl, TBool aOk, TSize aFullSize)
    {
    iView->ImageArrived(aUrl, aOk, aFullSize);
    }

void CBrAppUi::ImageProgress(const TDesC& /*aText*/)
    {
    UpdateStatus();
    }

void CBrAppUi::LinkFocused(TInt /*aTarget*/)
    {
    }

// ------------------------------------------------------------------ links and form fields

void CBrAppUi::ActivateTargetL(TInt aTarget)
    {
    BwDoc* d = iView->Doc();
    if (!d || aTarget < 0 || (TUint)aTarget >= d->targetCount) return;
    const BwTarget& t = d->targets[aTarget];
    if (t.kind == BW_TGT_LABEL)
        {
        // a label: its checkbox / radio / field
        TInt c = t.index >= 0 && (TUint)t.index < d->labelCount ? d->labels[t.index].control : -1;
        if (c >= 0 && d->controls[c].target >= 0) ActivateTargetL(d->controls[c].target);
        return;
        }
    if (t.kind == BW_TGT_LINK)
        {
        TPtrC8 href((const TUint8*)d->links[t.index].href);
        HBufC8* h = href.AllocLC();
        OpenUrlL(*h);
        CleanupStack::PopAndDestroy(h);
        return;
        }
    BwControl& c = d->controls[t.index];
    if (c.disabled == 1 || (c.disabled == 2 && (c.type == BW_CTL_TEXT || c.type == BW_CTL_PASSWORD || c.type == BW_CTL_TEXTAREA)))
        {
        Say(c.disabled == 1 ? _L("This field is disabled") : _L("This field is read-only"));
        return;
        }
    switch (c.type)
        {
        case BW_CTL_TEXT: case BW_CTL_PASSWORD: case BW_CTL_TEXTAREA:
            EditControlL(t.index);
            break;
        case BW_CTL_CHECKBOX: case BW_CTL_RADIO: case BW_CTL_RESET:
            bw_control_click(d, t.index);
            iView->ScrollTo(iView->ScrollY());
            break;
        case BW_CTL_SUBMIT: case BW_CTL_IMAGE:
            SubmitL(c.form, t.index);
            break;
        case BW_CTL_SELECT:
            SelectOptionL(t.index);
            break;
        case BW_CTL_BUTTON:
            Say(_L("This button needs JavaScript"));
            break;
        case BW_CTL_FILE:
            Say(_L("Sending files is not supported"));
            break;
        default:
            break;
        }
    }

// The pointer rests on an element: if a :hover rule is about it (or was about the last one), the
// page is styled again for it. Skipped on pages too slow to parse and lay out again.
const TInt KHoverMaxMs = 900;

void CBrAppUi::HoverElementL(TInt aElem)
    {
    BwDoc* d = iView->Doc();
    if (!d || !d->hoverCount || iLoader->Busy() || iView->ListOpen()) return;
    if (iParseMs + iView->LayoutMs() > KHoverMaxMs) return;
    if (iHistoryPos < 0 || iHistoryPos >= iHistory.Count()) return;
    THistoryEntry& e = iHistory[iHistoryPos];
    if (!e.iBody || !iCurrentUrl || *e.iUrl != *iCurrentUrl) return;
    int chain[32];
    TUint n = 0;
    if (aElem >= 0 && bw_doc_hover_matters(d, aElem)) n = bw_doc_hover_chain(d, aElem, chain, 32);
    if (n == iHoverCount)
        {
        TUint i = 0;
        while (i < n && chain[i] == iHover[i]) i++;
        if (i == n) return;
        }
    for (TUint i = 0; i < n; i++) iHover[i] = chain[i];
    iHoverCount = n;
    ReparseCurrentL();
    }

void CBrAppUi::ReparseCurrentL()
    {
    if (iHistoryPos < 0 || iHistoryPos >= iHistory.Count()) return;
    THistoryEntry& e = iHistory[iHistoryPos];
    if (!e.iBody || !iCurrentUrl || *e.iUrl != *iCurrentUrl || iLoader->Busy()) return;
    HBufC8* ct = e.iContentType ? e.iContentType->AllocLC() : KNullDesC8().AllocLC();
    ShowPageL(*e.iUrl, *ct, *e.iBody, iView->ScrollY(), iView->Focus(), ETrue);
    CleanupStack::PopAndDestroy(ct);
    }

void CBrAppUi::ZoomL(TInt aDir)
    {
    TInt step = aDir == 0 ? 0 : iSettings->iTextSize + aDir;
    if (step < -1) step = -1;
    if (step > 1) step = 1;
    if (step == iSettings->iTextSize) { Say(step > 0 ? _L("Largest text") : step < 0 ? _L("Smallest text") : _L("Normal text")); return; }
    HandleCommandL(step < 0 ? EBrCmdTextSmaller : step > 0 ? EBrCmdTextBigger : EBrCmdTextNormal);
    Say(step > 0 ? _L("Text: large") : step < 0 ? _L("Text: small") : _L("Text: normal"));
    }

void CBrAppUi::EditControlL(TInt aControl, TUint aFirstChar)
    {
    BwDoc* d = iView->Doc();
    BwControl& c = d->controls[aControl];
    TInt max = c.type == BW_CTL_TEXTAREA ? 4000 : 2000;
    HBufC* buf = HBufC::NewLC(max);
    TPtr t = buf->Des();
    if (c.value) t.Copy(TPtrC((const TUint16*)c.value, Min((TInt)c.valueLen, max)));
    // typed on the page with the field focused: that key goes on the end, as on a computer
    if (aFirstChar && t.Length() < max) t.Append((TChar)aFirstChar);
    TBuf<60> title;
    if (c.labelLen) title.Copy(TPtrC((const TUint16*)c.label, Min((TInt)c.labelLen, 60)));
    else if (c.name) title.Copy(TPtrC8((const TUint8*)c.name).Left(60));
    else title.Copy(_L("Text"));
    // a textarea gets the multi-line dialog
    TBool ok;
    {
    if (c.type == BW_CTL_PASSWORD)
        {
        CSecretDialog* dlg = new (ELeave) CSecretDialog(t, title);
        ok = dlg->ExecuteLD(R_BR_SECRET_DIALOG) == EEikBidOk;
        }
    else
        {
        CTextDialog* dlg = new (ELeave) CTextDialog(t, title, EFalse, c.type == BW_CTL_TEXTAREA);
        ok = dlg->ExecuteLD(c.type == BW_CTL_TEXTAREA ? R_BR_TEXT_DIALOG_MULTI : R_BR_TEXT_DIALOG) == EEikBidOk;
        }
    }
    if (ok)
        {
        if (c.type != BW_CTL_TEXTAREA)
            {
            // one line: the edit field's line breaks are paragraph separators
            for (TInt i = 0; i < t.Length(); i++) if (t[i] == 0x2029 || t[i] == '\n' || t[i] == '\r') t[i] = ' ';
            }
        else
            for (TInt i = 0; i < t.Length(); i++) if (t[i] == 0x2029) t[i] = '\n';
        if (c.maxLength > 0 && t.Length() > c.maxLength) t.SetLength(c.maxLength);
        bw_control_set_value(&c, (const unsigned short*)t.Ptr(), t.Length());
        iView->ScrollTo(iView->ScrollY());
        // a form with just this one text field (a search box) goes off right away, as Enter would
        if (c.type == BW_CTL_TEXT && c.form >= 0)
            {
            TInt fields = 0;
            for (TUint i = 0; i < d->controlCount; i++)
                {
                const BwControl& o = d->controls[i];
                if (o.form == c.form && o.target >= 0 && (o.type == BW_CTL_TEXT || o.type == BW_CTL_PASSWORD || o.type == BW_CTL_TEXTAREA)) fields++;
                }
            if (fields == 1)
                {
                // with its first submit button, as browsers do
                TInt submitter = -1;
                for (TUint i = 0; i < d->controlCount; i++)
                    if (d->controls[i].form == c.form && (d->controls[i].type == BW_CTL_SUBMIT || d->controls[i].type == BW_CTL_IMAGE)) { submitter = (TInt)i; break; }
                CleanupStack::PopAndDestroy(buf);
                SubmitL(c.form, submitter);
                return;
                }
            }
        }
    CleanupStack::PopAndDestroy(buf);
    }

void CBrAppUi::SubmitL(TInt aForm, TInt aSubmitter)
    {
    BwDoc* d = iView->Doc();
    if (!d || aForm < 0) { Say(_L("This button is not in a form")); return; }
    char* url = NULL;
    char* body = NULL;
    unsigned int bodyLen = 0;
    int post = 0;
    if (!bw_form_submit(d, aForm, aSubmitter, &url, &body, &bodyLen, &post)) { Say(_L("The form cannot be sent")); return; }
    TPtrC8 u((const TUint8*)url);
    HBufC8* uu = u.Alloc();
    HBufC8* bb = post ? TPtrC8((const TUint8*)(body ? body : ""), bodyLen).Alloc() : NULL;
    ig_free(url);
    ig_free(body);
    if (!uu || (post && !bb)) { delete uu; delete bb; User::Leave(KErrNoMemory); }
    CleanupStack::PushL(uu);
    CleanupStack::PushL(bb);
    if (uu->Left(6) == _L8("about:")) { CleanupStack::PopAndDestroy(2); return; }
    OpenUrlL(*uu, bb);
    CleanupStack::PopAndDestroy(2);
    }

void CBrAppUi::SelectOptionL(TInt aControl)
    {
    BwDoc* d = iView->Doc();
    BwControl& c = d->controls[aControl];
    if (!c.optionCount) return;
    RPointerArray<HBufC> items;
    CleanupStack::PushL(TCleanupItem(DestroyTextArray, &items));
    for (TUint i = 0; i < c.optionCount; i++)
        {
        HBufC* s = TPtrC((const TUint16*)c.options[i].label, c.options[i].labelLen).AllocLC();
        User::LeaveIfError(items.Append(s));
        CleanupStack::Pop(s);
        }
    TBuf<60> title;
    if (c.name) title.Copy(TPtrC8((const TUint8*)c.name).Left(60)); else title.Copy(_L("Choose"));
    iSelectControl = aControl;
    iView->ShowListL(EListSelect, title, items, c.selected, *this);
    CleanupStack::PopAndDestroy(&items);
    }

void CBrAppUi::ListChosen(TInt aListId, TInt aIndex)
    {
    if (aListId == EListSelect && aIndex >= 0 && iView->Doc() && iSelectControl >= 0 &&
        (TUint)iSelectControl < iView->Doc()->controlCount)
        {
        BwControl& c = iView->Doc()->controls[iSelectControl];
        if ((TUint)aIndex < c.optionCount) c.selected = aIndex;
        iView->Relayout();
        }
    iSelectControl = -1;
    }

// the focused link's address, else the page's, to the clipboard
void CBrAppUi::CopyAddressL()
    {
    HBufC* t = NULL;
    BwDoc* d = iView->Doc();
    TInt f = iView->Focus();
    if (d && f >= 0 && (TUint)f < d->targetCount && d->targets[f].kind == BW_TGT_LINK)
        t = Utf8ToUnicodeL(d->links[d->targets[f].index].href);
    else
        t = Utf8ToUnicodeL(*iCurrentUrl);
    CleanupStack::PushL(t);
    CClipboard* cb = CClipboard::NewForWritingLC(iCoeEnv->FsSession());
    CPlainText* text = CPlainText::NewL();
    CleanupStack::PushL(text);
    text->InsertL(0, *t);
    text->CopyToStoreL(cb->Store(), cb->StreamDictionary(), 0, text->DocumentLength());
    cb->CommitL();
    CleanupStack::PopAndDestroy(2);
    TBuf<100> m(_L("Copied: "));
    m.Append(t->Left(80));
    Say(m);
    CleanupStack::PopAndDestroy(t);
    }

void CBrAppUi::FindL(TBool aNext)
    {
    if (!iView->Doc()) return;
    if (!aNext || !iFindText)
        {
        HBufC* buf = HBufC::NewLC(100);
        TPtr t = buf->Des();
        if (iFindText) t.Copy(iFindText->Left(100));
        if (!TextDialogL(_L("Find in page"), t) || !t.Length()) { CleanupStack::PopAndDestroy(buf); return; }
        for (TInt i = 0; i < t.Length(); i++) if (t[i] == 0x2029) t[i] = ' ';
        delete iFindText;
        iFindText = buf;
        CleanupStack::Pop(buf);
        aNext = EFalse;
        }
    if (!iView->Find(*iFindText, aNext))
        {
        TBuf<80> m(_L("Not found: "));
        m.Append(iFindText->Left(60));
        Say(m);
        }
    else iView->SetStatus(_L("Found (Ctrl+N: next)"));
    }

void CBrAppUi::PageInfoL()
    {
    RPointerArray<HBufC> items;
    CleanupStack::PushL(TCleanupItem(DestroyTextArray, &items));
    BwDoc* d = iView->Doc();
    TBuf<200> t;
    t.Copy(_L("Address: "));
    {
    TBuf<180> u; u.Copy(iCurrentUrl->Left(180)); t.Append(u);
    }
    AddItemL(items, t);
    if (d)
        {
        if (d->titleLen) { t.Copy(_L("Title: ")); t.Append(TPtrC((const TUint16*)d->title, Min((TInt)d->titleLen, 180))); AddItemL(items, t); }
        t.Format(_L("Size %d KB, download %d ms, parse %d ms, layout %d ms"), iPageBytes / 1024, iDownloadMs, iParseMs, iView->LayoutMs());
        AddItemL(items, t);
        static const char* const kCs[] = { "UTF-8", "windows-1252", "windows-1250", "ISO-8859-2", "ISO-8859-1" };
        TPtrC8 cs((const TUint8*)(d->charset >= 0 && d->charset <= 4 ? kCs[d->charset] : "?"));
        TBuf<20> cs16; cs16.Copy(cs);
        t.Format(_L("Charset %S, %d links, %d pictures, %d form fields, %d CSS rules"), &cs16, d->linkCount, d->imageCount, d->controlCount, d->cssRules);
        AddItemL(items, t);
        t.Format(_L("Stylesheets %d (%d applied), cache %d KB"), d->styleCount, d->externalCss, iCssBytes / 1024);
        AddItemL(items, t);
        t.Format(_L("Page %d px high, %d boxes, %d ops, %d KB of text"), iView->PageHeight(), iView->BoxCount(), d->opCount, d->poolLen * 2 / 1024);
        AddItemL(items, t);
        if (d->failed) AddItemL(items, _L("Out of memory while reading: the page is incomplete"));
        }
    TInt freeRam = 0;
    HAL::Get(HALData::EMemoryRAMFree, freeRam);
    t.Format(_L("RAM free %d KB, pictures in RAM %d KB, %d waiting"), freeRam / 1024, iImages->CachedKb(), iImages->Pending());
    AddItemL(items, t);
    t.Format(_L("Cookies %d, history %d pages"), iCookies.count, iHistory.Count());
    AddItemL(items, t);
    iView->ShowListL(EListInfo, _L("Page info"), items, 0, *this);
    CleanupStack::PopAndDestroy(&items);
    }

// ------------------------------------------------------------------ internal pages

_LIT8(KPageHead, "<html><head><meta charset=utf-8><title>");
_LIT8(KPageStyle, "</title><style>body{background:#f4f6fa} h1{color:#1c3a6e} .s{color:#777}</style></head><body>");

HBufC8* CBrAppUi::StartPageLC()
    {
    TInt size = 3000;
    for (TInt i = 0; i < iBookmarkUrls.Count(); i++) size += iBookmarkUrls[i]->Length() * 2 + iBookmarkTitles[i]->Length() * 6 + 40;
    HBufC8* b = HBufC8::NewLC(size);
    TPtr8 p = b->Des();
    p.Append(KPageHead);
    p.Append(_L8("Browser 9300"));
    p.Append(KPageStyle);
    p.Append(_L8("<h1>Browser 9300</h1>"
                 "<p>Ctrl+L or the Go to button opens the address bar: type an address, or words to search for (DuckDuckGo). + / - zoom, Up / Down move between links.</p>"
                 "<h2>Bookmarks</h2><ul>"));
    for (TInt i = 0; i < iBookmarkUrls.Count(); i++)
        {
        p.Append(_L8("<li><a href=\""));
        AppendHtmlEscaped(p, *iBookmarkUrls[i]);
        p.Append(_L8("\">"));
        if (iBookmarkTitles[i]->Length()) AppendHtmlEscaped(p, *iBookmarkTitles[i]);
        else AppendHtmlEscaped(p, *iBookmarkUrls[i]);
        p.Append(_L8("</a>"));
        }
    p.Append(_L8("</ul><p><a href='about:bookmarks'>Edit bookmarks</a> | <a href='about:history'>History</a></p>"
                 "<p class=s>Typing on a page opens the address bar. Ctrl+L address, Ctrl+B bookmarks, Ctrl+D add bookmark, Esc back, Ctrl+R reload, Ctrl+F find, Ctrl+N find next, Ctrl+I page info. "
                 "Up/Down: links and scrolling, Left/Right: previous/next link, Shift+Up/Down or Space: page.</p></body></html>"));
    return b;
    }

HBufC8* CBrAppUi::BookmarksPageLC(TBool aHistory)
    {
    TInt size = 2000;
    if (aHistory)
        for (TInt i = 0; i < iHistory.Count(); i++) size += iHistory[i].iUrl->Length() * 2 + (iHistory[i].iTitle ? iHistory[i].iTitle->Length() * 6 : 0) + 60;
    else
        for (TInt i = 0; i < iBookmarkUrls.Count(); i++) size += iBookmarkUrls[i]->Length() * 3 + iBookmarkTitles[i]->Length() * 6 + 80;
    HBufC8* b = HBufC8::NewLC(size);
    TPtr8 p = b->Des();
    p.Append(KPageHead);
    p.Append(aHistory ? _L8("History") : _L8("Bookmarks"));
    p.Append(KPageStyle);
    if (aHistory)
        {
        p.Append(_L8("<h1>History</h1><ol>"));
        for (TInt i = iHistory.Count() - 1; i >= 0; i--)
            {
            const THistoryEntry& e = iHistory[i];
            if (e.iUrl->Left(6) == _L8("about:")) continue;
            p.Append(_L8("<li><a href=\""));
            AppendHtmlEscaped(p, *e.iUrl);
            p.Append(_L8("\">"));
            if (e.iTitle && e.iTitle->Length()) AppendHtmlEscaped(p, *e.iTitle); else AppendHtmlEscaped(p, *e.iUrl);
            p.Append(_L8("</a>"));
            if (e.iBody) p.Append(_L8(" <span class=s>(kept)</span>"));
            }
        p.Append(_L8("</ol>"));
        }
    else
        {
        p.Append(_L8("<h1>Bookmarks</h1><p>Add the page you are on with Ctrl+D (Bookmarks &gt; Add this page).</p><ul>"));
        for (TInt i = 0; i < iBookmarkUrls.Count(); i++)
            {
            p.Append(_L8("<li><a href=\""));
            AppendHtmlEscaped(p, *iBookmarkUrls[i]);
            p.Append(_L8("\">"));
            if (iBookmarkTitles[i]->Length()) AppendHtmlEscaped(p, *iBookmarkTitles[i]); else AppendHtmlEscaped(p, *iBookmarkUrls[i]);
            p.Append(_L8("</a> <a href='about:delbm?"));
            p.AppendNum(i);
            p.Append(_L8("'>[delete]</a>"));
            if (i > 0) { p.Append(_L8(" <a href='about:upbm?")); p.AppendNum(i); p.Append(_L8("'>[up]</a>")); }
            }
        p.Append(_L8("</ul><p><a href='about:start'>Start page</a></p>"));
        }
    p.Append(_L8("</body></html>"));
    return b;
    }

void CBrAppUi::ShowInternalL(const TDesC8& aUrl)
    {
    HBufC8* url = aUrl.AllocLC();
    HBufC8* body = NULL;
    TPtrC8 u(*url);
    if (u.Left(12).CompareF(_L8("about:delbm?")) == 0 || u.Left(11).CompareF(_L8("about:upbm?")) == 0)
        {
        TBool del = u.Left(12).CompareF(_L8("about:delbm?")) == 0;
        TLex8 l(u.Mid(del ? 12 : 11));
        TInt n = -1;
        if (l.Val(n) == KErrNone && n >= 0 && n < iBookmarkUrls.Count())
            {
            if (del) DeleteBookmark(n);
            else if (n > 0)
                {
                HBufC8* bu = iBookmarkUrls[n]; HBufC* bt = iBookmarkTitles[n];
                iBookmarkUrls.Remove(n); iBookmarkTitles.Remove(n);
                iBookmarkUrls.Insert(bu, n - 1); iBookmarkTitles.Insert(bt, n - 1);
                SaveBookmarks();
                }
            }
        // stay on the bookmarks page (the entry on screen is about:bookmarks)
        iNavigatingHistory = ETrue;
        TInt keep = iView->ScrollY();
        CleanupStack::PopAndDestroy(url);
        ShowInternalL(_L8("about:bookmarks"));
        iView->ScrollTo(keep);
        return;
        }
    if (u.CompareF(_L8("about:bookmarks")) == 0) body = BookmarksPageLC(EFalse);
    else if (u.CompareF(_L8("about:history")) == 0) body = BookmarksPageLC(ETrue);
    else if (u.CompareF(_L8("about:blank")) == 0) { body = HBufC8::NewLC(1); }
    else body = StartPageLC();
    iNavigatingHistory = iNavigatingHistory && iHistoryPos >= 0;
    AddHistoryL(u);
    iNavigatingHistory = EFalse;
    iPageBytes = body->Length();
    iDownloadMs = 0;
    ShowPageL(u, _L8("text/html; charset=utf-8"), *body, 0, -1);
    iView->SetStatus(KNullDesC);
    BwDoc* d = iView->Doc();
    if (d && d->titleLen && iHistoryPos >= 0)
        {
        delete iHistory[iHistoryPos].iTitle;
        iHistory[iHistoryPos].iTitle = TPtrC((const TUint16*)d->title, d->titleLen).Alloc();
        }
    CleanupStack::PopAndDestroy(2);     // body, url
    }

// ------------------------------------------------------------------ bookmarks

void CBrAppUi::LoadBookmarksL()
    {
    RFs& fs = iCoeEnv->FsSession();
    RFile f;
    if (f.Open(fs, KBookmarksFile, EFileRead) != KErrNone)
        {
        // the first start: pages that work without JavaScript
        static const char* const kDefaults[] = {
            "https://en.m.wikipedia.org/", "Wikipedia (mobile)",
            "https://cs.m.wikipedia.org/", "Wikipedie (mobilni)",
            "https://lite.cnn.com/", "CNN Lite",
            "https://text.npr.org/", "NPR Text",
            "https://news.ycombinator.com/", "Hacker News",
            "http://info.cern.ch/", "The first web page (CERN)",
            "http://192.168.137.1:8000/", "The PC server (USB link)"
            };
        for (TUint i = 0; i < sizeof(kDefaults) / sizeof(kDefaults[0]); i += 2)
            {
            HBufC8* u = TPtrC8((const TUint8*)kDefaults[i]).AllocLC();
            HBufC* t = Utf8ToUnicodeL(kDefaults[i + 1]);
            CleanupStack::PushL(t);
            PtrAppendL(iBookmarkUrls, u);
            PtrAppendL(iBookmarkTitles, t);
            CleanupStack::Pop(2);
            }
        return;
        }
    CleanupClosePushL(f);
    TInt size = 0;
    f.Size(size);
    if (size > 64 * 1024) size = 64 * 1024;
    HBufC8* buf = HBufC8::NewLC(size);
    TPtr8 p = buf->Des();
    User::LeaveIfError(f.Read(p));
    TInt i = 0;
    while (i < p.Length())
        {
        TInt e = i;
        while (e < p.Length() && p[e] != '\n') e++;
        TPtrC8 line = p.Mid(i, e - i);
        if (line.Length() && line[line.Length() - 1] == '\r') line.Set(line.Left(line.Length() - 1));
        i = e + 1;
        TInt tab = line.Locate('\t');
        TPtrC8 url = tab >= 0 ? line.Left(tab) : line;
        TPtrC8 title = tab >= 0 ? line.Mid(tab + 1) : TPtrC8();
        if (!url.Length()) continue;
        HBufC8* u = url.AllocLC();
        HBufC* t = Utf8ToUnicodeL(title);
        CleanupStack::PushL(t);
        PtrAppendL(iBookmarkUrls, u);
        PtrAppendL(iBookmarkTitles, t);
        CleanupStack::Pop(2);
        }
    CleanupStack::PopAndDestroy(2);
    }

void CBrAppUi::SaveBookmarks()
    {
    RFs& fs = iCoeEnv->FsSession();
    RFile f;
    if (f.Replace(fs, KBookmarksFile, EFileWrite) != KErrNone) { IgLog(_L("bookmarks: cannot write")); return; }
    for (TInt i = 0; i < iBookmarkUrls.Count(); i++)
        {
        f.Write(*iBookmarkUrls[i]);
        f.Write(_L8("\t"));
        HBufC8* t = NULL;
        TRAPD(err, t = UnicodeToUtf8L(*iBookmarkTitles[i]));
        if (err == KErrNone && t) { f.Write(*t); delete t; }
        f.Write(_L8("\r\n"));
        }
    f.Close();
    }

void CBrAppUi::AddBookmarkL()
    {
    if (!iCurrentUrl->Length() || iCurrentUrl->Left(6) == _L8("about:")) { Say(_L("Open a page first")); return; }
    for (TInt i = 0; i < iBookmarkUrls.Count(); i++)
        if (*iBookmarkUrls[i] == *iCurrentUrl) { Say(_L("Already in the bookmarks")); return; }
    HBufC* title = HBufC::NewLC(200);
    TPtr t = title->Des();
    BwDoc* d = iView->Doc();
    if (d && d->titleLen) t.Copy(TPtrC((const TUint16*)d->title, Min((TInt)d->titleLen, 200)));
    else t.Copy(iCurrentUrl->Left(200));
    if (!TextDialogL(_L("Bookmark name"), t)) { CleanupStack::PopAndDestroy(title); return; }
    HBufC8* u = iCurrentUrl->AllocLC();
    PtrAppendL(iBookmarkUrls, u);
    CleanupStack::Pop(u);
    PtrAppendL(iBookmarkTitles, title);
    CleanupStack::Pop(title);
    SaveBookmarks();
    Say(_L("Bookmark added"));
    }

void CBrAppUi::DeleteBookmark(TInt aIndex)
    {
    delete iBookmarkUrls[aIndex];
    delete iBookmarkTitles[aIndex];
    iBookmarkUrls.Remove(aIndex);
    iBookmarkTitles.Remove(aIndex);
    SaveBookmarks();
    }

void CBrAppUi::HomeDialogL()
    {
    TBuf<256> t(iSettings->iHome);
    if (iCurrentUrl->Length() && iCurrentUrl->Left(6) != _L8("about:")) t.Copy(iCurrentUrl->Left(256));
    if (!TextDialogL(_L("Home page (about:start = the start page)"), t)) return;
    iSettings->iHome.Copy(t);
    iSettings->Save();
    Say(_L("Home page set"));
    }

// ------------------------------------------------------------------ cookies

void CBrAppUi::LoadCookies()
    {
    RFile f;
    if (f.Open(iCoeEnv->FsSession(), KCookiesFile, EFileRead) != KErrNone) return;
    TInt size = 0;
    f.Size(size);
    if (size > 0 && size < 256 * 1024)
        {
        HBufC8* b = HBufC8::New(size);
        if (b)
            {
            TPtr8 p = b->Des();
            if (f.Read(p) == KErrNone) bw_cookies_load(&iCookies, (const char*)p.Ptr(), p.Length(), NowUnixSeconds());
            delete b;
            }
        }
    f.Close();
    IgLogF(_L("cookies loaded: %d"), iCookies.count);
    }

void CBrAppUi::SaveCookies()
    {
    char* t = bw_cookies_save(&iCookies, NowUnixSeconds());
    if (!t) return;
    RFile f;
    if (f.Replace(iCoeEnv->FsSession(), KCookiesFile, EFileWrite) == KErrNone)
        {
        f.Write(TPtrC8((const TUint8*)t));
        f.Close();
        }
    ig_free(t);
    }

// ------------------------------------------------------------------ the PC: update, log, screenshot

void CBrAppUi::ServerDialogL()
    {
    TBuf<80> t;
    iSettings->ServerText(t);
    if (!TextDialogL(_L("PC server (host:port)"), t)) return;
    iSettings->SetServer(t);
    iSettings->Save();
    iSettings->ServerText(t);
    Say(t);
    }

void CBrAppUi::UpdateFromPcL()
    {
    if (iHelperHttp->Busy()) { Say(_L("Busy with the PC...")); return; }
    iHelperOp = EHelperUpdate;
    iHelperHttp->SetSinkFile(KUpdateFile);
    TBuf8<200> h;
    h.Append(_L8("User-Agent: "));
    h.Append(KBrUserAgent);
    h.Append(_L8("\r\nAccept: */*\r\n"));
    iHelperHttp->RequestL(iSettings->iServerHost, _L8("GET"), KUpdatePath, h, KNullDesC8, *this, EFalse, iSettings->iServerPort);
    Say(_L("Downloading the update from the PC..."));
    }

void CBrAppUi::SendLogL()
    {
    if (iHelperHttp->Busy()) { Say(_L("Busy with the PC...")); return; }
    IgLog(_L("sending the log"));
    IgLogFlush();
    RFile f;
    if (f.Open(iCoeEnv->FsSession(), LogFileName(), EFileRead | EFileShareAny) != KErrNone) { Say(_L("No log yet (Tools > Logging)")); return; }
    TInt size = 0;
    f.Size(size);
    if (size > 200 * 1024) size = 200 * 1024;
    HBufC8* d = HBufC8::NewLC(size);
    TPtr8 dp = d->Des();
    f.Read(dp);
    f.Close();
    iHelperOp = EHelperLog;
    iHelperHttp->RequestL(iSettings->iServerHost, _L8("POST"), _L8("/results?name=browser9300_log&ext=txt"),
                          _L8("Content-Type: application/octet-stream\r\n"), *d, *this, EFalse, iSettings->iServerPort);
    CleanupStack::PopAndDestroy(d);
    Say(_L("Sending the log..."));
    }

void CBrAppUi::SendScreenshotL()
    {
    if (iHelperHttp->Busy()) { Say(_L("Busy with the PC...")); return; }
    CFbsBitmap* b = iView->Buffer();
    if (!b) return;
    HBufC8* bmp = BitmapToBmpLC(*b);
    iHelperOp = EHelperShot;
    iHelperHttp->RequestL(iSettings->iServerHost, _L8("POST"), _L8("/results?name=browser9300_screen&ext=bmp"),
                          _L8("Content-Type: application/octet-stream\r\n"), *bmp, *this, EFalse, iSettings->iServerPort);
    CleanupStack::PopAndDestroy(bmp);
    Say(_L("Sending a screenshot..."));
    }

void CBrAppUi::HttpProgress(const TDesC& aText)
    {
    if (iHelperOp == EHelperUpdate) iView->SetStatus(aText);
    }

void CBrAppUi::HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& /*aHeaders*/, const TDesC8& /*aBody*/)
    {
    TInt op = iHelperOp;
    iHelperOp = EHelperNone;
    if (aError != KErrNone || aHttpStatus != 200)
        {
        TBuf<100> t;
        if (aError != KErrNone) t.Format(_L("The PC did not answer (%d): is ota_server.js running?"), aError);
        else t.Format(_L("The PC answered HTTP %d"), aHttpStatus);
        Say(t);
        return;
        }
    if (op == EHelperUpdate)
        {
        IgLog(_L("update downloaded: starting the installer"));
        if (iSettings) iSettings->Save();
        SaveCookies();
        IgLogFlush();
        RApaLsSession ls;
        TInt err = ls.Connect();
        if (err == KErrNone)
            {
            TThreadId tid;
            err = ls.StartDocument(KUpdateFile, tid);
            ls.Close();
            }
        if (err != KErrNone)
            {
            TBuf<100> t;
            t.Format(_L("Saved as %S; the installer did not start (%d)"), &KUpdateFile, err);
            Say(t);
            return;
            }
        // not from here: this runs inside the HTTP client's RunL, whose RunError would swallow
        // the exit leave; a callback of its own leaves to the scheduler, which exits
        if (!iExitCallback) iExitCallback = new CAsyncCallBack(TCallBack(ExitCallback, this), CActive::EPriorityHigh);
        if (iExitCallback) iExitCallback->CallBack();
        else Exit();
        return;
        }
    Say(op == EHelperShot ? _L("Screenshot sent to the PC") : _L("Log sent to the PC"));
    }

TInt CBrAppUi::ExitCallback(TAny* aSelf)
    {
    ((CBrAppUi*)aSelf)->Exit();
    return 0;
    }

// ------------------------------------------------------------------ entry points

EXPORT_C CApaApplication* NewApplication()
    {
    return new CBrApplication;
    }

GLDEF_C TInt E32Dll(TDllReason)
    {
    return KErrNone;
    }
