// Browser 9300 - loading a page: the request with cookies and referer, redirects, the content
// type (a page, a picture, or a file to save), a watchdog for stalled transfers.

#include <apgcli.h>
#include "browser.h"

const TInt KMaxRedirects = 10;
const TInt KWatchdogMs = 5000;
const TInt KStallTicks = 9;             // 45 s without a byte: give up

CPageLoader* CPageLoader::NewL(MPageObserver& aObserver, BwCookieJar& aCookies)
    {
    CPageLoader* self = new (ELeave) CPageLoader(aObserver, aCookies);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop(self);
    return self;
    }

CPageLoader::CPageLoader(MPageObserver& aObserver, BwCookieJar& aCookies)
    : iObserver(aObserver), iCookies(aCookies)
    {
    }

void CPageLoader::ConstructL()
    {
    iHttp = CHttpClient::NewL();
    iWatchdog = CPeriodic::NewL(CActive::EPriorityStandard);
    iUrl = HBufC8::NewL(1);
    }

CPageLoader::~CPageLoader()
    {
    if (iWatchdog) iWatchdog->Cancel();
    delete iWatchdog;
    delete iHttp;
    delete iUrl;
    delete iPost;
    delete iReferer;
    }

void CPageLoader::LoadL(const TDesC8& aUrl, const TDesC8* aPostBody, const TDesC8& aReferer)
    {
    Stop();
    HBufC8* url = aUrl.AllocL();
    delete iUrl;
    iUrl = url;
    delete iPost;
    iPost = NULL;
    if (aPostBody) iPost = aPostBody->AllocL();
    delete iReferer;
    iReferer = NULL;
    iReferer = aReferer.AllocL();
    iRedirects = 0;
    iStartMs = (TInt)TickMs();
    SendL();
    }

void CPageLoader::Stop()
    {
    iOffered = EFalse;
    iDownloadPath.Zero();
    if (iBusy) iHttp->Abort();
    iBusy = EFalse;
    iWatchdog->Cancel();
    }

void CPageLoader::SendL()
    {
    // the URL as a C string for the core
    HBufC8* z = HBufC8::NewLC(iUrl->Length() + 1);
    z->Des().Copy(*iUrl);
    z->Des().ZeroTerminate();
    const char* u = (const char*)z->Ptr();
    char host[128];
    int port = 0, secure = 0;
    const char* path = NULL;
    unsigned int pathLen = 0;
    if (!bw_url_split(u, host, sizeof(host), &port, &secure, &path, &pathLen))
        {
        CleanupStack::PopAndDestroy(z);
        iBusy = EFalse;
        iObserver.PageFailed(_L("Only http:// and https:// addresses can be opened"));
        return;
        }
    iHost.Copy(TPtrC8((const TUint8*)host));
    TPtrC8 pathDes((const TUint8*)path, pathLen);
    // a path with the host for the cookies
    HBufC8* zpath = HBufC8::NewLC(pathLen + 1);
    zpath->Des().Copy(pathDes);
    zpath->Des().ZeroTerminate();

    HBufC8* headers = HBufC8::NewLC(4096 + (iReferer ? iReferer->Length() : 0));
    TPtr8 h = headers->Des();
    h.Append(_L8("User-Agent: "));
    h.Append(BrUserAgent());
    h.Append(_L8("\r\nAccept: text/html,application/xhtml+xml,text/plain;q=0.9,image/*;q=0.5,*/*;q=0.3\r\n"
                 "Accept-Language: cs,en;q=0.8\r\nAccept-Encoding: gzip\r\n"));
    if (iReferer && iReferer->Length() && iReferer->Length() < 1000 && iReferer->Left(4) == _L8("http"))
        {
        h.Append(_L8("Referer: "));
        h.Append(*iReferer);
        h.Append(_L8("\r\n"));
        }
    char* cookie = bw_cookies_header(&iCookies, host, (const char*)zpath->Ptr(), secure, NowUnixSeconds());
    if (cookie)
        {
        TPtrC8 c((const TUint8*)cookie);
        if (c.Length() < 3000)
            {
            h.Append(_L8("Cookie: "));
            h.Append(c);
            h.Append(_L8("\r\n"));
            }
        ig_free(cookie);
        }
    if (iPost) h.Append(_L8("Content-Type: application/x-www-form-urlencoded\r\n"));
    iBusy = ETrue;
    iLastBytes = 0;
    iIdleTicks = 0;
    if (IgLogEnabled())
        {
        HBufC8* l = HBufC8::New(320);
        if (l) { l->Des().Append(iPost ? _L8("page POST ") : _L8("page GET ")); l->Des().Append(iUrl->Left(280)); IgLog8(*l); delete l; }
        }
    const TDesC8& body = iPost ? (const TDesC8&)*iPost : (const TDesC8&)KNullDesC8;
    if (iPost) iHttp->RequestL(iHost, _L8("POST"), pathDes, h, body, *this, secure != 0, port);
    else iHttp->RequestL(iHost, _L8("GET"), pathDes, h, body, *this, secure != 0, port);
    CleanupStack::PopAndDestroy(3);    // headers, zpath, z
    iWatchdog->Cancel();
    iWatchdog->Start(KWatchdogMs * 1000, KWatchdogMs * 1000, TCallBack(WatchdogTick, this));
    }

TInt CPageLoader::WatchdogTick(TAny* aSelf)
    {
    CPageLoader* self = (CPageLoader*)aSelf;
    if (!self->iBusy) { self->iWatchdog->Cancel(); return 0; }
    if (self->iOffered) { self->iIdleTicks = 0; return 0; }          // the user is deciding
    TInt now = self->iHttp->BytesReceived();
    if (now != self->iLastBytes) { self->iLastBytes = now; self->iIdleTicks = 0; return 0; }
    if (++self->iIdleTicks >= KStallTicks)
        {
        IgLog(_L("page: no data for 45 s, giving up"));
        self->Stop();
        self->iHttp->CloseConnection();
        self->iObserver.PageFailed(_L("The server stopped answering (45 s)"));
        }
    return 0;
    }

void CPageLoader::StoreCookies(const TDesC8& aHeaders)
    {
    char host[128];
    int port, secure;
    const char* path;
    unsigned int pathLen;
    HBufC8* z = HBufC8::New(iUrl->Length() + 1);
    if (!z) return;
    z->Des().Copy(*iUrl);
    z->Des().ZeroTerminate();
    if (bw_url_split((const char*)z->Ptr(), host, sizeof(host), &port, &secure, &path, &pathLen))
        {
        HBufC8* zpath = HBufC8::New(pathLen + 1);
        if (zpath)
            {
            zpath->Des().Copy(TPtrC8((const TUint8*)path, pathLen));
            zpath->Des().ZeroTerminate();
            for (TInt nth = 0; nth < 40; nth++)
                {
                char* v = ig_http_header_dup((const char*)aHeaders.Ptr(), aHeaders.Length(), "Set-Cookie", nth);
                if (!v) break;
                bw_cookies_set(&iCookies, host, (const char*)zpath->Ptr(), v, ig_strlen(v), NowUnixSeconds());
                ig_free(v);
                }
            delete zpath;
            }
        }
    delete z;
    }

void CPageLoader::HttpProgress(const TDesC& aText)
    {
    iObserver.PageProgress(aText);
    }

void CPageLoader::HttpStep(const TDesC& aText)
    {
    iObserver.PageStep(aText);
    }

static void DownloadName(const TDesC8& aUrl, const TDesC8& aDisposition, TDes& aName);

// a page to show (else a file to download): HTML, text, XML, pictures; not a .jad, not an attachment
TBool CPageLoader::IsPageType(const TDesC8& aCt, const TDesC8& aDisposition)
    {
    if (aDisposition.Length() && aDisposition.Left(10).CompareF(_L8("attachment")) == 0) return EFalse;
    if (aCt.FindF(_L8("text/vnd.sun.j2me.app-descriptor")) == 0) return EFalse;
    return aCt.Length() == 0 || aCt.FindF(_L8("html")) >= 0 || aCt.FindF(_L8("text/")) == 0 ||
           aCt.FindF(_L8("image/")) == 0 || aCt.FindF(_L8("xml")) >= 0;
    }

// The headers are in: a file (not a page, a 200) pauses the transfer and is offered to the user
TBool CPageLoader::HttpHeadersArrived(const TDesC8& aHeaders, TInt aStatus)
    {
    if (aStatus != 200) return ETrue;
    const char* hd = (const char*)aHeaders.Ptr();
    TInt hl = aHeaders.Length();
    char* ct = ig_http_header_dup(hd, hl, "Content-Type", 0);
    char* disp = ig_http_header_dup(hd, hl, "Content-Disposition", 0);
    char* len = ig_http_header_dup(hd, hl, "Content-Length", 0);
    TPtrC8 ctype(ct ? (const TUint8*)ct : (const TUint8*)"");
    TPtrC8 dispo(disp ? (const TUint8*)disp : (const TUint8*)"");
    TBool page = IsPageType(ctype, dispo);
    if (!page)
        {
        TBuf<64> name;
        DownloadName(*iUrl, dispo, name);
        TInt length = len ? ig_atoi(len) : -1;
        HBufC8* ctCopy = ctype.Left(80).Alloc();
        iOffered = ETrue;
        iDownloadPath.Zero();
        iObserver.DownloadOffered(name, ctCopy ? *ctCopy : KNullDesC8(), length);
        delete ctCopy;
        }
    ig_free(ct); ig_free(disp); ig_free(len);
    return page;
    }

void CPageLoader::AcceptDownloadL(const TDesC& aPath)
    {
    if (!iOffered || !iBusy) return;
    iOffered = EFalse;
    iDownloadPath.Copy(aPath);
    iLastBytes = iHttp->BytesReceived();
    iIdleTicks = 0;
    iHttp->ResumeL(aPath);
    }

// the file name for a download: the last part of the path, else "download"
static void DownloadName(const TDesC8& aUrl, const TDesC8& aDisposition, TDes& aName)
    {
    TPtrC8 n;
    TInt fn = aDisposition.FindF(_L8("filename="));
    if (fn >= 0)
        {
        n.Set(aDisposition.Mid(fn + 9));
        if (n.Length() && n[0] == '"') n.Set(n.Mid(1));
        TInt e = n.Locate('"');
        if (e < 0) e = n.Locate(';');
        if (e >= 0) n.Set(n.Left(e));
        }
    else
        {
        TPtrC8 u(aUrl);
        TInt q = u.Locate('?');
        if (q >= 0) u.Set(u.Left(q));
        TInt h = u.Locate('#');
        if (h >= 0) u.Set(u.Left(h));
        TInt s = u.LocateReverse('/');
        n.Set(s >= 0 ? u.Mid(s + 1) : u);
        }
    aName.Zero();
    for (TInt i = 0; i < n.Length() && aName.Length() < 60; i++)
        {
        TChar c = n[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == '"' || c < 32) c = '_';
        aName.Append(c);
        }
    if (!aName.Length()) aName.Copy(_L("download"));
    }

void CPageLoader::HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody)
    {
    iWatchdog->Cancel();
    if (!iBusy) return;
    if (aError != KErrNone)
        {
        iBusy = EFalse;
        TBuf<120> t;
        if (iHttp->LastError().Length()) t.Copy(iHttp->LastError());
        else t.Format(_L("Error %d"), aError);
        iObserver.PageFailed(t);
        return;
        }
    StoreCookies(aHeaders);
    const char* hd = (const char*)aHeaders.Ptr();
    TInt hl = aHeaders.Length();
    if ((aHttpStatus == 301 || aHttpStatus == 302 || aHttpStatus == 303 || aHttpStatus == 307 || aHttpStatus == 308))
        {
        char* loc = ig_http_header_dup(hd, hl, "Location", 0);
        if (loc && iRedirects < KMaxRedirects)
            {
            HBufC8* z = HBufC8::New(iUrl->Length() + 1);
            char* next = NULL;
            if (z)
                {
                z->Des().Copy(*iUrl);
                z->Des().ZeroTerminate();
                next = bw_url_resolve((const char*)z->Ptr(), loc, ig_strlen(loc));
                delete z;
                }
            ig_free(loc);
            if (next)
                {
                TPtrC8 n((const TUint8*)next);
                HBufC8* nu = n.Alloc();
                ig_free(next);
                if (nu)
                    {
                    // the referer stays the page that asked for the first URL
                    delete iUrl;
                    iUrl = nu;
                    iRedirects++;
                    if (aHttpStatus != 307 && aHttpStatus != 308) { delete iPost; iPost = NULL; }
                    IgLogF(_L("redirect %d"), aHttpStatus);
                    TRAPD(err, SendL());
                    if (err != KErrNone)
                        {
                        iBusy = EFalse;
                        iObserver.PageFailed(_L("Redirect failed"));
                        }
                    return;
                    }
                }
            }
        else ig_free(loc);
        }
    iBusy = EFalse;
    char* ct = ig_http_header_dup(hd, hl, "Content-Type", 0);
    TPtrC8 ctype(ct ? (const TUint8*)ct : (const TUint8*)"");
    char* dispo0 = ig_http_header_dup(hd, hl, "Content-Disposition", 0);
    TBool page = IsPageType(ctype, dispo0 ? TPtrC8((const TUint8*)dispo0) : TPtrC8());
    ig_free(dispo0);
    if (page)
        {
        HBufC8* body = aBody.Alloc();
        if (!body) { ig_free(ct); iObserver.PageFailed(_L("Out of memory")); return; }
        HBufC8* ctCopy = ctype.Alloc();
        if (!ctCopy) { delete body; ig_free(ct); iObserver.PageFailed(_L("Out of memory")); return; }
        ig_free(ct);
        CleanupStack::PushL(ctCopy);
        iObserver.PageLoaded(*iUrl, *ctCopy, body, aHttpStatus);
        CleanupStack::PopAndDestroy(ctCopy);
        return;
        }
    // a file: save it (where the user chose; it may be written already) and offer to open it
    char* disp = ig_http_header_dup(hd, hl, "Content-Disposition", 0);
    TFileName name;
    if (iDownloadPath.Length()) name.Copy(iDownloadPath);
    else
        {
        name.Copy(_L("C:\\System\\Temp\\"));
        TBuf<64> fn;
        DownloadName(*iUrl, disp ? TPtrC8((const TUint8*)disp) : TPtrC8(), fn);
        name.Append(fn);
        }
    ig_free(disp);
    iDownloadPath.Zero();
    TBool written = iHttp->SinkUsed();
    RFs fs;
    TInt err = written ? KErrNone : fs.Connect();
    if (err == KErrNone && !written)
        {
        fs.MkDirAll(name);
        RFile f;
        err = f.Replace(fs, name, EFileWrite);
        if (err == KErrNone)
            {
            err = f.Write(aBody);
            f.Close();
            }
        fs.Close();
        }
    if (err != KErrNone)
        {
        ig_free(ct);
        TBuf<80> t;
        t.Format(_L("Could not save the file (%d)"), err);
        iObserver.PageFailed(t);
        return;
        }
    HBufC8* ctCopy = ctype.Alloc();
    ig_free(ct);
    if (ctCopy) iObserver.PageDownloaded(name, *ctCopy);
    else iObserver.PageDownloaded(name, KNullDesC8);
    delete ctCopy;
    }

// ------------------------------------------------------------------ stylesheets
// The page's external stylesheets, one at a time over a connection of their own (so the
// pictures do not wait): cookies, up to 3 redirects, a watchdog. Each result goes to the
// observer (the app caches them per URL and lays the page out again when all are in).

const TInt KMaxStyleRedirects = 3;
const TInt KMaxStyleBytes = 400 * 1024;

const TInt KMaxScriptBytes = 512 * 1024;

CStyleFetcher* CStyleFetcher::NewL(MStyleObserver& aObserver, BwCookieJar& aCookies, TBool aScripts)
    {
    CStyleFetcher* self = new (ELeave) CStyleFetcher(aObserver, aCookies);
    self->iScripts = aScripts;
    CleanupStack::PushL(self);
    self->iHttp = CHttpClient::NewL();
    self->iHttp->SetQuiet(ETrue);
    self->iWatchdog = CPeriodic::NewL(CActive::EPriorityStandard);
    CleanupStack::Pop(self);
    return self;
    }

CStyleFetcher::CStyleFetcher(MStyleObserver& aObserver, BwCookieJar& aCookies)
    : iObserver(aObserver), iCookies(aCookies)
    {
    }

CStyleFetcher::~CStyleFetcher()
    {
    if (iWatchdog) iWatchdog->Cancel();
    delete iWatchdog;
    delete iHttp;
    iQueue.ResetAndDestroy();
    delete iCurrent;
    delete iCurrentFinal;
    }

void CStyleFetcher::Stop()
    {
    iWatchdog->Cancel();
    if (iHttp->Busy()) iHttp->Abort();
    iQueue.ResetAndDestroy();
    delete iCurrent; iCurrent = NULL;
    delete iCurrentFinal; iCurrentFinal = NULL;
    }

void CStyleFetcher::AddL(const TDesC8& aUrl)
    {
    if (iCurrent && *iCurrent == aUrl) return;
    for (TInt i = 0; i < iQueue.Count(); i++) if (*iQueue[i] == aUrl) return;
    HBufC8* u = aUrl.AllocLC();
    User::LeaveIfError(iQueue.Append(u));
    CleanupStack::Pop(u);
    }

void CStyleFetcher::StartL()
    {
    if (iCurrent || !iQueue.Count()) return;
    iCurrent = iQueue[0];
    iQueue.Remove(0);
    iRedirects = 0;
    delete iCurrentFinal;
    iCurrentFinal = iCurrent->AllocL();
    TRAPD(err, SendL());
    if (err != KErrNone) Done(NULL);
    }

void CStyleFetcher::SendL()
    {
    HBufC8* z = HBufC8::NewLC(iCurrentFinal->Length() + 1);
    z->Des().Copy(*iCurrentFinal);
    z->Des().ZeroTerminate();
    char host[128];
    int port, secure;
    const char* path;
    unsigned int pathLen;
    if (!bw_url_split((const char*)z->Ptr(), host, sizeof(host), &port, &secure, &path, &pathLen)) User::Leave(KErrArgument);
    iHost.Copy(TPtrC8((const TUint8*)host));
    HBufC8* h = HBufC8::NewLC(3500);
    TPtr8 hp = h->Des();
    hp.Append(_L8("User-Agent: "));
    hp.Append(BrUserAgent());
    if (iScripts) hp.Append(_L8("\r\nAccept: */*\r\nAccept-Encoding: gzip\r\n"));
    else hp.Append(_L8("\r\nAccept: text/css,*/*;q=0.1\r\nAccept-Encoding: gzip\r\n"));
    HBufC8* zp = HBufC8::NewLC(pathLen + 1);
    zp->Des().Copy(TPtrC8((const TUint8*)path, pathLen));
    zp->Des().ZeroTerminate();
    char* cookie = bw_cookies_header(&iCookies, host, (const char*)zp->Ptr(), secure, NowUnixSeconds());
    if (cookie)
        {
        TPtrC8 c((const TUint8*)cookie);
        if (c.Length() < 3000) { hp.Append(_L8("Cookie: ")); hp.Append(c); hp.Append(_L8("\r\n")); }
        ig_free(cookie);
        }
    iHttp->RequestL(iHost, _L8("GET"), TPtrC8((const TUint8*)path, pathLen), hp, KNullDesC8, *this, secure != 0, port);
    CleanupStack::PopAndDestroy(3);
    iLastBytes = 0;
    iIdleTicks = 0;
    iWatchdog->Cancel();
    iWatchdog->Start(5000000, 5000000, TCallBack(WatchdogTick, this));
    }

TInt CStyleFetcher::WatchdogTick(TAny* aSelf)
    {
    CStyleFetcher* self = (CStyleFetcher*)aSelf;
    if (!self->iCurrent) { self->iWatchdog->Cancel(); return 0; }
    TInt now = self->iHttp->BytesReceived();
    if (now != self->iLastBytes) { self->iLastBytes = now; self->iIdleTicks = 0; return 0; }
    if (++self->iIdleTicks >= 6)
        {
        if (self->iScripts) IgLog(_L("script: no data for 30 s"));
        else IgLog(_L("stylesheet: no data for 30 s"));
        self->iHttp->Abort();
        self->Done(NULL);
        }
    return 0;
    }

void CStyleFetcher::HttpProgress(const TDesC& /*aText*/)
    {
    }

void CStyleFetcher::HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody)
    {
    iWatchdog->Cancel();
    if (!iCurrent) return;
    if (aError == KErrNone && aHttpStatus >= 301 && aHttpStatus <= 308 && iRedirects < KMaxStyleRedirects)
        {
        char* loc = ig_http_header_dup((const char*)aHeaders.Ptr(), aHeaders.Length(), "Location", 0);
        HBufC8* z = loc ? HBufC8::New(iCurrentFinal->Length() + 1) : NULL;
        char* next = NULL;
        if (z)
            {
            z->Des().Copy(*iCurrentFinal);
            z->Des().ZeroTerminate();
            next = bw_url_resolve((const char*)z->Ptr(), loc, ig_strlen(loc));
            delete z;
            }
        ig_free(loc);
        if (next)
            {
            HBufC8* n = TPtrC8((const TUint8*)next).Alloc();
            ig_free(next);
            if (n)
                {
                delete iCurrentFinal;
                iCurrentFinal = n;
                iRedirects++;
                TRAPD(err, SendL());
                if (err == KErrNone) return;
                }
            }
        }
    if (aError != KErrNone || aHttpStatus != 200 || aBody.Length() > (iScripts ? KMaxScriptBytes : KMaxStyleBytes))
        {
        if (iScripts) IgLogF2(_L("script failed: err %d HTTP %d"), aError, aHttpStatus);
        else IgLogF2(_L("stylesheet failed: err %d HTTP %d"), aError, aHttpStatus);
        Done(NULL);
        return;
        }
    if (iScripts) IgLogF(_L("script: %d bytes"), aBody.Length());
    else IgLogF(_L("stylesheet: %d bytes"), aBody.Length());
    Done(&aBody);
    }

void CStyleFetcher::Done(const TDesC8* aCss)
    {
    HBufC8* url = iCurrent;
    iCurrent = NULL;
    if (url)
        {
        if (iScripts) iObserver.ScriptArrived(*url, aCss); else iObserver.StyleArrived(*url, aCss);
        delete url;
        }
    if (iQueue.Count())
        {
        TRAPD(err, StartL());
        (void)err;
        }
    else if (iScripts) iObserver.ScriptsDone();
    else iObserver.StylesDone();
    }
