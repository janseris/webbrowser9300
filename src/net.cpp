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
    User::LeaveIfError(iSs.Connect());
    iSsOpen = ETrue;
    }

CHttpClient::~CHttpClient()
    {
    Cancel();
    CloseSocket();
    CloseConnection();
    if (iSsOpen) iSs.Close();
    if (iSinkOpen) iSink.Close();
    delete iSinkBuf;
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

void CHttpClient::RequestL(const TDesC8& aHost, const TDesC8& aMethod, const TDesC8& aPath,
                           const TDesC8& aHeaders, const TDesC8& aBody, MHttpObserver& aObserver,
                           TBool aSecure, TInt aPort)
    {
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
    iFramed = EFalse;
    iReused = EFalse;
    iLastProgressKb = 0;
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
        TInt want = freeRam - 3 * 1024 * 1024;
        if (want > 12 * 1024 * 1024) want = 12 * 1024 * 1024;
        if (want < 32768) want = 32768;
        while (want >= 32768 && !(iSinkBuf = HBufC8::New(want))) want /= 2;
        IgLogF2(_L("file sink: %d KB free RAM, %d KB buffer"), freeRam / 1024, iSinkBuf ? want / 1024 : 0);
        if (!iSinkBuf) return iSink.Write(aData) == KErrNone;
        }
    TPtr8 b = iSinkBuf->Des();
    if (b.Length() + aData.Length() > b.MaxLength())
        {
        if (!SinkFlush()) return EFalse;
        if (aData.Length() > b.MaxLength()) return iSink.Write(aData) == KErrNone;
        }
    b.Append(aData);
    return ETrue;
    }

TBool CHttpClient::SinkFlush()
    {
    if (!iSinkBuf || !iSinkBuf->Length()) return ETrue;
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
    }

void CHttpClient::Abort()
    {
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

void CHttpClient::ConnectL()
    {
    CloseSocket();
    User::LeaveIfError(iSocket.Open(iSs, KAfInet, KSockStream, KProtocolInetTcp, iConn));
    iSocketOpen = ETrue;
    iSocket.SetOpt(KSoTcpRecvWinSize, KSolInetTcp, 49152);    // the 8 KB default caps the link; before Connect
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
            ResolveL();
            break;

        case EResolving:
            if (err != KErrNone)
                {
                iLastError.Format(_L("Name lookup failed (%d)"), err);
                FinishL(err);
                return;
                }
            iAddr = TInetAddr::Cast(iNameEntry().iAddr);
            iCachedHost = iHost;
            iCachedAddr = iAddr;
            ConnectL();
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
                        // a file sink: the body so far goes to the file, the memory keeps the headers
                        if (iSinkName.Length() && !iInfo.chunked && iInfo.status == 200)
                            {
                            if (!iSinkFsOpen) { if (iSinkFs.Connect() == KErrNone) iSinkFsOpen = ETrue; }
                            if (iSinkFsOpen)
                                {
                                iSinkFs.MkDirAll(iSinkName);
                                if (iSink.Replace(iSinkFs, iSinkName, EFileWrite | EFileShareAny) == KErrNone)
                                    {
                                    iSinkOpen = ETrue;
                                    TPtrC8 rest(iResponse->Ptr() + iInfo.headerLen, iRespLen - iInfo.headerLen);
                                    if (rest.Length()) { SinkWrite(rest); iSinkBytes = rest.Length(); }
                                    iResponse->Des().SetLength(iInfo.headerLen);
                                    iRespLen = iInfo.headerLen;
                                    }
                                }
                            }
                        }
                    }
                else if (iSinkOpen)
                    {
                    // (AppendResponse put this chunk in memory: move it to the file)
                    TPtrC8 rest(iResponse->Ptr() + iInfo.headerLen, iRespLen - iInfo.headerLen);
                    if (!SinkWrite(rest))
                        {
                        iLastError.Copy(_L("Cannot write the file"));
                        FinishL(KErrDiskFull);
                        return;
                        }
                    iSinkBytes += rest.Length();
                    iResponse->Des().SetLength(iInfo.headerLen);
                    iRespLen = iInfo.headerLen;
                    }
                TBool complete;
                if (iSinkOpen) complete = iInfo.contentLength >= 0 && iSinkBytes >= iInfo.contentLength;
                else complete = iHeadersDone && ig_http_body_complete((const char*)iResponse->Ptr(), iRespLen, &iInfo);
                if (complete)
                    {
                    iFramed = ETrue;
                    FinishL(KErrNone);
                    return;
                    }
                TInt got = iSinkOpen ? iSinkBytes : iRespLen;
                if (got / 2048 != iLastProgressKb)
                    {
                    // "12 / 40 KB, 30%" (the body when its length is known), then the speed
                    TInt body = iSinkOpen ? iSinkBytes : (iHeadersDone ? iRespLen - (TInt)iInfo.headerLen : 0);
                    TBuf<60> t;
                    if (iHeadersDone && iInfo.contentLength > 0)
                        t.Format(_L("%d / %d KB, %d%%"), body / 1024, iInfo.contentLength / 1024,
                                 (TInt)((TInt64(body) * TInt64(100) / TInt64(iInfo.contentLength)).GetTInt()));
                    else t.Format(_L("%d KB"), got / 1024);
                    TTime now; now.UniversalTime();
                    TInt ms = (now.MicroSecondsFrom(iRequestStart).Int64() / TInt64(1000)).GetTInt();
                    if (ms > 500) { t.Append(_L(", ")); t.AppendNum(got / ms); t.Append(_L(" KB/s")); }
                    iLastProgressKb = got / 2048;
                    if (iObserver) iObserver->HttpProgress(t);
                    }
                ReceiveNext();
                }
            else if (err == KErrEof || err == KErrDisconnected || err == KErrNone)
                {
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
