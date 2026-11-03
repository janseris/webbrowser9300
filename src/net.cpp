// Browser 9300 - one HTTP(S) request at a time: RConnection (attach to the running connection,
// else start the default one), DNS, TCP, TLS via CSecureSocket (ssladaptor.dll), HTTP/1.1 with
// keep-alive (one connection is reused for the next request to the same host), chunked and gzip
// bodies. From Instagram 9300's client (net.cpp 0.57), with the video side file removed.
#include <hal.h>

#include <es_enum.h>
#include <commdbconnpref.h>
#include <ezlib.h>
#include "browser.h"

const TInt KMaxResponse = 4 * 1024 * 1024;
// the body read into RAM while the download question is up (over Bluetooth ~35 KB a second)
const TInt KPauseRam = 512 * 1024;
// the progress text at most this often (each redraw is the CPU the Bluetooth stack runs on)
const TInt KProgressMs = 300;
_LIT(KTlsProtocol, "TLS1.0");     // the protocol name CSecureSocket looks up; the adaptor speaks TLS 1.2

CHttpClient* CHttpClient::NewL()
    {
    CHttpClient* self = new (ELeave) CHttpClient;
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop(self);
    return self;
    }

CHttpClient::CHttpClient() : CActive(CActive::EPriorityStandard)
    {
    CActiveScheduler::Add(this);
    }

void CHttpClient::ConstructL()
    {
    // (the socket server session is opened on the first request: four clients connecting at
    //  start cost time, and may even start ESOCK, for a start page that needs no network)
    }

CHttpClient::~CHttpClient()
    {
    Cancel();
    if (iRetryTimerOpen) iRetryTimer.Close();
    CloseSocket();
    CloseConnection();
    if (iSsOpen) iSs.Close();
    if (iSinkOpen) iSink.Close();
    delete iSinkBuf;
    if (iZs) { if (iGzState >= 1) inflateEnd(iZs); User::Free(iZs); }
    delete iGzHead;
    delete iStage;
    if (iSinkFsOpen) iSinkFs.Close();
    delete iRequest;
    delete iResponse;
    }

void CHttpClient::Progress(const TDesC& aText)
    {
    if (!iQuiet) IgLog(aText);
    if (iObserver) iObserver->HttpProgress(aText);
    }

// ------------------------------------------------------------------ request

void CHttpClient::EnsureSessionL()
    {
    if (iSsOpen) return;
    User::LeaveIfError(iSs.Connect());
    iSsOpen = ETrue;
    }

void CHttpClient::RequestL(const TDesC8& aHost, const TDesC8& aMethod, const TDesC8& aPath,
                           const TDesC8& aHeaders, const TDesC8& aBody, MHttpObserver& aObserver,
                           TBool aSecure, TInt aPort)
    {
    iLookupTries = 0;
    EnsureSessionL();
    if (iState != EIdle) User::Leave(KErrInUse);
    iRequestStart.UniversalTime();
    iSecure = aSecure;
    iPort = aPort;
    if (aHost.Length() > iHost.MaxLength()) User::Leave(KErrArgument);
    iObserver = &aObserver;
    iHost = aHost;
    iLastError.Zero();
    if (IgLogEnabled())
        {
        HBufC8* l = HBufC8::New(300);
        if (l)
            {
            TPtr8 lp = l->Des();
            lp.Append(_L8("HTTP ")); lp.Append(aMethod); lp.Append(' '); lp.Append(aHost); lp.Append(aPath.Left(200));
            lp.Append(_L8(" body ")); lp.AppendNum(aBody.Length());
            if (!aSecure) lp.Append(_L8(" plain"));
            IgLog8(lp);
            delete l;
            }
        }

    // the request text
    delete iRequest;
    iRequest = NULL;
    HBufC8* req = HBufC8::NewL(aMethod.Length() + aPath.Length() + aHeaders.Length() + aBody.Length() + 160);
    TPtr8 p = req->Des();
    p.Append(aMethod);
    p.Append(' ');
    p.Append(aPath);
    p.Append(_L8(" HTTP/1.1\r\nHost: "));
    p.Append(aHost);
    p.Append(_L8("\r\nConnection: keep-alive\r\n"));
    p.Append(aHeaders);
    if (aBody.Length() || aMethod == _L8("POST"))
        {
        p.Append(_L8("Content-Length: "));
        p.AppendNum(aBody.Length());
        p.Append(_L8("\r\n"));
        }
    p.Append(_L8("\r\n"));
    p.Append(aBody);
    iRequest = req;

    delete iResponse;
    iResponse = NULL;
    iResponse = HBufC8::NewL(16384);
    iRespLen = 0;
    iHeadersDone = EFalse;
    iSinkBytes = 0;
    iSinkUsed = EFalse;
    iPaused = EFalse;
    iPauseReading = EFalse;
    iPauseDone = EFalse;
    iFramed = EFalse;
    iReused = EFalse;
    iLastProgressKb = 0;
    iLastProgress = TTime(TInt64(0));
    if (iSinkOpen) CloseSink();
    ig_memset(&iInfo, 0, sizeof(iInfo));

    if (iKeepAlive && iKeepHost == iHost && iKeepSecure == iSecure && iKeepPort == iPort && (iTls || iSocketOpen))
        {
        iReused = ETrue;
        iKeepAlive = EFalse;
        Progress(_L("Reusing the connection"));
        SendL();
        return;
        }
    CloseSocket();
    if (iConnStarted) ResolveL();
    else StartConnectionL();
    }

// Uses the connection that is already up (USB passthrough / GnuBox BT), otherwise starts the
// phone's default one (no prompt, unless asked for).
void CHttpClient::StartConnectionL()
    {
    if (!iConnOpen)
        {
        User::LeaveIfError(iConn.Open(iSs));
        iConnOpen = ETrue;
        }
    if (!iPrompt)
        {
        TUint count = 0;
        if (iConn.EnumerateConnections(count) == KErrNone && count > 0)
            {
            TConnectionInfoBuf info;
            if (iConn.GetConnectionInfo(1, info) == KErrNone &&
                iConn.Attach(info, RConnection::EAttachTypeNormal) == KErrNone)
                {
                iConnStarted = ETrue;
                DetectBearer();
                Progress(_L("Using the open connection"));
                ResolveL();
                return;
                }
            // a failed Attach leaves the handle unusable: reopen it
            iConn.Close();
            User::LeaveIfError(iConn.Open(iSs));
            }
        }
    Progress(_L("Connecting..."));
    iState = EStartingConnection;
    if (iPrompt)
        {
        TCommDbConnPref pref;
        pref.SetDialogPreference(ECommDbDialogPrefPrompt);
        pref.SetDirection(ECommDbConnectionDirectionOutgoing);
        iConn.Start(pref, iStatus);
        }
    else
        {
        iConn.Start(iStatus);
        }
    SetActive();
    }

// the file sink: received data is collected in RAM and written in 32 KB pieces (a write on
// the 9300's flash costs ~15 ms whatever its size, and the TLS chunks are 2 KB)
// The buffer takes what RAM there is: the free system RAM minus a 3 MB margin, at most 12 MB,
// halved until the allocation succeeds. The file is written when the buffer is full, at the end,
// or - while MP4 Player is playing the growing file - every SinkFlushEvery() bytes.
TBool CHttpClient::SinkWrite(const TDesC8& aData)
    {
    if (!iSinkBuf)
        {
        TInt freeRam = 0;
        HAL::Get(HALData::EMemoryRAMFree, freeRam);
        TInt want = freeRam - 2560 * 1024;
        if (want > 24 * 1024 * 1024) want = 24 * 1024 * 1024;
        if (want < 32768) want = 32768;
        while (want >= 32768 && !(iSinkBuf = HBufC8::New(want))) want /= 2;
        IgLogF2(_L("file sink: %d KB free RAM, %d KB buffer"), freeRam / 1024, iSinkBuf ? want / 1024 : 0);
        if (!iSinkBuf) return iSink.Write(aData) == KErrNone;
        }
    if (iSinkBuf->Length() + aData.Length() > iSinkBuf->Des().MaxLength())
        {
        if (!SinkFlush()) return EFalse;
        if (aData.Length() > iSinkBuf->Des().MaxLength()) return iSink.Write(aData) == KErrNone;
        }
    // (a TPtr8 taken before SinkFlush keeps the old length: the Append after a flush overflowed,
    //  USER 23. The pointer is taken here, after any flush.)
    iSinkBuf->Des().Append(aData);
    return ETrue;
    }

TBool CHttpClient::SinkFlush()
    {
    if (!iSinkBuf || !iSinkBuf->Length()) return ETrue;
    // a big write takes a while on the flash (the transfer looked stuck near its end): say so first
    if (iSinkBuf->Length() >= 16 * 1024 && iObserver)
        {
        TBuf<60> t;
        t.Format(_L("Writing the file (%d KB)..."), iSinkBuf->Length() / 1024);
        iObserver->HttpStep(t);
        }
    TInt err = iSink.Write(*iSinkBuf);
    iSinkBuf->Des().Zero();
    return err == KErrNone;
    }

void CHttpClient::CloseSink()
    {
    SinkFlush();
    iSink.Close();
    iSinkOpen = EFalse;
    delete iSinkBuf; iSinkBuf = NULL;           // the RAM goes back after each download
    if (iZs) { if (iGzState >= 1) inflateEnd(iZs); User::Free(iZs); iZs = NULL; }
    delete iGzHead; iGzHead = NULL;
    delete iStage; iStage = NULL;
    iGzState = 0;
    }

// all of the body: the last chunk, or Content-Length bytes (the raw ones)
TBool CHttpClient::SinkDone() const
    {
    if (iSinkChunked) return iDechunk.done;
    return iInfo.contentLength >= 0 && iSinkBytes >= iInfo.contentLength;
    }

TBool CHttpClient::SinkFeed(const TDesC8& aData)
    {
    TPtrC8 data(aData);
    if (iSinkChunked)
        {
        if (iDechunk.done) return ETrue;
        if (!iStage || iStage->Des().MaxLength() < data.Length())
            {
            delete iStage;
            iStage = HBufC8::New(Max(data.Length(), 16384));
            if (!iStage) return EFalse;
            }
        TPtr8 st = iStage->Des();
        TUint n = bw_dechunk(&iDechunk, data.Ptr(), data.Length(), (unsigned char*)st.Ptr());
        if (iDechunk.error) return EFalse;
        st.SetLength(n);
        data.Set(st);
        }
    if (!data.Length()) return ETrue;
    if (iSinkGzip) return GunzipFeed(data);
    return SinkWrite(data);
    }

// gzip on the way: the header (it may come in pieces), then raw inflate, 32 KB at a time to the file
TBool CHttpClient::GunzipFeed(const TDesC8& aData)
    {
    if (iGzState == 2) return ETrue;            // (the 8-byte trailer and anything after)
    TPtrC8 in(aData);
    if (iGzState == 0)
        {
        TInt have = iGzHead ? iGzHead->Length() : 0;
        if (have + in.Length() > 65536) return EFalse;
        HBufC8* h = iGzHead ? iGzHead->ReAlloc(have + in.Length()) : HBufC8::New(in.Length());
        if (!h) return EFalse;
        iGzHead = h;
        iGzHead->Des().Append(in);
        TInt hl = bw_gzip_header_len(iGzHead->Ptr(), iGzHead->Length());
        if (hl < 0)
            {
            // not gzip after all: as it is
            iSinkGzip = EFalse;
            TBool ok = SinkWrite(*iGzHead);
            delete iGzHead; iGzHead = NULL;
            return ok;
            }
        if (hl == 0) return ETrue;              // more of the header to come
        iZs = (z_stream*)User::Alloc(sizeof(z_stream));
        if (!iZs) return EFalse;
        Mem::FillZ(iZs, sizeof(z_stream));
        if (inflateInit2(iZs, -MAX_WBITS) != Z_OK) { User::Free(iZs); iZs = NULL; return EFalse; }
        iGzState = 1;
        // the rest after the header, then the header buffer goes
        HBufC8* head = iGzHead;
        iGzHead = NULL;
        TBool ok = GunzipFeed(head->Mid(hl));
        delete head;
        return ok;
        }
    const TInt KOut = 32768;
    HBufC8* out = HBufC8::New(KOut);            // (iStage may hold the input: a buffer of its own)
    if (!out) return EFalse;
    iZs->next_in = (Bytef*)in.Ptr();
    iZs->avail_in = in.Length();
    TBool ok = ETrue;
    for (;;)
        {
        iZs->next_out = (Bytef*)out->Ptr();
        iZs->avail_out = KOut;
        TInt r = inflate(iZs, Z_NO_FLUSH);
        TInt produced = KOut - iZs->avail_out;
        if (produced > 0) { TPtrC8 o(out->Ptr(), produced); if (!SinkWrite(o)) { ok = EFalse; break; } }
        if (r == Z_STREAM_END) { iGzState = 2; break; }
        if (r != Z_OK && r != Z_BUF_ERROR) { ok = EFalse; break; }
        if (iZs->avail_in == 0 && iZs->avail_out != 0) break;     // all of this piece is in
        if (r == Z_BUF_ERROR && produced == 0) break;
        }
    delete out;
    return ok;
    }

// a file sink: the body so far goes to the file, the memory keeps the headers
void CHttpClient::TryOpenSink()
    {
    if (!iSinkName.Length() || iInfo.status != 200) return;
    if (!iSinkFsOpen) { if (iSinkFs.Connect() == KErrNone) iSinkFsOpen = ETrue; }
    if (!iSinkFsOpen) return;
    iSinkFs.MkDirAll(iSinkName);
    if (iSink.Replace(iSinkFs, iSinkName, EFileWrite | EFileShareAny) != KErrNone) return;
    iSinkOpen = ETrue;
    iSinkUsed = ETrue;
    // chunked and gzip replies are decoded on the way (a file bigger than the RAM still fits)
    iSinkChunked = iInfo.chunked;
    iSinkGzip = iInfo.gzip;
    bw_dechunk_init(&iDechunk);
    iGzState = 0;
    TPtrC8 rest(iResponse->Ptr() + iInfo.headerLen, iRespLen - iInfo.headerLen);
    if (rest.Length()) { SinkFeed(rest); iSinkBytes = rest.Length(); }
    iResponse->Des().SetLength(iInfo.headerLen);
    iRespLen = iInfo.headerLen;
    }

void CHttpClient::ResumeL(const TDesC& aFile)
    {
    if (!iPaused) return;
    iPaused = EFalse;
    iSinkName.Copy(aFile);
    IgLogF2(_L("download: %d KB came while the question was up (all of it: %d)"), (iRespLen - (TInt)iInfo.headerLen) / 1024,
            iPauseDone ? 1 : 0);
    TryOpenSink();                         // (what came meanwhile goes to the file first)
    if (iPauseReading)
        {
        iPauseReading = EFalse;
        if (iPauseDone) { iPauseDone = EFalse; iFramed = iSinkOpen ? SinkDone() : ETrue; FinishL(KErrNone); return; }
        if (IsActive()) return;            // a read is out: RunL goes on into the file
        }
    ReceivedSoFarL();
    }

// what arrived with the headers may be all of it
void CHttpClient::ReceivedSoFarL()
    {
    TBool complete;
    if (iSinkOpen) complete = SinkDone();
    else complete = ig_http_body_complete((const char*)iResponse->Ptr(), iRespLen, &iInfo);
    if (complete) { iFramed = ETrue; FinishL(KErrNone); return; }
    ReceiveNext();
    }

void CHttpClient::Abort()
    {
    iPaused = EFalse;
    Cancel();
    CloseSocket();
    if (iSinkOpen) CloseSink();
    iSinkName.Zero();
    iObserver = NULL;
    }

void CHttpClient::CloseConnection()
    {
    if (iResolverOpen) { iResolver.Close(); iResolverOpen = EFalse; }
    if (iConnOpen) { iConn.Close(); iConnOpen = EFalse; }
    iConnStarted = EFalse;
    iCachedHost.Zero();
    }

void CHttpClient::ResolveL()
    {
    if (iCachedHost.Length() && iCachedHost == iHost)
        {
        iAddr = iCachedAddr;
        ConnectL();
        return;
        }
    if (!iResolverOpen)
        {
        User::LeaveIfError(iResolver.Open(iSs, KAfInet, KProtocolInetUdp, iConn));
        iResolverOpen = ETrue;
        }
    iHost16.Copy(iHost);
    Progress(_L("Looking up the server..."));
    iState = EResolving;
    iResolver.GetByName(iHost16, iNameEntry, iStatus);
    SetActive();
    }

// The access point's bearer (as Maps 9300 finds it): Bluetooth dial-up is a modem bearer on the
// BTCOMM port. There the TCP window is 16 KB: GnuBox's measurements (test log #19) gave the same
// ~34 kB/s with 8-64 KB windows, and a bigger one only queued data in front of the link (round
// trip 358 ms with 8 KB, 624 ms with 64 KB): the page's other requests waited behind it. USB
// (a LAN bearer) and the rest keep 48 KB (220 kB/s needs more in flight).
void CHttpClient::DetectBearer()
    {
    TInt win = 49152;
    TBuf<40> bearer, port;
    if (iConn.GetDesSetting(_L("IAP\\IAPBearerType"), bearer) == KErrNone && bearer.Compare(_L("ModemBearer")) == 0 &&
        iConn.GetDesSetting(_L("ModemBearer\\PortName"), port) == KErrNone && port.Left(6).CompareF(_L("BTCOMM")) == 0)
        win = 16384;
    if (win != iRecvWin)
        {
        iRecvWin = win;
        IgLogF2(_L("connection: Bluetooth %d, TCP window %d B"), win == 16384 ? 1 : 0, win);
        }
    }

void CHttpClient::ConnectL()
    {
    CloseSocket();
    User::LeaveIfError(iSocket.Open(iSs, KAfInet, KSockStream, KProtocolInetTcp, iConn));
    iSocketOpen = ETrue;
    // the 8 KB default caps the link; before Connect. 16 KB over Bluetooth (DetectBearer)
    iSocket.SetOpt(KSoTcpRecvWinSize, KSolInetTcp, iRecvWin > 0 ? iRecvWin : 49152);
    iAddr.SetPort(iPort);
    Progress(_L("Connecting to the server..."));
    iState = EConnecting;
    iSocket.Connect(iAddr, iStatus);
    SetActive();
    }

void CHttpClient::HandshakeL()
    {
    TRAPD(err, iTls = CSecureSocket::NewL(iSocket, KTlsProtocol));
    if (err != KErrNone)
        {
        _LIT(KSsl3, "SSL3.0");      // the other name the adaptor is registered under
        iTls = CSecureSocket::NewL(iSocket, KSsl3);
        }
    // from here on the adaptor closes iSocket when iTls is closed (CTlsConnection::Close)
    // the host name: for SNI and so the adaptor does the handshake now (without it, the
    // patched adaptor defers the handshake to the first Send and reads the Host: header)
    iTls->SetOpt(KSoSSLDomainName, KSolInetSSL, iHost);
    iTls->SetDialogMode(EDialogModeUnattended);
    Progress(_L("TLS handshake..."));
    iState = EHandshaking;
    iTls->StartClientHandshake(iStatus);
    SetActive();
    }

void CHttpClient::SendL()
    {
    Progress(_L("Sending the request..."));
    iState = ESending;
    if (iTls) iTls->Send(*iRequest, iStatus);
    else iSocket.Write(*iRequest, iStatus);
    SetActive();
    }

void CHttpClient::ReceiveNext()
    {
    iState = EReceiving;
    iChunk.Zero();
    if (iTls) iTls->RecvOneOrMore(iChunk, iStatus, iXfrLen);
    else iSocket.RecvOneOrMore(iChunk, 0, iStatus, iXfrLen);
    SetActive();
    }

TBool CHttpClient::AppendResponse(const TDesC8& aData)
    {
    if (iRespLen + aData.Length() > KMaxResponse) return EFalse;
    if (iRespLen + aData.Length() > iResponse->Des().MaxLength())
        {
        TInt newMax = iResponse->Des().MaxLength() * 2;
        while (newMax < iRespLen + aData.Length()) newMax *= 2;
        HBufC8* bigger = iResponse->ReAlloc(newMax);
        if (!bigger) return EFalse;
        iResponse = bigger;
        }
    iResponse->Des().Append(aData);
    iRespLen = iResponse->Length();
    return ETrue;
    }

// ------------------------------------------------------------------ active object

void CHttpClient::RunL()
    {
    TInt err = iStatus.Int();
    switch (iState)
        {
        case EStartingConnection:
            if (err != KErrNone)
                {
                iConn.Close();
                iConnOpen = EFalse;
                iLastError.Format(_L("No connection (%d)"), err);
                FinishL(err);
                return;
                }
            iConnStarted = ETrue;
            DetectBearer();
            ResolveL();
            break;

        case EResolving:
            if (err != KErrNone)
                {
                // a lookup just after the link came up (or a lost DNS reply) often fails once:
                // two more tries, 1.5 s apart, on a new resolver
                if (++iLookupTries < 3 && err != KErrCancel)
                    {
                    IgLogF2(_L("name lookup failed (%d), try %d of 3 in 1.5 s"), err, iLookupTries + 1);
                    if (iResolverOpen) { iResolver.Close(); iResolverOpen = EFalse; }
                    if (!iRetryTimerOpen && iRetryTimer.CreateLocal() == KErrNone) iRetryTimerOpen = ETrue;
                    if (iRetryTimerOpen)
                        {
                        Progress(_L("DNS retry"));
                        iState = EResolveWait;
                        iRetryTimer.After(iStatus, 1500000);
                        SetActive();
                        return;
                        }
                    }
                iLastError.Format(_L("Name lookup failed (%d)"), err);
                FinishL(err);
                return;
                }
            iAddr = TInetAddr::Cast(iNameEntry().iAddr);
            iCachedHost = iHost;
            iCachedAddr = iAddr;
            ConnectL();
            break;

        case EResolveWait:
            ResolveL();
            break;

        case EConnecting:
            if (err != KErrNone)
                {
                iCachedHost.Zero();
                iLastError.Format(_L("Connect failed (%d)"), err);
                FinishL(err);
                return;
                }
            if (iSecure) HandshakeL();
            else SendL();
            break;

        case EHandshaking:
            if (err != KErrNone)
                {
                iLastError.Format(_L("TLS handshake failed (%d)"), err);
                FinishL(err);
                return;
                }
            SendL();
            break;

        case ESending:
            if (err != KErrNone)
                {
                if (iReused)
                    {
                    // the kept connection was dead: start a new one
                    iReused = EFalse;
                    CloseSocket();
                    IgLog(_L("kept connection dead on send: reconnecting"));
                    if (iConnStarted) ResolveL(); else StartConnectionL();
                    return;
                    }
                iLastError.Format(_L("Send failed (%d)"), err);
                FinishL(err);
                return;
                }
            Progress(_L("Waiting for the reply..."));
            ReceiveNext();
            break;

        case EReceiving:
            {
            if (err == KErrNone && iChunk.Length())
                {
                if (!AppendResponse(iChunk))
                    {
                    iLastError.Copy(_L("Reply too big"));
                    FinishL(KErrOverflow);
                    return;
                    }
                if (!iHeadersDone)
                    {
                    if (ig_http_parse_head((const char*)iResponse->Ptr(), iRespLen, &iInfo))
                        {
                        iHeadersDone = ETrue;
                        Progress(_L("Receiving..."));
                        // the observer may want to ask first (a file to download): the answer waits,
                        // the body does not (read into RAM meanwhile, up to iPauseMax)
                        if (iObserver && !iObserver->HttpHeadersArrived(TPtrC8(iResponse->Ptr(), iInfo.headerLen), iInfo.status))
                            {
                            iPaused = ETrue;
                            iPauseReading = ETrue;
                            iPauseDone = EFalse;
                            TInt room = BrFreeRam() / 8;
                            iPauseMax = room < KPauseRam ? room : KPauseRam;
                            }
                        else TryOpenSink();
                        }
                    }
                else if (iSinkOpen)
                    {
                    // (AppendResponse put this chunk in memory: move it to the file)
                    TPtrC8 rest(iResponse->Ptr() + iInfo.headerLen, iRespLen - iInfo.headerLen);
                    if (!SinkFeed(rest))
                        {
                        iLastError.Copy(iDechunk.error ? _L("Bad chunked reply") : _L("Cannot write the file"));
                        FinishL(KErrDiskFull);
                        return;
                        }
                    iSinkBytes += rest.Length();
                    iResponse->Des().SetLength(iInfo.headerLen);
                    iRespLen = iInfo.headerLen;
                    }
                TBool complete;
                if (iSinkOpen) complete = SinkDone();
                else complete = iHeadersDone && ig_http_body_complete((const char*)iResponse->Ptr(), iRespLen, &iInfo);
                if (iPaused)
                    {
                    // the question is still up: all of it here, or the RAM's share, and the
                    // reading stops until the answer (ResumeL / Abort)
                    if (complete) { iPauseDone = ETrue; return; }
                    if (!iPauseReading || iRespLen - (TInt)iInfo.headerLen >= iPauseMax) return;
                    ReceiveNext();
                    return;
                    }
                if (complete)
                    {
                    iFramed = ETrue;
                    FinishL(KErrNone);
                    return;
                    }
                TInt got = iSinkOpen ? iSinkBytes : iRespLen;
                TTime now; now.UniversalTime();
                if (got / 2048 != iLastProgressKb && now.MicroSecondsFrom(iLastProgress).Int64() >= TInt64(KProgressMs * 1000))
                    {
                    iLastProgress = now;
                    // "12 / 40 KB" (the body when its length is known), then the speed
                    TInt body = iSinkOpen ? iSinkBytes : (iHeadersDone ? iRespLen - (TInt)iInfo.headerLen : 0);
                    TBuf<60> t;
                    if (iHeadersDone && iInfo.contentLength > 0) t.Format(_L("%d / %d KB"), body / 1024, iInfo.contentLength / 1024);
                    else t.Format(_L("%d KB"), got / 1024);
                    TInt ms = (now.MicroSecondsFrom(iRequestStart).Int64() / TInt64(1000)).GetTInt();
                    if (ms > 500) { t.Append(_L(", ")); t.AppendNum(got / ms); t.Append(_L(" KB/s")); }
                    iLastProgressKb = got / 2048;
                    if (iObserver) iObserver->HttpProgress(t);
                    }
                ReceiveNext();
                }
            else if (err == KErrEof || err == KErrDisconnected || err == KErrNone)
                {
                if (iPaused && iHeadersDone)
                    {
                    // the server closed it while the question is up: finish on the answer
                    iPauseDone = ETrue;
                    return;
                    }
                if (iReused && iRespLen == 0)
                    {
                    // the kept connection had been closed by the server: start a new one
                    iReused = EFalse;
                    CloseSocket();
                    IgLog(_L("kept connection closed: reconnecting"));
                    if (iConnStarted) ResolveL(); else StartConnectionL();
                    return;
                    }
                // the server closed the connection: done if we have at least the headers
                if (iHeadersDone) FinishL(KErrNone);
                else
                    {
                    iLastError.Copy(_L("Connection closed early"));
                    FinishL(iRespLen ? KErrCorrupt : KErrEof);
                    }
                }
            else
                {
                if (iPaused && iHeadersDone) { iPauseDone = ETrue; return; }   // (as a close: on the answer)
                if (iHeadersDone && iRespLen > (TInt)iInfo.headerLen) FinishL(KErrNone);   // partial body
                else
                    {
                    iLastError.Format(_L("Receive failed (%d)"), err);
                    FinishL(err);
                    }
                }
            break;
            }

        default:
            break;
        }
    }

TInt CHttpClient::RunError(TInt aError)
    {
    iLastError.Format(_L("Error %d"), aError);
    TRAPD(e, FinishL(aError));
    (void)e;
    return KErrNone;
    }

void CHttpClient::DoCancel()
    {
    switch (iState)
        {
        case EStartingConnection:
            iConn.Close();          // cancels Start
            iConnOpen = EFalse;
            iConnStarted = EFalse;
            break;
        case EResolving:
            iResolver.Cancel();
            break;
        case EResolveWait:
            iRetryTimer.Cancel();
            break;
        case EConnecting:
            iSocket.CancelConnect();
            break;
        case EHandshaking:
            if (iTls) iTls->CancelHandshake();
            break;
        case ESending:
            if (iTls) iTls->CancelSend(); else iSocket.CancelWrite();
            break;
        case EReceiving:
            if (iTls) iTls->CancelRecv(); else iSocket.CancelRecv();
            break;
        default:
            break;
        }
    iState = EIdle;
    }

void CHttpClient::CloseSocket()
    {
    iKeepAlive = EFalse;
    if (iTls)
        {
        iTls->CancelAll();
        delete iTls;            // Close()s the secure socket, and the adaptor closes the RSocket too
        iTls = NULL;
        iSocketOpen = EFalse;   // so it must not be touched again (KERN-EXEC 0 otherwise)
        }
    if (iSocketOpen)
        {
        iSocket.CancelAll();
        iSocket.Close();
        iSocketOpen = EFalse;
        }
    }

void CHttpClient::FinishL(TInt aError)
    {
    if (IgLogEnabled())
        {
        HBufC* l = HBufC::New(160);
        if (l)
            {
            TPtr lp = l->Des();
            lp.Format(_L("HTTP done err %d status %d bytes %d sink %d chunked %d gzip %d"), aError, iInfo.status, iRespLen, iSinkBytes, iInfo.chunked, iInfo.gzip);
            if (iLastError.Length()) { lp.Append(_L(" : ")); lp.Append(iLastError.Left(40)); }
            IgLog(lp);
            delete l;
            }
        }
    iState = EIdle;
    // keep the connection for the next request if the reply ended cleanly and the server allows it
    if (aError == KErrNone && iFramed && iHeadersDone && !iInfo.closeAfter && (iTls || iSocketOpen))
        {
        iKeepAlive = ETrue;
        iKeepHost = iHost;
        iKeepSecure = iSecure;
        iKeepPort = iPort;
        }
    else CloseSocket();
    if (iSinkOpen) CloseSink();
    iSinkName.Zero();
    MHttpObserver* obs = iObserver;
    iObserver = NULL;
    delete iRequest;
    iRequest = NULL;
    if (aError != KErrNone || !iHeadersDone)
        {
        if (aError == KErrNone) aError = KErrCorrupt;
        if (aError == KErrNotReady || aError == KErrDisconnected || aError == KErrCouldNotConnect)
            CloseConnection();      // the link is gone: start it again next time
        if (obs) obs->HttpDone(aError, 0, KNullDesC8, KNullDesC8);
        return;
        }
    TPtrC8 all(iResponse->Ptr(), iRespLen);
    TPtrC8 headers(all.Left(iInfo.headerLen));
    TPtrC8 rawBody(all.Mid(iInfo.headerLen));
    if (obs && iInfo.gzip && rawBody.Length() > 4096)
        {
        TBuf<40> t;
        t.Format(_L("Unzipping %d KB..."), rawBody.Length() / 1024);
        obs->HttpStep(t);
        }
    HBufC8* body = DecodeBodyL(rawBody, iInfo.chunked, iInfo.gzip);
    CleanupStack::PushL(body);
    IgLogF(_L("HTTP body decoded: %d bytes"), body->Length());
    if (obs) obs->HttpDone(KErrNone, iInfo.status, headers, *body);
    CleanupStack::PopAndDestroy(body);
    }

// De-chunks and un-gzips (zlib 1.1.3: raw inflate after the gzip header). Returns a new buffer
// (not on the cleanup stack).
HBufC8* CHttpClient::DecodeBodyL(const TDesC8& aBody, TBool aChunked, TBool aGzip)
    {
    HBufC8* body = aBody.AllocLC();
    if (aChunked)
        {
        TPtr8 p = body->Des();
        TInt n = (TInt)ig_http_dechunk((char*)p.Ptr(), p.Length());
        p.SetLength(n);
        }
    if (aGzip && body->Length() > 18 && (*body)[0] == 0x1f && (*body)[1] == 0x8b)
        {
        const TUint8* d = body->Ptr();
        TInt len = body->Length();
        TInt flags = d[3];
        TInt pos = 10;
        if (flags & 4) { if (pos + 2 > len) User::Leave(KErrCorrupt); pos += 2 + (d[pos] | (d[pos + 1] << 8)); }
        if (flags & 8) { while (pos < len && d[pos]) pos++; pos++; }
        if (flags & 16) { while (pos < len && d[pos]) pos++; pos++; }
        if (flags & 2) pos += 2;
        if (pos >= len) User::Leave(KErrCorrupt);
        TInt outMax = len * 5 + 4096;
        if (outMax < 65536) outMax = 65536;
        HBufC8* out = HBufC8::NewL(outMax);
        z_stream zs;
        Mem::FillZ(&zs, sizeof(zs));
        zs.next_in = (Bytef*)(d + pos);
        zs.avail_in = len - pos;
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
            {
            delete out;
            User::Leave(KErrGeneral);
            }
        TInt total = 0;
        for (;;)
            {
            zs.next_out = (Bytef*)out->Ptr() + total;
            zs.avail_out = outMax - total;
            TInt r = inflate(&zs, Z_SYNC_FLUSH);
            total = outMax - zs.avail_out;
            if (r == Z_STREAM_END) break;
            if (r == Z_OK && zs.avail_out == 0 || r == Z_BUF_ERROR && zs.avail_out == 0)
                {
                // more room
                if (outMax >= KMaxResponse) break;
                outMax *= 2;
                out->Des().SetLength(total);        // ReAlloc keeps Length() bytes
                HBufC8* bigger = out->ReAlloc(outMax);
                if (!bigger) break;
                out = bigger;
                continue;
                }
            break;      // Z_OK with input exhausted (truncated download), or an error: keep what we have
            }
        inflateEnd(&zs);
        out->Des().SetLength(total);
        CleanupStack::PopAndDestroy(body);
        return out;
        }
    CleanupStack::Pop(body);
    return body;
    }
