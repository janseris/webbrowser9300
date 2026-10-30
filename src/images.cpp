// Browser 9300 - the page's pictures: one download at a time (its own keep-alive connection,
// so pictures do not wait behind the page), nearest the screen first; decoded with the Image
// Conversion Library at the 1/2-1/8 reduction that still covers the page width, box-scaled,
// transparent parts put on white; one bitmap per URL kept in RAM while the phone has RAM free.
// From Instagram 9300's image store (0.57).

#include <imageconversion.h>
#include <fbs.h>
#include <hal.h>
#include "browser.h"

const TInt KRamReserve = 3 * 1024 * 1024;
const TInt KMaxImageFile = 700 * 1024;
const TInt KMaxImageRedirects = 4;

CImageLoader::CEntry::~CEntry() { delete iUrl; delete iBitmap; }
CImageLoader::CRequest::~CRequest() { delete iUrl; }

CImageLoader* CImageLoader::NewL(MImageObserver& aObserver, BwCookieJar& aCookies)
    {
    CImageLoader* self = new (ELeave) CImageLoader(aObserver, aCookies);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop(self);
    return self;
    }

CImageLoader::CImageLoader(MImageObserver& aObserver, BwCookieJar& aCookies)
    : CActive(CActive::EPriorityLow), iObserver(aObserver), iCookies(aCookies), iMax(480, 1200), iEnabled(ETrue)
    {
    CActiveScheduler::Add(this);
    }

void CImageLoader::ConstructL()
    {
    User::LeaveIfError(iFs.Connect());
    iHttp = CHttpClient::NewL();
    iHttp->SetQuiet(ETrue);
    iKicker = new (ELeave) CAsyncCallBack(TCallBack(KickCallback, this), CActive::EPriorityLow);
    }

CImageLoader::~CImageLoader()
    {
    Cancel();
    delete iKicker;
    delete iDecoder;
    delete iDecoded;
    delete iData;
    delete iCurrent;
    delete iHttp;
    iQueue.ResetAndDestroy();
    iEntries.ResetAndDestroy();
    iFs.Close();
    }

CImageLoader::CEntry* CImageLoader::Find(const TDesC8& aUrl)
    {
    for (TInt i = 0; i < iEntries.Count(); i++)
        if (*iEntries[i]->iUrl == aUrl) return iEntries[i];
    return NULL;
    }

static TInt FreeRam()
    {
    TInt freeRam = 0;
    HAL::Get(HALData::EMemoryRAMFree, freeRam);
    return freeRam;
    }

void CImageLoader::TrimFor(TInt aBytes)
    {
    TInt freeRam = FreeRam();
    TInt dropped = 0;
    while (freeRam - aBytes < KRamReserve && iEntries.Count() > 1)     // (the newest stays: no re-download loop)
        {
        TInt oldest = 0;
        for (TInt i = 1; i < iEntries.Count(); i++)
            if (iEntries[i]->iLastUse < iEntries[oldest]->iLastUse) oldest = i;
        CEntry* e = iEntries[oldest];
        if (e->iBitmap)
            {
            TSize s = e->iBitmap->SizeInPixels();
            iBytes -= s.iWidth * s.iHeight * 2;
            freeRam += s.iWidth * s.iHeight * 2;
            dropped++;
            }
        iEntries.Remove(oldest);
        delete e;
        }
    if (dropped) IgLogF2(_L("images: %d dropped, %d KB kept"), dropped, iBytes / 1024);
    }

TBool CImageLoader::Failed(const char* aUrl)
    {
    if (!aUrl) return ETrue;
    TPtrC8 url((const TUint8*)aUrl);
    CEntry* e = Find(url);
    return e && !e->iBitmap;
    }

void CImageLoader::BeginPass()
    {
    iGen++;
    }

void CImageLoader::EndPass()
    {
    for (TInt i = iQueue.Count() - 1; i >= 0; i--)
        if (iQueue[i]->iGen != iGen) { delete iQueue[i]; iQueue.Remove(i); }
    }

CFbsBitmap* CImageLoader::Get(const char* aUrl, TInt aPriority)
    {
    if (!aUrl || !aUrl[0]) return NULL;
    TPtrC8 url((const TUint8*)aUrl);
    if (url.Length() > 2000) return NULL;
    CEntry* e = Find(url);
    if (e)
        {
        e->iLastUse = ++iUse;
        return e->iBitmap;
        }
    if (!iEnabled) return NULL;
    if (iCurrent && *iCurrent->iUrl == url) return NULL;
    TInt i;
    for (i = 0; i < iQueue.Count(); i++)
        if (*iQueue[i]->iUrl == url)
            {
            CRequest* q = iQueue[i];
            q->iGen = iGen;
            if (aPriority < q->iPriority)
                {
                iQueue.Remove(i);
                q->iPriority = aPriority;
                TInt at;
                for (at = 0; at < iQueue.Count() && iQueue[at]->iPriority <= aPriority; at++) {}
                if (iQueue.Insert(q, at) != KErrNone) delete q;
                }
            return NULL;
            }
    CRequest* r = new CRequest;
    if (!r) return NULL;
    r->iUrl = url.Alloc();
    r->iPriority = aPriority;
    r->iGen = iGen;
    r->iRedirects = 0;
    TInt at;
    for (at = 0; at < iQueue.Count() && iQueue[at]->iPriority <= aPriority; at++) {}
    if (!r->iUrl || iQueue.Insert(r, at) != KErrNone) { delete r; return NULL; }
    Kick();
    return NULL;
    }

void CImageLoader::ClearAll()
    {
    iQueue.ResetAndDestroy();
    if (IsActive()) Cancel();
    delete iDecoder; iDecoder = NULL;
    delete iDecoded; iDecoded = NULL;
    delete iData; iData = NULL;
    if (iCurrent)
        {
        if (iHttp->Busy()) iHttp->Abort();
        delete iCurrent;
        iCurrent = NULL;
        }
    iEntries.ResetAndDestroy();
    iBytes = 0;
    }

void CImageLoader::Kick()
    {
    if (iCurrent || !iQueue.Count() || !iKicker) return;
    if (!iKicker->IsActive()) iKicker->CallBack();
    }

TInt CImageLoader::KickCallback(TAny* aSelf)
    {
    CImageLoader* self = (CImageLoader*)aSelf;
    if (self->iCurrent || !self->iQueue.Count()) return 0;
    TRAPD(err, self->StartNextL());
    if (err != KErrNone) self->Finish(NULL);
    return 0;
    }

void CImageLoader::StartNextL()
    {
    iCurrent = iQueue[0];
    iQueue.Remove(0);
    FetchL(*iCurrent->iUrl);
    }

void CImageLoader::FetchL(const TDesC8& aUrl)
    {
    HBufC8* z = HBufC8::NewLC(aUrl.Length() + 1);
    z->Des().Copy(aUrl);
    z->Des().ZeroTerminate();
    char host[128];
    int port, secure;
    const char* path;
    unsigned int pathLen;
    if (!bw_url_split((const char*)z->Ptr(), host, sizeof(host), &port, &secure, &path, &pathLen)) User::Leave(KErrArgument);
    iHostBuf.Copy(TPtrC8((const TUint8*)host));
    HBufC8* h = HBufC8::NewLC(3500);
    TPtr8 hp = h->Des();
    hp.Append(_L8("User-Agent: "));
    hp.Append(KBrUserAgent);
    hp.Append(_L8("\r\nAccept: image/jpeg,image/png,image/gif,image/*;q=0.5\r\n"));
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
    iHttp->RequestL(iHostBuf, _L8("GET"), TPtrC8((const TUint8*)path, pathLen), hp, KNullDesC8, *this, secure != 0, port);
    CleanupStack::PopAndDestroy(3);
    }

void CImageLoader::HttpProgress(const TDesC& /*aText*/)
    {
    }

void CImageLoader::HttpDone(TInt aError, TInt aHttpStatus, const TDesC8& aHeaders, const TDesC8& aBody)
    {
    if (!iCurrent) return;
    if (aError == KErrNone && aHttpStatus >= 301 && aHttpStatus <= 308 && iCurrent->iRedirects < KMaxImageRedirects)
        {
        char* loc = ig_http_header_dup((const char*)aHeaders.Ptr(), aHeaders.Length(), "Location", 0);
        if (loc)
            {
            HBufC8* z = HBufC8::New(iCurrent->iUrl->Length() + 1);
            char* next = NULL;
            if (z)
                {
                z->Des().Copy(*iCurrent->iUrl);
                z->Des().ZeroTerminate();
                next = bw_url_resolve((const char*)z->Ptr(), loc, ig_strlen(loc));
                delete z;
                }
            ig_free(loc);
            if (next)
                {
                iCurrent->iRedirects++;
                TPtrC8 n((const TUint8*)next);
                TRAPD(err, FetchL(n));
                ig_free(next);
                if (err == KErrNone) return;
                }
            }
        }
    if (aError != KErrNone || aHttpStatus != 200 || aBody.Length() == 0 || aBody.Length() > KMaxImageFile)
        {
        IgLogF2(_L("image failed: err %d HTTP %d"), aError, aHttpStatus);
        Finish(NULL);
        return;
        }
    delete iData;
    iData = aBody.Alloc();
    if (!iData) { Finish(NULL); return; }
    TRAPD(err, DecodeL());
    if (err != KErrNone)
        {
        IgLogF(_L("image decode error %d"), err);
        Finish(NULL);
        }
    }

// The ICL reduction (1/2..1/8) that still covers the size the picture gets on the page.
void CImageLoader::DecodeL()
    {
    delete iDecoder; iDecoder = NULL;
    delete iDecoded; iDecoded = NULL;
    iDecoder = CImageDecoder::DataNewL(iFs, *iData, CImageDecoder::EOptionNoDither);
    const TFrameInfo& info = iDecoder->FrameInfo(0);
    TSize full = info.iOverallSizeInPixels;
    if (full.iWidth <= 0 || full.iHeight <= 0) User::Leave(KErrCorrupt);
    iFullNow = full;
    TSize fit(full);
    if (fit.iWidth > iMax.iWidth) { fit.iHeight = Max(1, fit.iHeight * iMax.iWidth / fit.iWidth); fit.iWidth = iMax.iWidth; }
    if (fit.iHeight > iMax.iHeight) { fit.iWidth = Max(1, fit.iWidth * iMax.iHeight / fit.iHeight); fit.iHeight = iMax.iHeight; }
    TInt shift = 0;
    while (shift < 3)
        {
        TInt w = (full.iWidth + (2 << shift) - 1) >> (shift + 1);
        TInt h = (full.iHeight + (2 << shift) - 1) >> (shift + 1);
        if (w < fit.iWidth || h < fit.iHeight) break;
        shift++;
        }
    TSize reduced((full.iWidth + (1 << shift) - 1) >> shift, (full.iHeight + (1 << shift) - 1) >> shift);
    TrimFor(reduced.iWidth * reduced.iHeight * 3 + fit.iWidth * fit.iHeight * 2 + 512 * 1024);
    iDecoded = new (ELeave) CFbsBitmap;
    User::LeaveIfError(iDecoded->Create(reduced, EColor64K));
    // transparent GIF/PNG: the pixels under the mask are undefined; paint them white first
    iDecoded->LockHeap();
    {
    TUint16* p = (TUint16*)iDecoded->DataAddress();
    TInt words = CFbsBitmap::ScanLineLength(reduced.iWidth, EColor64K) / 2 * reduced.iHeight;
    for (TInt i = 0; i < words; i++) p[i] = 0xFFFF;
    }
    iDecoded->UnlockHeap();
    iDecoder->Convert(&iStatus, *iDecoded, 0);
    SetActive();
    }

void CImageLoader::RunL()
    {
    if (iStatus.Int() != KErrNone || !iCurrent)
        {
        IgLogF(_L("image convert %d"), iStatus.Int());
        Finish(NULL);
        return;
        }
    TSize s = iDecoded->SizeInPixels();
    TSize fit(iFullNow);
    if (fit.iWidth > iMax.iWidth) { fit.iHeight = Max(1, fit.iHeight * iMax.iWidth / fit.iWidth); fit.iWidth = iMax.iWidth; }
    if (fit.iHeight > iMax.iHeight) { fit.iWidth = Max(1, fit.iWidth * iMax.iHeight / fit.iHeight); fit.iHeight = iMax.iHeight; }
    CFbsBitmap* result = NULL;
    if (fit == s)
        {
        result = iDecoded;
        iDecoded = NULL;
        }
    else
        {
        TRAPD(err, result = ScaleL(*iDecoded, fit));
        if (err != KErrNone) result = NULL;
        }
    Finish(result);
    }

TInt CImageLoader::RunError(TInt /*aError*/)
    {
    Finish(NULL);
    return KErrNone;
    }

void CImageLoader::DoCancel()
    {
    if (iDecoder) iDecoder->Cancel();
    }

void CImageLoader::Finish(CFbsBitmap* aBitmap)
    {
    delete iDecoder; iDecoder = NULL;
    delete iDecoded; iDecoded = NULL;
    delete iData; iData = NULL;
    if (iCurrent)
        {
        TBool ok = aBitmap != NULL;
        TSize full = ok ? iFullNow : TSize(0, 0);
        CEntry* e = new CEntry;
        HBufC8* url = iCurrent->iUrl;
        iCurrent->iUrl = NULL;
        delete iCurrent;
        iCurrent = NULL;
        if (e)
            {
            e->iUrl = url;
            e->iBitmap = aBitmap;
            e->iLastUse = ++iUse;
            e->iFull = full;
            if (aBitmap) { TSize s = aBitmap->SizeInPixels(); iBytes += s.iWidth * s.iHeight * 2; }
            if (iEntries.Append(e) != KErrNone) { delete e; url = NULL; }
            else
                {
                iObserver.ImageArrived(*url, ok, full);
                TrimFor(0);         // (after: the entry and its URL may go when RAM is short)
                }
            }
        else
            {
            delete aBitmap;
            delete url;
            }
        }
    else delete aBitmap;
    TBuf<40> t;
    if (iQueue.Count()) t.Format(_L("%d pictures to load"), iQueue.Count());
    iObserver.ImageProgress(t);
    Kick();
    }

// Box-filter scale (EColor64K) to exactly aTarget.
CFbsBitmap* CImageLoader::ScaleL(CFbsBitmap& aSrc, const TSize& aTarget)
    {
    TSize s = aSrc.SizeInPixels();
    TInt dw = aTarget.iWidth, dh = aTarget.iHeight;
    if (s.iWidth <= 0 || s.iHeight <= 0 || dw <= 0 || dh <= 0) User::Leave(KErrCorrupt);
    if (dw > s.iWidth) dw = s.iWidth;
    if (dh > s.iHeight) dh = s.iHeight;
    CFbsBitmap* dst = new (ELeave) CFbsBitmap;
    CleanupStack::PushL(dst);
    User::LeaveIfError(dst->Create(TSize(dw, dh), EColor64K));
    TInt srcStride = CFbsBitmap::ScanLineLength(s.iWidth, EColor64K) / 2;
    TInt dstStride = CFbsBitmap::ScanLineLength(dw, EColor64K) / 2;
    aSrc.LockHeap();
    const TUint16* sp = (const TUint16*)aSrc.DataAddress();
    TUint16* dp = (TUint16*)dst->DataAddress();
    for (TInt y = 0; y < dh; y++)
        {
        TInt y0 = y * s.iHeight / dh;
        TInt y1 = (y + 1) * s.iHeight / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (TInt x = 0; x < dw; x++)
            {
            TInt x0 = x * s.iWidth / dw;
            TInt x1 = (x + 1) * s.iWidth / dw;
            if (x1 <= x0) x1 = x0 + 1;
            TUint r = 0, g = 0, b = 0, n = 0;
            for (TInt yy = y0; yy < y1; yy++)
                {
                const TUint16* row = sp + yy * srcStride;
                for (TInt xx = x0; xx < x1; xx++)
                    {
                    TUint16 p = row[xx];
                    r += (p >> 11) & 31;
                    g += (p >> 5) & 63;
                    b += p & 31;
                    n++;
                    }
                }
            dp[y * dstStride + x] = (TUint16)(((r / n) << 11) | ((g / n) << 5) | (b / n));
            }
        }
    aSrc.UnlockHeap();
    CleanupStack::Pop(dst);
    return dst;
    }
