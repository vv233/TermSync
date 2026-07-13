#include "transfer_view/WinDragOut.h"

#ifdef _WIN32

#include <QDir>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <shlobj.h>

#include <cstring>
#include <vector>

namespace termsync::ui {

namespace {

// Builds a CF_HDROP HGLOBAL (DROPFILES + double-NUL-terminated wide path list).
HGLOBAL makeHDrop(const QStringList &paths)
{
    std::vector<wchar_t> chars;
    for (const QString &p : paths) {
        const QString native = QDir::toNativeSeparators(p);
        const int len = native.length();
        const wchar_t *w = reinterpret_cast<const wchar_t *>(native.utf16());
        chars.insert(chars.end(), w, w + len);
        chars.push_back(L'\0'); // terminate this path
    }
    chars.push_back(L'\0'); // terminate the list

    const SIZE_T bytes = sizeof(DROPFILES) + chars.size() * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GHND, bytes);
    if (!h)
        return nullptr;
    auto *df = static_cast<DROPFILES *>(GlobalLock(h));
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    std::memcpy(reinterpret_cast<BYTE *>(df) + sizeof(DROPFILES), chars.data(),
                chars.size() * sizeof(wchar_t));
    GlobalUnlock(h);
    return h;
}

HGLOBAL cloneGlobal(HGLOBAL src)
{
    if (!src)
        return nullptr;
    const SIZE_T n = GlobalSize(src);
    HGLOBAL dst = GlobalAlloc(GHND, n);
    if (!dst)
        return nullptr;
    void *s = GlobalLock(src);
    void *d = GlobalLock(dst);
    std::memcpy(d, s, n);
    GlobalUnlock(dst);
    GlobalUnlock(src);
    return dst;
}

// A minimal IDataObject exposing CF_HDROP with deferred rendering: the files are
// produced by `m_provider` the first time the drop target reads the data.
class DataObject : public IDataObject
{
public:
    explicit DataObject(std::function<QStringList()> provider)
        : m_provider(std::move(provider))
    {
    }
    virtual ~DataObject()
    {
        if (m_hdrop)
            GlobalFree(m_hdrop);
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --m_ref;
        if (r == 0)
            delete this;
        return r;
    }

    // IDataObject
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *fmt, STGMEDIUM *med) override
    {
        if (!isHDrop(fmt))
            return DV_E_FORMATETC;
        if (!m_materialized) {
            const QStringList paths = m_provider ? m_provider() : QStringList();
            if (!paths.isEmpty())
                m_hdrop = makeHDrop(paths);
            m_materialized = true;
        }
        if (!m_hdrop)
            return E_UNEXPECTED; // cancelled or nothing to copy
        med->tymed = TYMED_HGLOBAL;
        med->hGlobal = cloneGlobal(m_hdrop);
        med->pUnkForRelease = nullptr;
        return med->hGlobal ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *fmt) override
    {
        return isHDrop(fmt) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) override
    {
        if (out)
            out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC **out) override
    {
        if (dir != DATADIR_GET)
            return E_NOTIMPL;
        FORMATETC fmt = hdropFormat();
        return SHCreateStdEnumFmtEtc(1, &fmt, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *,
                                     DWORD *) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    static FORMATETC hdropFormat()
    {
        FORMATETC fmt{};
        fmt.cfFormat = CF_HDROP;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;
        return fmt;
    }
    static bool isHDrop(const FORMATETC *fmt)
    {
        return fmt && fmt->cfFormat == CF_HDROP &&
               (fmt->dwAspect & DVASPECT_CONTENT) && (fmt->tymed & TYMED_HGLOBAL);
    }

    std::function<QStringList()> m_provider;
    HGLOBAL m_hdrop = nullptr;
    bool m_materialized = false;
    ULONG m_ref = 1;
};

// Standard IDropSource: cancel on Esc, drop when the mouse button releases.
class DropSource : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --m_ref;
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed,
                                               DWORD keyState) override
    {
        if (escapePressed)
            return DRAGDROP_S_CANCEL;
        if (!(keyState & (MK_LBUTTON | MK_RBUTTON)))
            return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    ULONG m_ref = 1;
};

} // namespace

bool startWindowsFileDrag(std::function<QStringList()> provideFiles)
{
    auto *data = new DataObject(std::move(provideFiles));
    auto *source = new DropSource;

    DWORD effect = 0;
    const HRESULT hr = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);

    data->Release();
    source->Release();
    return hr == DRAGDROP_S_DROP && (effect & DROPEFFECT_COPY);
}

QStringList debugRoundTripHDrop(const QStringList &paths)
{
    auto *data = new DataObject([paths] { return paths; });
    QStringList out;

    FORMATETC fmt{};
    fmt.cfFormat = CF_HDROP;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;

    STGMEDIUM med{};
    if (data->QueryGetData(&fmt) == S_OK && data->GetData(&fmt, &med) == S_OK) {
        auto *df = static_cast<DROPFILES *>(GlobalLock(med.hGlobal));
        const auto *p = reinterpret_cast<const wchar_t *>(
            reinterpret_cast<const BYTE *>(df) + df->pFiles);
        while (*p) {
            const QString s = QString::fromWCharArray(p);
            out << s;
            p += s.length() + 1;
        }
        GlobalUnlock(med.hGlobal);
        ReleaseStgMedium(&med);
    }
    data->Release();
    return out;
}

} // namespace termsync::ui

#else // !_WIN32

namespace termsync::ui {
bool startWindowsFileDrag(std::function<QStringList()>) { return false; }
QStringList debugRoundTripHDrop(const QStringList &) { return {}; }
} // namespace termsync::ui

#endif
