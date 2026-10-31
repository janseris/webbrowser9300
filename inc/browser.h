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
#include <coecntrl.h>
#include <bitdev.h>
#include <bitstd.h>
#include <f32file.h>
#include "bwcore.h"

const TUid KUidBrowser9300 = { 0x0F5A9310 };
_LIT(KBrVersion, "0.4");
_LIT8(KBrUserAgent, "Mozilla/5.0 (Series80/2.0; Nokia9300; Profile/MIDP-2.0 Configuration/CLDC-1.1) Browser9300/0.4");
_LIT(KBrDataDir, "C:\\System\\Data\\browser9300\\");

// ------------------------------------------------------------------ util.cpp

HBufC* Utf8ToUnicodeL(const TDesC8& aText);
HBufC* Utf8ToUnicodeL(const char* aText);
char* UnicodeToUtf8(const TDesC& aText);          // ig_malloc'ed, NULL on failure
HBufC8* UnicodeToUtf8L(const TDesC& aText);
TUint NowUnixSeconds();
TUint32 TickMs();                                  // milliseconds (for timings)

void IgLogEnable(TBool aOn);
TBool IgLogEnabled();
void IgLog(const TDesC& aText);
void IgLog8(const TDesC8& aText);
void IgLogF(const TDesC& aFormat, TInt aValue);
void IgLogF2(const TDesC& aFormat, TInt aValue1, TInt aValue2);
void IgLogRotate();
void IgLogFlush();
void IgLogClose();
const TDesC& LogFileName();
const TDesC& LogPrevFileName();

// ------------------------------------------------------------------ net.cpp

class MHttpObserver
    {
public:
    virtual void HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody) = 0;
    virtual void HttpProgress(const TDesC& aText) = 0;
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
    virtual void PageFailed(const TDesC& aText) = 0;
    virtual void PageProgress(const TDesC& aText) = 0;
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
private:
    CPageLoader(MPageObserver& aObserver, BwCookieJar& aCookies);
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
    TInt Pending() const { return iQueue.Count() + (iCurrent ? 1 : 0); }
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
    CFbsBitmap* iDecoded;
    TSize iFullNow;
    TUint iUse;
    TInt iBytes;
    TInt iGen;
    TSize iMax;
    TBool iEnabled;
    TBuf8<128> iHostBuf;
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
    TBool iFullScreen;
    TInt iTextSize;                 // -1, 0, 1
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
    void SetTitle(const TDesC& aText);     // the header's left part (title or address)
    void SetLoading(TBool aLoading);
    TInt ScrollY() const { return iScrollY; }
    TInt Focus() const { return iFocus; }
    void ScrollToAnchor(const TDesC8& aName);
    void ScrollTo(TInt aY);
    TBool Find(const TDesC& aText, TBool aNext);
    void ShowListL(TInt aId, const TDesC& aTitle, const RPointerArray<HBufC>& aItems, TInt aSelected, MListObserver& aObserver);
    TBool ListOpen() const { return iListOpen; }
    void SetPointerOn(TBool aOn);
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
    void Render();                         // into iBuffer, then DrawDeferred
    void RenderHeader(CFbsBitGc& aGc);
    void RenderPage(CFbsBitGc& aGc);
    void RenderList(CFbsBitGc& aGc);
    void DrawControl(CFbsBitGc& aGc, const BwBox& aBox, const TRect& aRect, TBool aFocused);
    void DrawImage(CFbsBitGc& aGc, const BwBox& aBox, const TRect& aRect, TInt aPriority);
    void RequestImages();                  // queue the pictures near the screen
    TInt PageTop() const { return iHeaderH; }
    TInt PageHeightOnScreen() const { return Rect().Height() - iHeaderH; }
    void ClampScroll();
    TBool TargetVisible(TInt aTarget, TBool aFully) const;
    void MoveFocus(TInt aDir);             // Up/Down: the next link if it is on screen, else scroll
    void JumpFocus(TInt aDir);             // Tab: the next link wherever it is
    void EnsureFocusVisible();
    void SetFocusTarget(TInt aTarget);
    TKeyResponse ListKeyL(const TKeyEvent& aKeyEvent);
    static TInt RelayoutCallback(TAny* aSelf);

    CBrAppUi& iAppUi;
    CImageLoader& iImages;
    CBrFonts* iFonts;
    BwDoc* iDoc;
    BwLayout* iLayout;
    TInt iScrollY;
    TInt iFocus;                           // target index, -1 none
    TInt iHeaderH;
    TBuf<160> iTitle;
    TBuf<120> iStatusText;
    TBool iLoading;
    CFbsBitmap* iBuffer;
    CFbsBitmapDevice* iBufferDev;
    CFbsBitGc* iBufferGc;
    CAsyncCallBack* iRelayout;             // image sizes arrive in bursts: one relayout per burst
    TBool iRelayoutPending;
    TInt iLayoutMs;
    TBool iPointerOn;
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
    CTextDialog(TDes& aText, const TDesC& aTitle, TBool aSelectAll = EFalse) : iText(aText), iTitle(aTitle), iSelectAll(aSelectAll) {}
    void PreLayoutDynInitL();
    TBool OkToExitL(TInt aButtonId);
private:
    TDes& iText;
    const TDesC& iTitle;
    TBool iSelectAll;
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
    void BackL();
    void ForwardL();
    void ReloadL();
    void StopL();
    void HomeL();

    // MPageObserver
    void PageLoaded(const TDesC8& aUrl, const TDesC8& aContentType, HBufC8* aBody, TInt aStatus);
    void PageDownloaded(const TDesC& aFile, const TDesC8& aContentType);
    void PageFailed(const TDesC& aText);
    void PageProgress(const TDesC& aText);
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
    BwDoc* ApplyStylesL(BwDoc* aDoc, const TDesC8& aBody, const char* aUrl, const char* aCt, TBool aReparse);
    TInt FindCss(const TDesC8& aUrl) const;
    void FindL(TBool aNext);
    void CopyAddressL();
    TInt iForceCs;                      // the encoding chosen by hand for this page (-1: automatic)
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
    void EditControlL(TInt aControl);
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
    RArray<THistoryEntry> iHistory;
    TInt iHistoryPos;              // the entry on screen
    TBool iNavigatingHistory;      // the load comes from Back/Forward: no new entry
    TInt iRestoreScroll, iRestoreFocus;
    RPointerArray<HBufC8> iBookmarkUrls;
    RPointerArray<HBufC> iBookmarkTitles;
    HBufC8* iCurrentUrl;
    TInt iSelectControl;           // the <select> whose list is open
    CPeriodic* iRefreshTimer;
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
