// Browser 9300 - a web browser for the Nokia 9300 / 9500 (Series 80 2.0, Symbian 7.0s, EKA1).
//
// Everything runs on the phone:
//   core/        plain C: URLs, charsets, HTML + CSS parsing, layout, forms, cookies (host-tested)
//   CHttpClient  one HTTP(S) request at a time: RConnection (the open link), TCP, TLS through
//                CSecureSocket (the patched ssladaptor.dll), keep-alive, chunked, gzip
//   CPageLoader  a page: cookies, redirects, content types, downloads
//   CImageLoader pictures of the page: queue by distance from the screen, ICL decode, RAM cache
//   CBrView      draws the laid-out page, scrolling, focus (links and form fields), the pointer
//   CBrAppUi     commands, history with cached pages, bookmarks, internal about: pages,
//                update from the PC and log upload

#ifndef BROWSER_H
#define BROWSER_H

#include <e32base.h>
#include <es_sock.h>
#include <in_sock.h>
#include <securesocket.h>
#include <eikapp.h>
#include <eikdoc.h>
#include <eikappui.h>
#include <eikdialg.h>
#include <eikfctry.h>
#include <coecntrl.h>
#include <bitdev.h>
#include <bitstd.h>
#include <f32file.h>
#include "bwcore.h"

const TUid KUidBrowser9300 = { 0x0F5A9310 };
_LIT(KBrVersion, "0.38");
_LIT8(KBrUserAgent, "Mozilla/5.0 (Series80/2.0; Nokia9300; Profile/MIDP-2.0 Configuration/CLDC-1.1) Browser9300/0.38");
// View > Desktop pages: a desktop browser's, so sites send the layout for a wide screen (the mobile
// ones are made for portrait phones); the version stays at the end
_LIT8(KBrDesktopAgent, "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0 Browser9300/0.38");
const TDesC8& BrUserAgent();
HBufC8* BrJsTestPageLC();
// a page's inline scripts (jsplat.cpp): the page with each script replaced by what it wrote, NULL
// if they wrote nothing; aSummary for the status / log
HBufC8* BrRunPageScriptsL(const TDesC8& aHtml, const TDesC8& aUrl, const TDesC8& aUserAgent, TDes& aSummary);             // about:js (jsplat.cpp): the JavaScript engine's self-test
HBufC8* BrSpeedTestPageLC();           // about:speed (speed.cpp): the phone's double / int / copy timings            // the one the settings ask for
_LIT(KBrDataDir, "C:\\System\\Data\\browser9300\\");

// ------------------------------------------------------------------ util.cpp

HBufC* Utf8ToUnicodeL(const TDesC8& aText);
HBufC* Utf8ToUnicodeL(const char* aText);
char* UnicodeToUtf8(const TDesC& aText);          // ig_malloc'ed, NULL on failure
HBufC8* UnicodeToUtf8L(const TDesC& aText);
TUint NowUnixSeconds();
// the virtual cursor off while a text dialog needs the arrow keys (ETrue if it was on)
TBool SuspendCursor(CEikonEnv& aEnv);
void ResumeCursor(CEikonEnv& aEnv);
TUint32 TickMs();                                  // milliseconds (for timings)
TInt BrFreeRam();                                  // the system's free RAM in bytes (HAL)

void IgLogEnable(TBool aOn);
TBool IgLogEnabled();
void IgLog(const TDesC& aText);
void IgLog8(const TDesC8& aText);
void IgLogF(const TDesC& aFormat, TInt aValue);
void IgLogF2(const TDesC& aFormat, TInt aValue1, TInt aValue2);
void IgLogRotate();
void IgLogFlush();
void IgTrace(const TDesC& aStep);
void IgTraceF(const TDesC& aFormat, TInt aValue);                 // with logging on: the step, saved at once (a crash after it shows it)
void IgTraceHeap(const TDesC& aStep);                             // the same after checking the heap (a corrupt heap panics in the check)
void IgLogClose();
const TDesC& LogFileName();
const TDesC& LogPrevFileName();

// ------------------------------------------------------------------ net.cpp

struct z_stream_s;

class MHttpObserver
    {
public:
    virtual void HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody) = 0;
    virtual void HttpProgress(const TDesC& aText) = 0;
    // a step that blocks for a while (unzipping): shown on the screen before it starts
    virtual void HttpStep(const TDesC& aText) { HttpProgress(aText); }
    // the reply's headers are in: EFalse pauses the transfer (Resume / Abort it later)
    virtual TBool HttpHeadersArrived(const TDesC8& /*aHeaders*/, TInt /*aStatus*/) { return ETrue; }
    };

class CHttpClient : public CActive
    {
public:
    enum TState { EIdle, EStartingConnection, EResolving, EConnecting, EHandshaking, ESending, EReceiving };

    static CHttpClient* NewL();
    ~CHttpClient();
    void RequestL(const TDesC8& aHost, const TDesC8& aMethod, const TDesC8& aPath,
                  const TDesC8& aHeaders, const TDesC8& aBody, MHttpObserver& aObserver,
                  TBool aSecure = ETrue, TInt aPort = 443);
    TBool Busy() const { return iState != EIdle; }
    void SetPromptForAccessPoint(TBool aPrompt) { iPrompt = aPrompt; }
    void SetSinkFile(const TDesC& aFileName) { iSinkName.Copy(aFileName); }
    // a transfer paused at its headers goes on: into aFile (written as it arrives; empty: RAM)
    void ResumeL(const TDesC& aFile);
    TBool Paused() const { return iPaused; }
    TBool SinkUsed() const { return iSinkUsed; }       // the last reply went to the sink file
    TInt SinkBytes() const { return iSinkBytes; }
    TInt ContentLength() const { return iInfo.contentLength; }
    void CloseConnection();
    void Abort();
    TInt BytesReceived() const { return iRespLen + iSinkBytes; }
    const TDesC& LastError() const { return iLastError; }
    void SetQuiet(TBool aQuiet) { iQuiet = aQuiet; }

private:
    CHttpClient();
    void ConstructL();
    void RunL();
    void DoCancel();
    TInt RunError(TInt aError);
    void EnsureSessionL();
    void StartConnectionL();
    void ResolveL();
    void ConnectL();
    void HandshakeL();
    void SendL();
    void ReceiveNext();
    void FinishL(TInt aError);
    void CloseSocket();
    TBool AppendResponse(const TDesC8& aData);
    HBufC8* DecodeBodyL(const TDesC8& aBody, TBool aChunked, TBool aGzip);
    void Progress(const TDesC& aText);
    TBool SinkWrite(const TDesC8& aData);
    void TryOpenSink();
    // a body piece on its way to the sink file: chunked framing off, gzip inflated, then written
    TBool SinkFeed(const TDesC8& aData);
    TBool GunzipFeed(const TDesC8& aData);
    TBool SinkDone() const;               // all of the body is in the file
    BwDechunk iDechunk;
    TBool iSinkChunked, iSinkGzip;
    TInt iGzState;                        // 0 the header, 1 inflating, 2 the end
    HBufC8* iGzHead;                      // the gzip header while it arrives
    HBufC8* iStage;                       // the de-chunked bytes / the inflated ones
    struct z_stream_s* iZs;
    void ReceivedSoFarL();                 // after the headers: done, or receive more
    TBool iPaused, iSinkUsed;
    TBool SinkFlush();
    void CloseSink();

    TState iState;
    MHttpObserver* iObserver;
    RSocketServ iSs;
    TBool iSsOpen;
    RConnection iConn;
    TBool iConnOpen;
    TBool iConnStarted;
    TBool iPrompt;
    TBool iQuiet;
    RHostResolver iResolver;
    TBool iResolverOpen;
    TNameEntry iNameEntry;
    RSocket iSocket;
    TBool iSocketOpen;
    CSecureSocket* iTls;
    TSockXfrLength iXfrLen;
    TBuf8<128> iHost;
    TBool iSecure;
    TInt iPort;
    TBool iKeepAlive;
    TBuf8<128> iKeepHost;
    TBool iKeepSecure;
    TInt iKeepPort;
    TBool iReused;
    TBool iFramed;
    TBuf<128> iHost16;
    TInetAddr iAddr;
    TBuf8<128> iCachedHost;
    TInetAddr iCachedAddr;
    HBufC8* iRequest;
    HBufC8* iResponse;
    TInt iRespLen;
    TBuf8<16384> iChunk;
    TTime iRequestStart;
    TBool iHeadersDone;
    IgHttpInfo iInfo;
    TBuf<64> iLastError;
    TFileName iSinkName;
    RFile iSink;
    TBool iSinkOpen;
    HBufC8* iSinkBuf;
    TInt iSinkBytes;
    RFs iSinkFs;
    TBool iSinkFsOpen;
    TInt iLastProgressKb;
    };

// ------------------------------------------------------------------ loader.cpp

class MPageObserver
    {
public:
    // a page arrived: aBody ownership passes (may be empty); aContentType the header value
    virtual void PageLoaded(const TDesC8& aUrl, const TDesC8& aContentType, HBufC8* aBody, TInt aStatus) = 0;
    // a file that is not a page was saved (aFile): offer to open it
    virtual void PageDownloaded(const TDesC& aFile, const TDesC8& aContentType) = 0;
    // a file (not a page) is about to come: the transfer waits for AcceptDownloadL / Stop
    virtual void DownloadOffered(const TDesC& aName, const TDesC8& aContentType, TInt aLength) = 0;
    virtual void PageFailed(const TDesC& aText) = 0;
    virtual void PageProgress(const TDesC& aText) = 0;
    virtual void PageStep(const TDesC& aText) { PageProgress(aText); }   // shown at once
    };

class CPageLoader : public CBase, public MHttpObserver
    {
public:
    static CPageLoader* NewL(MPageObserver& aObserver, BwCookieJar& aCookies);
    ~CPageLoader();
    // aPostBody: NULL for GET
    void LoadL(const TDesC8& aUrl, const TDesC8* aPostBody, const TDesC8& aReferer);
    void Stop();
    TBool Busy() const { return iBusy; }
    const TDesC8& Url() const { return *iUrl; }
    CHttpClient& Http() { return *iHttp; }
    TInt StartedMs() const { return iStartMs; }
    // MHttpObserver
    void HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody);
    void HttpProgress(const TDesC& aText);
    void HttpStep(const TDesC& aText);
    TBool HttpHeadersArrived(const TDesC8& aHeaders, TInt aStatus);
    // the offered file is wanted: it goes to aPath as it arrives
    void AcceptDownloadL(const TDesC& aPath);
private:
    CPageLoader(MPageObserver& aObserver, BwCookieJar& aCookies);
    static TBool IsPageType(const TDesC8& aContentType, const TDesC8& aDisposition);
    void ConstructL();
    void SendL();
    void StoreCookies(const TDesC8& aHeaders);
    static TInt WatchdogTick(TAny* aSelf);
    MPageObserver& iObserver;
    BwCookieJar& iCookies;
    CHttpClient* iHttp;
    CPeriodic* iWatchdog;
    HBufC8* iUrl;
    HBufC8* iPost;
    HBufC8* iReferer;
    TInt iRedirects;
    TBool iBusy;
    TInt iStartMs;
    TInt iLastBytes;
    TInt iIdleTicks;
    TBuf8<128> iHost;
    TFileName iDownloadPath;               // where the accepted file goes (empty: no offer accepted)
    TBool iOffered;                        // paused at a file's headers, waiting for the user
    };

class MStyleObserver
    {
public:
    virtual void StyleArrived(const TDesC8& aUrl, const TDesC8* aCss) = 0;   // aCss NULL: failed
    virtual void StylesDone() = 0;                                          // the queue is empty
    };

class CStyleFetcher : public CBase, public MHttpObserver
    {
public:
    static CStyleFetcher* NewL(MStyleObserver& aObserver, BwCookieJar& aCookies);
    ~CStyleFetcher();
    void AddL(const TDesC8& aUrl);
    void StartL();
    void Stop();
    TBool Busy() const { return iCurrent != NULL || iQueue.Count() > 0; }
    TInt Waiting() const { return iQueue.Count() + (iCurrent ? 1 : 0); }
    void HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody);
    void HttpProgress(const TDesC& aText);
private:
    CStyleFetcher(MStyleObserver& aObserver, BwCookieJar& aCookies);
    void SendL();
    void Done(const TDesC8* aCss);
    static TInt WatchdogTick(TAny* aSelf);
    MStyleObserver& iObserver;
    BwCookieJar& iCookies;
    CHttpClient* iHttp;
    CPeriodic* iWatchdog;
    RPointerArray<HBufC8> iQueue;
    HBufC8* iCurrent;               // the URL as the page names it (the cache key)
    HBufC8* iCurrentFinal;          // after redirects
    TInt iRedirects;
    TInt iLastBytes, iIdleTicks;
    TBuf8<128> iHost;
    };

// ------------------------------------------------------------------ images.cpp

class CFbsBitmap;
class CImageDecoder;

class MImageObserver
    {
public:
    virtual void ImageArrived(const TDesC8& aUrl, TBool aOk, TSize aFullSize) = 0;
    virtual void ImageProgress(const TDesC& aText) = 0;
    };

// One decoded bitmap per picture URL, at the size that fits the page width (the layout scales
// it further when the page asks for less). Downloads one at a time, nearest the screen first.
class CImageLoader : public CActive, public MHttpObserver
    {
public:
    static CImageLoader* NewL(MImageObserver& aObserver, BwCookieJar& aCookies);
    ~CImageLoader();
    // the bitmap, or NULL (then it is queued with that priority: smaller = sooner)
    CFbsBitmap* Get(const char* aUrl, TInt aPriority);
    TBool Failed(const char* aUrl);
    void BeginPass();             // a draw / scan pass starts: requests not renewed are dropped at EndPass
    void EndPass();
    void ClearAll();              // a new page: the queue and the cache go
    void SetMaxSize(const TSize& aSize) { iMax = aSize; }
    void SetEnabled(TBool aOn) { iEnabled = aOn; }
    TInt Pending() const;         // the page's pictures still to load (not the icon: priority < 0)
    TInt LoadedCount() const { return iLoadedCount; }   // this page's pictures so far
    TInt FailedCount() const { return iFailedCount; }
    TBool Decoding(TSize& aSize) const { aSize = iFullNow; return iDecoder != NULL || iJob != NULL; }   // a picture being decoded, its full size
    TInt CachedKb() const { return iBytes / 1024; }
    TBool FullSize(const char* aUrl, TSize& aSize);     // a cached picture's original size
    // MHttpObserver
    void HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody);
    void HttpProgress(const TDesC& aText);
private:
    struct CEntry : public CBase
        {
        ~CEntry();
        HBufC8* iUrl;
        CFbsBitmap* iBitmap;      // NULL: failed
        TUint iLastUse;
        TSize iFull;
        };
    struct CRequest : public CBase
        {
        ~CRequest();
        HBufC8* iUrl;
        TInt iPriority;
        TInt iGen;
        TInt iRedirects;
        };
    CImageLoader(MImageObserver& aObserver, BwCookieJar& aCookies);
    void ConstructL();
    void RunL();
    void DoCancel();
    TInt RunError(TInt aError);
    static TInt KickCallback(TAny* aSelf);
    void Kick();
    void StartNextL();
    void FetchL(const TDesC8& aUrl);
    void DecodeL();
    void DecodeIcoL(BwIcoImage& aIco);
    TBool OwnDecodeL();                 // PNG / baseline JPEG: our decoders (Maps 9300's) in a thread
    void DecodeIclL();                  // the rest (GIF, progressive JPEG...): the phone's ICL
    TBool OwnDecodeDone();              // RunL: the thread's result into iDecoded (EFalse: try ICL)
    void FreeJob();
    static TInt DecodeThread(TAny* aJob);
    void Finish(CFbsBitmap* aBitmap);
    CEntry* Find(const TDesC8& aUrl);
    void TrimFor(TInt aBytes);
    CFbsBitmap* ScaleL(CFbsBitmap& aSrc, const TSize& aTarget);
    MImageObserver& iObserver;
    BwCookieJar& iCookies;
    RFs iFs;
    CHttpClient* iHttp;
    CAsyncCallBack* iKicker;
    RPointerArray<CEntry> iEntries;
    RPointerArray<CRequest> iQueue;
    CRequest* iCurrent;
    HBufC8* iData;
    CImageDecoder* iDecoder;
    struct TDecodeJob* iJob;            // our decoder's work, while its thread runs
    RThread iDecThread;
    TInt iDecSerial;                    // (a new thread name each time)
    CFbsBitmap* iDecoded;
    TSize iFullNow;
    TUint iUse;
    TInt iBytes;
    TInt iGen;
    TSize iMax;
    TBool iEnabled;
    TBuf8<128> iHostBuf;
    TInt iLoadedCount, iFailedCount;
    };

// ------------------------------------------------------------------ fonts.cpp

class CBrFonts : public CBase
    {
public:
    static CBrFonts* NewL(TInt aSizeStep);     // -1 smaller, 0 normal, 1 bigger
    ~CBrFonts();
    const CFont* Font(TInt aId);                 // bwcore font id
    const BwMetrics& Metrics() const { return iMetrics; }
private:
    CBrFonts(TInt aSizeStep);
    static int TextWidth(void* ctx, int font, const unsigned short* s, int n);
    static int Height(void* ctx, int font);
    static int Ascent(void* ctx, int font);
    CFont* iFonts[BW_FONT_COUNT];
    TInt iStep;
    BwMetrics iMetrics;
    };

// ------------------------------------------------------------------ settings.cpp

class CBrSettings : public CBase
    {
public:
    static CBrSettings* NewL(RFs& aFs);
    TBool iImages;
    TBool iStyles;
    TBool iLogOn;
    TBool iMouseOn;
    TBool iFullScreen;              // on by default ("full="; "fullscreen", off before 0.9, is ignored)
    TInt iTextSize;                 // -1, 0, 1
    TBool iFullSize;                // View > Original size: laid out 1024 px wide, scrolls sideways
    TBool iDesktop;                 // View > Desktop pages: the desktop User-Agent
    TBool iJs;                      // View > JavaScript: run a page's inline scripts (on by default)
    TBool iStatsOn;                 // View > CPU and RAM (on by default)
    TInt iBar;                      // View > Bar: 0 top, else the side bar's width (80 / 100 / 130)
    TBool iPromptAccessPoint;
    TBuf<256> iHome;
    TBuf8<64> iServerHost;
    TInt iServerPort;
    void LoadL();
    void Save();
    void SetServer(const TDesC& aText);
    void ServerText(TDes& aText) const;
private:
    CBrSettings(RFs& aFs);
    RFs& iFs;
    };

// ------------------------------------------------------------------ view.cpp

class CBrAppUi;

// a list drawn over the page: <select> options, page info
class MListObserver
    {
public:
    virtual void ListChosen(TInt aListId, TInt aIndex) = 0;      // aIndex -1: cancelled
    };

class CBrView : public CCoeControl
    {
public:
    static CBrView* NewL(const TRect& aRect, CBrAppUi& aAppUi, CImageLoader& aImages, CBrFonts& aFonts);
    ~CBrView();

    // the page (ownership of aDoc passes); aScrollY / aFocus restore a position (history)
    void SetDocumentL(BwDoc* aDoc, TInt aScrollY, TInt aFocus);
    BwDoc* Doc() const { return iDoc; }
    void ClearDocument();
    void SetFonts(CBrFonts& aFonts);
    void Relayout();                       // widths, fonts or image sizes changed
    void ImageArrived(const TDesC8& aUrl, TBool aOk, TSize aFullSize);
    void SetStatus(const TDesC& aText);    // the header's right part
    void StatusNow(const TDesC& aText);    // the same, the bar drawn and on the screen at once (a long step follows)
    void SetStats(const TDesC& aText);     // CPU and RAM (lines separated by '\n'); redraws only the bar
    void SetTitle(const TDesC& aText);     // the header's left part (title or address)
    void SetIconL(const TDesC8& aUrl);     // the page's icon (favicon); empty: none
    void SetHost(const TDesC8& aHost) { iHost.Copy(aHost.Left(iHost.MaxLength())); }
    void SetLoading(TBool aLoading);
    TInt ScrollY() const { return iScrollY; }
    void ScrollXBy(TInt aDx);             // sideways (the original size is wider than the screen)
    TInt Focus() const { return iFocus; }
    void ScrollToAnchor(const TDesC8& aName);
    void ScrollTo(TInt aY);
    TBool Find(const TDesC& aText, TBool aNext);
    void ShowListL(TInt aId, const TDesC& aTitle, const RPointerArray<HBufC>& aItems, TInt aSelected, MListObserver& aObserver);
    TBool ListOpen() const { return iListOpen; }
    void SetPointerOn(TBool aOn);
    void SetBar(TInt aSideW);              // the bar at the side, this wide; 0: at the top
    void SetForeground(TBool aForeground);
    void ResetHover();                     // a new page: the old element numbers mean nothing; at the left
    CFbsBitmap* Buffer() const { return iBuffer; }
    TInt LayoutMs() const { return iLayoutMs; }
    TInt BoxCount() const { return iLayout ? (TInt)iLayout->boxCount : 0; }
    TInt PageHeight() const { return iLayout ? iLayout->height : 0; }

    // CCoeControl
    void Draw(const TRect& aRect) const;
    TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
    void HandlePointerEventL(const TPointerEvent& aPointerEvent);
    void SizeChanged();

private:
    CBrView(CBrAppUi& aAppUi, CImageLoader& aImages, CBrFonts& aFonts);
    void ConstructL(const TRect& aRect);
    void EnsureBufferL();
    void Render();                         // asks for a redraw: DoRender runs from its own callback
    void RenderLight();                    // the same when only the scroll position or the bars changed:
                                           // the page part is moved and only the strip it uncovers drawn
    void DoRender();                       // into iBuffer, then DrawDeferred
    static TInt RenderCallback(TAny* aSelf);
    void RenderSide(CFbsBitGc& aGc);       // full screen: the bar at the right (title, link address, status)
    void RenderHeader(CFbsBitGc& aGc);     // otherwise the bar at the top
    TInt DrawWrapped(CFbsBitGc& aGc, const CFont* aFont, const TDesC& aText, TInt aX, TInt aY, TInt aW, TInt aMaxLines);
    void RenderPage(CFbsBitGc& aGc, TInt aStripTop = -1, TInt aStripH = 0);   // a strip (screen y): only what is in it
    void RenderList(CFbsBitGc& aGc);
    void DrawControl(CFbsBitGc& aGc, const BwBox& aBox, const TRect& aRect, TBool aFocused);
    void DrawImage(CFbsBitGc& aGc, const BwBox& aBox, const TRect& aRect, TInt aPriority);
    void RequestImages();                  // queue the pictures near the screen
    TInt PageTop() const { return iSideBar ? 0 : iHeaderH; }
    TInt PageHeightOnScreen() const { return Rect().Height() - PageTop(); }
    TInt PageAreaW() const { return Rect().Width() - (iSideBar ? iSideW : 0); }   // the page and its scroll bar
    void ClampScroll();
    TInt PageWidthOnScreen() const;
    TBool TargetVisible(TInt aTarget, TBool aFully) const;
    void MoveFocus(TInt aDir);             // Up/Down: the next link if it is on screen, else scroll
    void JumpFocus(TInt aDir);             // Tab: the next link wherever it is
    void EnsureFocusVisible();
    void SetFocusTarget(TInt aTarget);
    TKeyResponse ListKeyL(const TKeyEvent& aKeyEvent);
    static TInt RelayoutCallback(TAny* aSelf);
    static TInt PointerTick(TAny* aSelf);
    void UpdatePointerTimer();
    void HoverAt(TPoint aPos, TBool aTick); // the pointer is here (view coordinates)

    CBrAppUi& iAppUi;
    CImageLoader& iImages;
    CBrFonts* iFonts;
    BwDoc* iDoc;
    BwLayout* iLayout;
    TInt iScrollY;
    TInt iScrollX;                         // > 0 only at the original size
    TInt iLayoutW;                         // the width the page was laid out at
    TInt iFocus;                           // target index, -1 none
    TInt iSideW;
    TInt iHeaderH;
    TBool iSideBar;                        // full screen: the bar at the side, the page the whole height
    TBuf<160> iTitle;
    TBuf<200> iStatusText;                 // lines separated by '\n'
    TBuf<80> iStats;
    HBufC8* iIconUrl;                      // zero-terminated; NULL: none
    TBuf<64> iHost;                        // shown by the icon in the side bar
    void DrawIcon(CFbsBitGc& aGc, const TRect& aRect);
    TBool iLoading;
    CFbsBitmap* iBuffer;
    CFbsBitmapDevice* iBufferDev;
    CFbsBitGc* iBufferGc;
    TInt iTraceRenders, iTraceDrawn;
    CAsyncCallBack* iRender;               // drawing from a shallow stack (8 KB on EKA1) and once per burst
    CAsyncCallBack* iRelayout;             // image sizes arrive in bursts: one relayout per burst
    TBool iRelayoutPending;
    TInt iLayoutMs;
    TBool iPointerOn;
    TBool iForeground;
    CPeriodic* iPointerTimer;              // polls the cursor: move events do not always come
    TPoint iPointerPos;
    TInt iHoverTarget;                     // the link under the pointer, -1 none
    TInt iHoverElem;                       // the element under it (for :hover), -1 none
    TInt iHoverSent;                       // the element the page was last styled for
    TInt iHoverQuiet;                      // ticks it has rested on iHoverElem
    TInt iEdgeTicks;                       // ticks at the top / bottom edge (scrolling speeds up)
    // what the buffer shows, for RenderLight: a full draw is needed unless only these moved
    TBool iNeedFull;
    TBool iDrawnOk;
    TInt iDrawnY, iDrawnX, iDrawnFocus, iDrawnHover, iDrawnFind, iDrawnFindOff;
    const BwLayout* iDrawnLayout;
    TInt iPointerMinY;                     // the highest the cursor has been (it stops short of the top)
    TInt iFindBox, iFindOff, iFindLen;     // the last match (box index; -1 none)
    // the list
    TBool iListOpen;
    TInt iListId;
    TBuf<80> iListTitle;
    RPointerArray<HBufC> iListItems;
    TInt iListSel;
    TInt iListFirst;
    MListObserver* iListObserver;
    };

// ------------------------------------------------------------------ app.cpp

// a password: the text is not shown
class CSecretDialog : public CEikDialog
    {
public:
    CSecretDialog(TDes& aText, const TDesC& aTitle) : iText(aText), iTitle(aTitle) {}
    void PreLayoutDynInitL();
    TBool OkToExitL(TInt aButtonId);
private:
    TDes& iText;
    const TDesC& iTitle;
    };

class CTextDialog : public CEikDialog
    {
public:
    // aMulti: Enter makes a new line (a textarea); otherwise Enter is OK
    CTextDialog(TDes& aText, const TDesC& aTitle, TBool aSelectAll = EFalse, TBool aMulti = EFalse)
        : iText(aText), iTitle(aTitle), iSelectAll(aSelectAll), iMulti(aMulti) {}
    void PreLayoutDynInitL();
    TBool OkToExitL(TInt aButtonId);
    TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
    ~CTextDialog();
private:
    TDes& iText;
    const TDesC& iTitle;
    TBool iSelectAll;
    TBool iMulti;
    TBool iCursorSuspended;
    };

// the address bar: the text, and the addresses visited (newest first) in the combo box's list
class CAddressDialog : public CEikDialog
    {
public:
    CAddressDialog(TDes& aText, CDesCArray* aHistory, TBool aSelectAll) : iText(aText), iHistory(aHistory), iSelectAll(aSelectAll) {}
    ~CAddressDialog();
    void PreLayoutDynInitL();
    TBool OkToExitL(TInt aButtonId);
    TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
private:
    TDes& iText;
    CDesCArray* iHistory;          // ours until the combo box takes it
    TBool iSelectAll;
    TBool iCursorSuspended;
    };

// before a download: the file's type and size, where it is from, its name (editable) and where to
// save it (a choice of folders)
class CDownloadDialog : public CEikDialog
    {
public:
    CDownloadDialog(TDes& aName, const TDesC& aFile, const TDesC& aFrom, CDesCArray* aPlaces, TInt& aPlace)
        : iName(aName), iFile(aFile), iFrom(aFrom), iPlaces(aPlaces), iPlace(aPlace) {}
    ~CDownloadDialog();
    void PreLayoutDynInitL();
    TBool OkToExitL(TInt aButtonId);
    TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
private:
    TDes& iName;
    const TDesC& iFile;
    const TDesC& iFrom;
    CDesCArray* iPlaces;           // ours until the choice list takes it
    TInt& iPlace;
    };

struct THistoryEntry
    {
    HBufC8* iUrl;
    HBufC* iTitle;
    HBufC8* iBody;          // the page as received (NULL when not kept)
    HBufC8* iContentType;
    TInt iScrollY;
    TInt iFocus;
    TBool iWasPost;
    };

class CBrAppUi : public CEikAppUi, public MPageObserver, public MImageObserver, public MHttpObserver, public MListObserver,
                 public MStyleObserver
    {
public:
    void ConstructL();
    ~CBrAppUi();
    CBrSettings* Settings() const { return iSettings; }
    void HandleCommandL(TInt aCommand);
    void DynInitMenuPaneL(TInt aResourceId, CEikMenuPane* aMenuPane);
    void HandleForegroundEventL(TBool aForeground);
    TKeyResponse HandleKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);

    // from the view
    void ActivateTargetL(TInt aTarget);
    void LinkFocused(TInt aTarget);
    TBool Loading() const { return iLoader->Busy(); }

    // navigation
    void OpenUrlL(const TDesC8& aUrl, const TDesC8* aPost = NULL, TBool aAddHistory = ETrue);
    void GoToDialogL(TUint aFirstChar = 0);
    void ZoomL(TInt aDir);                // Ctrl+Plus / Minus (+1 / -1) / 0: the text size
    static TInt StatsCallback(TAny* aSelf);
    void StatsTick();
    void UpdateStatsTimer(TBool aForeground);
    void SetPageIconL(const TDesC8& aUrl, const BwDoc* aDoc);
    HBufC8* LogPageLC(TBool aPrevious);   // about:log / about:prevlog: the log file's end as a page
    void ApplyCursor();                   // the phone's virtual cursor on / off as set (logged)
    void ReparseCurrentL();               // the page on the screen parsed again (hover, original size)
    void EditControlL(TInt aControl, TUint aFirstChar = 0);
    void HoverElementL(TInt aElem);       // the pointer rests on this element (-1 none)
    void BackL();
    void ForwardL();
    void ReloadL();
    void StopL();
    void HomeL();

    // MPageObserver
    void PageLoaded(const TDesC8& aUrl, const TDesC8& aContentType, HBufC8* aBody, TInt aStatus);
    static TInt ShowCallback(TAny* aSelf);
    void ShowPendingPage();               // the page that arrived, from a shallow stack
    void DropPendingPage();
    void PageDownloaded(const TDesC& aFile, const TDesC8& aContentType);
    void DownloadOffered(const TDesC& aName, const TDesC8& aContentType, TInt aLength);
    static TInt OfferCallback(TAny* aSelf);
    void AskDownloadL();
    void PageFailed(const TDesC& aText);
    void PageProgress(const TDesC& aText);
    void PageStep(const TDesC& aText);
    void StepNow(const TDesC& aText);   // the page's status line, drawn now (before a long step)
    void UpdateStatus(TBool aNow = EFalse);   // the status from its parts: the page, stylesheets, pictures
    // MImageObserver
    void ImageArrived(const TDesC8& aUrl, TBool aOk, TSize aFullSize);
    void ImageProgress(const TDesC& aText);
    // MHttpObserver (the helper requests: update, log)
    void HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody);
    void HttpProgress(const TDesC& aText);
    // MListObserver
    void ListChosen(TInt aListId, TInt aIndex);
    // MStyleObserver
    void StyleArrived(const TDesC8& aUrl, const TDesC8* aCss);
    void StylesDone();

    void Say(const TDesC& aText);

private:
    void ShowPageL(const TDesC8& aUrl, const TDesC8& aContentType, const TDesC8& aBody, TInt aScrollY, TInt aFocus, TBool aReparse = EFalse);
    BwDoc* ApplyStylesL(BwDoc* aDoc, const TDesC8& aBody, const char* aUrl, const char* aCt, TBool aReparse, TInt aCs);
    void RunScriptsL(const TDesC8& aUrl, const TDesC8& aBody, const char* aCt, const TDesC8*& aHtml, TInt& aCs);
    TInt FindCss(const TDesC8& aUrl) const;
    void FindL(TBool aNext);
    void CopyAddressL();
    TBuf<80> iPageStatus;               // the page's part of the status (stage, KB, the result)
    TInt iForceCs;                      // the encoding chosen by hand for this page (-1: automatic)
    // the last page after its scripts (UTF-8), for the same body read again (stylesheets came)
    HBufC8* iScriptedBody;              // NULL: the scripts wrote nothing (the body as it is)
    TBool iScriptedValid;
    TUint32 iScriptedKey;
    TInt iScriptedLen;
    TInt iScriptedForceCs;
    TInt iHover[32];                    // the element under the pointer and its ancestors (:hover)
    TUint iHoverCount;
    void ShowInternalL(const TDesC8& aUrl);
    void ShowLoadedL(const TDesC8& aUrl, const TDesC8& aContentType, HBufC8*& aBody);
    HBufC8* StartPageLC();
    HBufC8* BookmarksPageLC(TBool aHistory);
    void AddHistoryL(const TDesC8& aUrl);
    void SaveHistoryPosition();
    void TrimHistoryCache();
    void FreeHistoryEntry(THistoryEntry& aEntry);
    void LoadBookmarksL();
    void SaveBookmarks();
    void AddBookmarkL();
    void DeleteBookmark(TInt aIndex);
    void LoadCookies();
    void SaveCookies();
    void SubmitL(TInt aForm, TInt aSubmitter);
    void SelectOptionL(TInt aControl);
    void PageInfoL();
    void UpdateFromPcL();
    void SendLogL();
    void SendScreenshotL();
    void ServerDialogL();
    void HomeDialogL();
    void ApplyFullScreenL();
    void ApplySettingsL();
    void UpdateTitle();
    static TInt RefreshCallback(TAny* aSelf);
    static TInt ExitCallback(TAny* aSelf);
    CAsyncCallBack* iExitCallback;
    TBool TextDialogL(const TDesC& aTitle, TDes& aText);

    CBrView* iView;
    CBrSettings* iSettings;
    CBrFonts* iFonts;
    CPageLoader* iLoader;
    CImageLoader* iImages;
    CHttpClient* iHelperHttp;
    TInt iHelperOp;                // 1 update, 2 log upload, 3 screenshot
    BwCookieJar iCookies;
    RArray<THistoryEntry> iHistory;        // Back / Forward (this run)
    // the pages visited, newest first, each once, kept across runs (history.txt)
    RPointerArray<HBufC8> iVisitUrls;
    RPointerArray<HBufC> iVisitTitles;
    TBool iVisitsDirty;
    // read on first use, not at start (each is a file read and a parse on flash)
    TBool iVisitsLoaded, iDlLoaded, iCookiesLoaded, iNullTried;
    void EnsureVisits();
    void EnsureDownloads();
    void EnsureCookies();
    TInt iStartupMs;
    void RecordVisitL(const TDesC8& aUrl, const TDesC* aTitle);
    // the files downloaded, newest first, kept across runs (downloads.txt: path TAB URL)
    RPointerArray<HBufC> iDlPaths;
    RPointerArray<HBufC8> iDlUrls;
    void RecordDownloadL(const TDesC& aPath, const TDesC8& aUrl);
    void LoadDownloadsL();
    void SaveDownloads();
    void RemoveDownload(TInt aIndex);
    HBufC8* DownloadsPageLC();
    void OpenFileL(const TDesC& aPath);
    void LoadVisitsL();
    void SaveVisits();
    TInt iHistoryPos;              // the entry on screen
    TBool iNavigatingHistory;      // the load comes from Back/Forward: no new entry
    TInt iRestoreScroll, iRestoreFocus;
    RPointerArray<HBufC8> iBookmarkUrls;
    RPointerArray<HBufC> iBookmarkTitles;
    HBufC8* iCurrentUrl;
    TInt iSelectControl;           // the <select> whose list is open
    CPeriodic* iRefreshTimer;
    CAsyncCallBack* iShowCallback;
    CAsyncCallBack* iOfferCallback;     // the download prompt, from a shallow stack
    TBuf<64> iOfferName;
    TBuf8<80> iOfferCt;
    TInt iOfferLength;
    CAsyncCallBack* iStylesCallback;
    static TInt StylesCallback(TAny* aSelf);
    void ApplyArrivedStyles();
    HBufC8* iPendUrl;
    HBufC8* iPendCt;
    HBufC8* iPendBody;
    TInt iPendStatus;
    CPeriodic* iStatsTimer;             // CPU and RAM, once a second while in front
    TInt64 iStatsWall, iStatsApp, iStatsIdle;
    RThread iNullThread;                // the kernel's idle thread: its CPU time is the idle time
    TBool iNullOpen;
    TBool iStatsStarted;
    // without CPU times: a lowest-priority thread counts how far it gets in ~0.1 s each second
    // (it only runs when nothing else wants the CPU; the most it ever got = an idle phone)
    struct TCpuProbe
        {
        RSemaphore iGo;
        volatile TInt iCount;
        volatile TInt iDone;
        volatile TBool iQuit;
        };
    TCpuProbe* iProbe;
    RThread iProbeThread;
    TBool iProbeOn;
    TInt iProbeSeen, iProbeMax;
    static TInt ProbeThread(TAny* aProbe);
    void StartProbeL();
    HBufC8* iRefreshUrl;
    TInt iLoadStartMs, iParseMs, iDownloadMs, iPageBytes;
    TBool iSubmitAfterEdit;
    CStyleFetcher* iStyles;
    RPointerArray<HBufC8> iCssUrls;     // the stylesheet cache, oldest first (an empty text: failed)
    RPointerArray<HBufC8> iCssTexts;
    TInt iCssBytes;
    TInt iStylesMissing;
    TInt iStylesGot;                    // stylesheets that arrived for this page
    HBufC* iFindText;
    };

class CBrDocument : public CEikDocument
    {
public:
    CBrDocument(CEikApplication& aApp) : CEikDocument(aApp) {}
    CEikAppUi* CreateAppUiL() { return new (ELeave) CBrAppUi; }
    };

class CBrApplication : public CEikApplication
    {
public:
    TUid AppDllUid() const { return KUidBrowser9300; }
protected:
    CApaDocument* CreateDocumentL() { return new (ELeave) CBrDocument(*this); }
    };

#endif
