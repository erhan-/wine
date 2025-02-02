/* Video For Windows Steering structure
 *
 * Copyright 2005 Maarten Lankhorst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#define COBJMACROS

#include "config.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wtypes.h"
#include "wingdi.h"
#include "winuser.h"
#include "dshow.h"

#include "qcap_main.h"
#include "wine/debug.h"

#include "capture.h"
#include "uuids.h"
#include "vfwmsgs.h"
#include "amvideo.h"
#include "strmif.h"
#include "ddraw.h"
#include "ocidl.h"
#include "oleauto.h"

WINE_DEFAULT_DEBUG_CHANNEL(qcap);

static const IBaseFilterVtbl VfwCapture_Vtbl;
static const IAMStreamConfigVtbl IAMStreamConfig_VTable;
static const IAMVideoProcAmpVtbl IAMVideoProcAmp_VTable;
static const IPersistPropertyBagVtbl IPersistPropertyBag_VTable;
static const IPinVtbl VfwPin_Vtbl;

static HRESULT VfwPin_Construct( IBaseFilter *, LPCRITICAL_SECTION, IPin ** );

typedef struct VfwCapture
{
    BaseFilter filter;
    IAMStreamConfig IAMStreamConfig_iface;
    IAMVideoProcAmp IAMVideoProcAmp_iface;
    IPersistPropertyBag IPersistPropertyBag_iface;
    BOOL init;
    Capture *driver_info;

    IPin * pOutputPin;
} VfwCapture;

static inline VfwCapture *impl_from_BaseFilter(BaseFilter *iface)
{
    return CONTAINING_RECORD(iface, VfwCapture, filter);
}

static inline VfwCapture *impl_from_IBaseFilter(IBaseFilter *iface)
{
    return CONTAINING_RECORD(iface, VfwCapture, filter.IBaseFilter_iface);
}

static inline VfwCapture *impl_from_IAMStreamConfig(IAMStreamConfig *iface)
{
    return CONTAINING_RECORD(iface, VfwCapture, IAMStreamConfig_iface);
}

static inline VfwCapture *impl_from_IAMVideoProcAmp(IAMVideoProcAmp *iface)
{
    return CONTAINING_RECORD(iface, VfwCapture, IAMVideoProcAmp_iface);
}

static inline VfwCapture *impl_from_IPersistPropertyBag(IPersistPropertyBag *iface)
{
    return CONTAINING_RECORD(iface, VfwCapture, IPersistPropertyBag_iface);
}

/* VfwPin implementation */
typedef struct VfwPinImpl
{
    BaseOutputPin pin;
    IKsPropertySet IKsPropertySet_iface;
    VfwCapture *parent;
} VfwPinImpl;

static IPin *vfw_capture_get_pin(BaseFilter *iface, unsigned int index)
{
    VfwCapture *This = impl_from_BaseFilter(iface);

    if (index >= 1)
        return NULL;

    return This->pOutputPin;
}

static void vfw_capture_destroy(BaseFilter *iface)
{
    VfwCapture *filter = impl_from_BaseFilter(iface);
    IPin *peer = NULL;

    if (filter->init)
    {
        if (filter->filter.state != State_Stopped)
            qcap_driver_stop(filter->driver_info, &filter->filter.state);
        qcap_driver_destroy(filter->driver_info);
    }
    IPin_ConnectedTo(filter->pOutputPin, &peer);
    if (peer)
    {
        IPin_Disconnect(peer);
        IPin_Disconnect(filter->pOutputPin);
        IPin_Release(peer);
    }
    IPin_Release(filter->pOutputPin);
    strmbase_filter_cleanup(&filter->filter);
    CoTaskMemFree(filter);
    ObjectRefCount(FALSE);
}

static HRESULT vfw_capture_query_interface(BaseFilter *iface, REFIID iid, void **out)
{
    VfwCapture *filter = impl_from_BaseFilter(iface);

    if (IsEqualGUID(iid, &IID_IPersistPropertyBag))
        *out = &filter->IPersistPropertyBag_iface;
    else if (IsEqualGUID(iid, &IID_IAMStreamConfig))
        *out = &filter->IAMStreamConfig_iface;
    else if (IsEqualGUID(iid, &IID_IAMVideoProcAmp))
        *out = &filter->IAMVideoProcAmp_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static const BaseFilterFuncTable BaseFuncTable = {
    .filter_get_pin = vfw_capture_get_pin,
    .filter_destroy = vfw_capture_destroy,
    .filter_query_interface = vfw_capture_query_interface,
};

IUnknown * WINAPI QCAP_createVFWCaptureFilter(IUnknown *outer, HRESULT *phr)
{
    VfwCapture *pVfwCapture;
    HRESULT hr;

    *phr = E_OUTOFMEMORY;
    pVfwCapture = CoTaskMemAlloc( sizeof(VfwCapture) );
    if (!pVfwCapture)
        return NULL;

    strmbase_filter_init(&pVfwCapture->filter, &VfwCapture_Vtbl, outer, &CLSID_VfwCapture,
            (DWORD_PTR)(__FILE__ ": VfwCapture.csFilter"), &BaseFuncTable);

    pVfwCapture->IAMStreamConfig_iface.lpVtbl = &IAMStreamConfig_VTable;
    pVfwCapture->IAMVideoProcAmp_iface.lpVtbl = &IAMVideoProcAmp_VTable;
    pVfwCapture->IPersistPropertyBag_iface.lpVtbl = &IPersistPropertyBag_VTable;
    pVfwCapture->init = FALSE;

    hr = VfwPin_Construct(&pVfwCapture->filter.IBaseFilter_iface,
                   &pVfwCapture->filter.csFilter, &pVfwCapture->pOutputPin);
    if (FAILED(hr))
    {
        CoTaskMemFree(pVfwCapture);
        return NULL;
    }
    TRACE("-- created at %p\n", pVfwCapture);

    ObjectRefCount(TRUE);
    *phr = S_OK;
    return &pVfwCapture->filter.IUnknown_inner;
}

/** IMediaFilter methods **/

static HRESULT WINAPI VfwCapture_Stop(IBaseFilter * iface)
{
    VfwCapture *This = impl_from_IBaseFilter(iface);

    TRACE("()\n");
    return qcap_driver_stop(This->driver_info, &This->filter.state);
}

static HRESULT WINAPI VfwCapture_Pause(IBaseFilter * iface)
{
    VfwCapture *This = impl_from_IBaseFilter(iface);

    TRACE("()\n");
    return qcap_driver_pause(This->driver_info, &This->filter.state);
}

static HRESULT WINAPI VfwCapture_Run(IBaseFilter * iface, REFERENCE_TIME tStart)
{
    VfwCapture *This = impl_from_IBaseFilter(iface);
    TRACE("(%s)\n", wine_dbgstr_longlong(tStart));
    return qcap_driver_run(This->driver_info, &This->filter.state);
}

static const IBaseFilterVtbl VfwCapture_Vtbl =
{
    BaseFilterImpl_QueryInterface,
    BaseFilterImpl_AddRef,
    BaseFilterImpl_Release,
    BaseFilterImpl_GetClassID,
    VfwCapture_Stop,
    VfwCapture_Pause,
    VfwCapture_Run,
    BaseFilterImpl_GetState,
    BaseFilterImpl_SetSyncSource,
    BaseFilterImpl_GetSyncSource,
    BaseFilterImpl_EnumPins,
    BaseFilterImpl_FindPin,
    BaseFilterImpl_QueryFilterInfo,
    BaseFilterImpl_JoinFilterGraph,
    BaseFilterImpl_QueryVendorInfo
};

/* AMStreamConfig interface, we only need to implement {G,S}etFormat */
static HRESULT WINAPI AMStreamConfig_QueryInterface(IAMStreamConfig *iface, REFIID riid,
        void **ret_iface)
{
    VfwCapture *This = impl_from_IAMStreamConfig(iface);

    return IUnknown_QueryInterface(This->filter.outer_unk, riid, ret_iface);
}

static ULONG WINAPI AMStreamConfig_AddRef( IAMStreamConfig * iface )
{
    VfwCapture *This = impl_from_IAMStreamConfig(iface);

    return IUnknown_AddRef(This->filter.outer_unk);
}

static ULONG WINAPI AMStreamConfig_Release( IAMStreamConfig * iface )
{
    VfwCapture *This = impl_from_IAMStreamConfig(iface);

    return IUnknown_Release(This->filter.outer_unk);
}

static HRESULT WINAPI
AMStreamConfig_SetFormat(IAMStreamConfig *iface, AM_MEDIA_TYPE *pmt)
{
    HRESULT hr;
    VfwCapture *This = impl_from_IAMStreamConfig(iface);
    BasePin *pin;

    TRACE("(%p): %p->%p\n", iface, pmt, pmt ? pmt->pbFormat : NULL);

    if (This->filter.state != State_Stopped)
    {
        TRACE("Returning not stopped error\n");
        return VFW_E_NOT_STOPPED;
    }

    if (!pmt)
    {
        TRACE("pmt is NULL\n");
        return E_POINTER;
    }

    dump_AM_MEDIA_TYPE(pmt);

    pin = (BasePin *)This->pOutputPin;
    if (pin->pConnectedTo != NULL)
    {
        hr = IPin_QueryAccept(pin->pConnectedTo, pmt);
        TRACE("Would accept: %d\n", hr);
        if (hr == S_FALSE)
            return VFW_E_INVALIDMEDIATYPE;
    }

    hr = qcap_driver_set_format(This->driver_info, pmt);
    if (SUCCEEDED(hr) && This->filter.filterInfo.pGraph && pin->pConnectedTo )
    {
        hr = IFilterGraph_Reconnect(This->filter.filterInfo.pGraph, This->pOutputPin);
        if (SUCCEEDED(hr))
            TRACE("Reconnection completed, with new media format..\n");
    }
    TRACE("Returning: %d\n", hr);
    return hr;
}

static HRESULT WINAPI
AMStreamConfig_GetFormat( IAMStreamConfig *iface, AM_MEDIA_TYPE **pmt )
{
    VfwCapture *This = impl_from_IAMStreamConfig(iface);

    TRACE("%p -> (%p)\n", iface, pmt);
    return qcap_driver_get_format(This->driver_info, pmt);
}

static HRESULT WINAPI
AMStreamConfig_GetNumberOfCapabilities( IAMStreamConfig *iface, int *piCount,
                                        int *piSize )
{
    FIXME("%p: %p %p - stub, intentional\n", iface, piCount, piSize);
    *piCount = 0;
    return E_NOTIMPL; /* Not implemented for this interface */
}

static HRESULT WINAPI
AMStreamConfig_GetStreamCaps( IAMStreamConfig *iface, int iIndex,
                              AM_MEDIA_TYPE **pmt, BYTE *pSCC )
{
    FIXME("%p: %d %p %p - stub, intentional\n", iface, iIndex, pmt, pSCC);
    return E_NOTIMPL; /* Not implemented for this interface */
}

static const IAMStreamConfigVtbl IAMStreamConfig_VTable =
{
    AMStreamConfig_QueryInterface,
    AMStreamConfig_AddRef,
    AMStreamConfig_Release,
    AMStreamConfig_SetFormat,
    AMStreamConfig_GetFormat,
    AMStreamConfig_GetNumberOfCapabilities,
    AMStreamConfig_GetStreamCaps
};

static HRESULT WINAPI AMVideoProcAmp_QueryInterface(IAMVideoProcAmp *iface, REFIID riid,
        void **ret_iface)
{
    VfwCapture *This = impl_from_IAMVideoProcAmp(iface);

    return IUnknown_QueryInterface(This->filter.outer_unk, riid, ret_iface);
}

static ULONG WINAPI AMVideoProcAmp_AddRef(IAMVideoProcAmp * iface)
{
    VfwCapture *This = impl_from_IAMVideoProcAmp(iface);

    return IUnknown_AddRef(This->filter.outer_unk);
}

static ULONG WINAPI AMVideoProcAmp_Release(IAMVideoProcAmp * iface)
{
    VfwCapture *This = impl_from_IAMVideoProcAmp(iface);

    return IUnknown_Release(This->filter.outer_unk);
}

static HRESULT WINAPI
AMVideoProcAmp_GetRange( IAMVideoProcAmp * iface, LONG Property, LONG *pMin,
        LONG *pMax, LONG *pSteppingDelta, LONG *pDefault, LONG *pCapsFlags )
{
    VfwCapture *This = impl_from_IAMVideoProcAmp(iface);

    return qcap_driver_get_prop_range( This->driver_info, Property, pMin, pMax,
                   pSteppingDelta, pDefault, pCapsFlags );
}

static HRESULT WINAPI
AMVideoProcAmp_Set( IAMVideoProcAmp * iface, LONG Property, LONG lValue,
                    LONG Flags )
{
    VfwCapture *This = impl_from_IAMVideoProcAmp(iface);

    return qcap_driver_set_prop(This->driver_info, Property, lValue, Flags);
}

static HRESULT WINAPI
AMVideoProcAmp_Get( IAMVideoProcAmp * iface, LONG Property, LONG *lValue,
                    LONG *Flags )
{
    VfwCapture *This = impl_from_IAMVideoProcAmp(iface);

    return qcap_driver_get_prop(This->driver_info, Property, lValue, Flags);
}

static const IAMVideoProcAmpVtbl IAMVideoProcAmp_VTable =
{
    AMVideoProcAmp_QueryInterface,
    AMVideoProcAmp_AddRef,
    AMVideoProcAmp_Release,
    AMVideoProcAmp_GetRange,
    AMVideoProcAmp_Set,
    AMVideoProcAmp_Get,
};

static HRESULT WINAPI PPB_QueryInterface(IPersistPropertyBag *iface, REFIID riid, void **ret_iface)
{
    VfwCapture *This = impl_from_IPersistPropertyBag(iface);

    return IUnknown_QueryInterface(This->filter.outer_unk, riid, ret_iface);
}

static ULONG WINAPI PPB_AddRef(IPersistPropertyBag * iface)
{
    VfwCapture *This = impl_from_IPersistPropertyBag(iface);

    return IUnknown_AddRef(This->filter.outer_unk);
}

static ULONG WINAPI PPB_Release(IPersistPropertyBag * iface)
{
    VfwCapture *This = impl_from_IPersistPropertyBag(iface);

    return IUnknown_Release(This->filter.outer_unk);
}

static HRESULT WINAPI
PPB_GetClassID( IPersistPropertyBag * iface, CLSID * pClassID )
{
    VfwCapture *This = impl_from_IPersistPropertyBag(iface);

    FIXME("%p - stub\n", This);

    return E_NOTIMPL;
}

static HRESULT WINAPI PPB_InitNew(IPersistPropertyBag * iface)
{
    VfwCapture *This = impl_from_IPersistPropertyBag(iface);

    FIXME("%p - stub\n", This);

    return E_NOTIMPL;
}

static HRESULT WINAPI
PPB_Load( IPersistPropertyBag * iface, IPropertyBag *pPropBag,
          IErrorLog *pErrorLog )
{
    static const OLECHAR VFWIndex[] = {'V','F','W','I','n','d','e','x',0};
    VfwCapture *This = impl_from_IPersistPropertyBag(iface);
    HRESULT hr;
    VARIANT var;

    TRACE("%p/%p-> (%p, %p)\n", iface, This, pPropBag, pErrorLog);

    V_VT(&var) = VT_I4;
    hr = IPropertyBag_Read(pPropBag, VFWIndex, &var, pErrorLog);

    if (SUCCEEDED(hr))
    {
        VfwPinImpl *pin;

        This->driver_info = qcap_driver_init( This->pOutputPin,
               var.__VARIANT_NAME_1.__VARIANT_NAME_2.__VARIANT_NAME_3.ulVal );
        if (This->driver_info)
        {
            pin = (VfwPinImpl *)This->pOutputPin;
            pin->parent = This;
            This->init = TRUE;
            hr = S_OK;
        }
        else
            hr = E_FAIL;
    }

    return hr;
}

static HRESULT WINAPI
PPB_Save( IPersistPropertyBag * iface, IPropertyBag *pPropBag,
          BOOL fClearDirty, BOOL fSaveAllProperties )
{
    VfwCapture *This = impl_from_IPersistPropertyBag(iface);
    FIXME("%p - stub\n", This);
    return E_NOTIMPL;
}

static const IPersistPropertyBagVtbl IPersistPropertyBag_VTable =
{
    PPB_QueryInterface,
    PPB_AddRef,
    PPB_Release,
    PPB_GetClassID,
    PPB_InitNew,
    PPB_Load,
    PPB_Save
};

/* IKsPropertySet interface */
static inline VfwPinImpl *impl_from_IKsPropertySet(IKsPropertySet *iface)
{
    return CONTAINING_RECORD(iface, VfwPinImpl, IKsPropertySet_iface);
}

static HRESULT WINAPI KSP_QueryInterface(IKsPropertySet * iface, REFIID riid, void **ret_iface)
{
    VfwPinImpl *This = impl_from_IKsPropertySet(iface);

    return IPin_QueryInterface(&This->pin.pin.IPin_iface, riid, ret_iface);
}

static ULONG WINAPI KSP_AddRef(IKsPropertySet * iface)
{
    VfwPinImpl *This = impl_from_IKsPropertySet(iface);

    return IPin_AddRef(&This->pin.pin.IPin_iface);
}

static ULONG WINAPI KSP_Release(IKsPropertySet * iface)
{
    VfwPinImpl *This = impl_from_IKsPropertySet(iface);

    return IPin_Release(&This->pin.pin.IPin_iface);
}

static HRESULT WINAPI
KSP_Set( IKsPropertySet * iface, REFGUID guidPropSet, DWORD dwPropID,
         LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData,
         DWORD cbPropData )
{
    FIXME("%p: stub\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI
KSP_Get( IKsPropertySet * iface, REFGUID guidPropSet, DWORD dwPropID,
         LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData,
         DWORD cbPropData, DWORD *pcbReturned )
{
    LPGUID pGuid;

    TRACE("()\n");

    if (!IsEqualIID(guidPropSet, &AMPROPSETID_Pin))
        return E_PROP_SET_UNSUPPORTED;
    if (pPropData == NULL && pcbReturned == NULL)
        return E_POINTER;
    if (pcbReturned)
        *pcbReturned = sizeof(GUID);
    if (pPropData == NULL)
        return S_OK;
    if (cbPropData < sizeof(GUID))
        return E_UNEXPECTED;
    pGuid = pPropData;
    *pGuid = PIN_CATEGORY_CAPTURE;
    FIXME("() Not adding a pin with PIN_CATEGORY_PREVIEW\n");
    return S_OK;
}

static HRESULT WINAPI
KSP_QuerySupported( IKsPropertySet * iface, REFGUID guidPropSet,
                    DWORD dwPropID, DWORD *pTypeSupport )
{
   FIXME("%p: stub\n", iface);
   return E_NOTIMPL;
}

static const IKsPropertySetVtbl IKsPropertySet_VTable =
{
   KSP_QueryInterface,
   KSP_AddRef,
   KSP_Release,
   KSP_Set,
   KSP_Get,
   KSP_QuerySupported
};

static inline VfwPinImpl *impl_from_BasePin(BasePin *pin)
{
    return CONTAINING_RECORD(pin, VfwPinImpl, pin.pin);
}

static HRESULT WINAPI VfwPin_CheckMediaType(BasePin *pin, const AM_MEDIA_TYPE *mt)
{
    VfwPinImpl *filter = impl_from_BasePin(pin);
    return qcap_driver_check_format(filter->parent->driver_info, mt);
}

static HRESULT WINAPI VfwPin_GetMediaType(BasePin *pin, int iPosition, AM_MEDIA_TYPE *pmt)
{
    VfwPinImpl *This = impl_from_BasePin(pin);
    AM_MEDIA_TYPE *vfw_pmt;
    HRESULT hr;

    if (iPosition < 0)
        return E_INVALIDARG;
    if (iPosition > 0)
        return VFW_S_NO_MORE_ITEMS;

    hr = qcap_driver_get_format(This->parent->driver_info, &vfw_pmt);
    if (SUCCEEDED(hr)) {
        CopyMediaType(pmt, vfw_pmt);
        DeleteMediaType(vfw_pmt);
    }
    return hr;
}

static HRESULT WINAPI VfwPin_DecideBufferSize(BaseOutputPin *iface, IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest)
{
    ALLOCATOR_PROPERTIES actual;

    /* What we put here doesn't matter, the
       driver function should override it then commit */
    if (!ppropInputRequest->cBuffers)
        ppropInputRequest->cBuffers = 3;
    if (!ppropInputRequest->cbBuffer)
        ppropInputRequest->cbBuffer = 230400;
    if (!ppropInputRequest->cbAlign)
        ppropInputRequest->cbAlign = 1;

    return IMemAllocator_SetProperties(pAlloc, ppropInputRequest, &actual);
}

static const BaseOutputPinFuncTable output_BaseOutputFuncTable = {
    {
        VfwPin_CheckMediaType,
        VfwPin_GetMediaType
    },
    BaseOutputPinImpl_AttemptConnection,
    VfwPin_DecideBufferSize,
    BaseOutputPinImpl_DecideAllocator,
};

static HRESULT
VfwPin_Construct( IBaseFilter * pBaseFilter, LPCRITICAL_SECTION pCritSec,
                  IPin ** ppPin )
{
    static const WCHAR wszOutputPinName[] = { 'O','u','t','p','u','t',0 };
    PIN_INFO piOutput;
    HRESULT hr;

    *ppPin = NULL;

    piOutput.dir = PINDIR_OUTPUT;
    piOutput.pFilter = pBaseFilter;
    lstrcpyW(piOutput.achName, wszOutputPinName);

    hr = BaseOutputPin_Construct(&VfwPin_Vtbl, sizeof(VfwPinImpl), &piOutput, &output_BaseOutputFuncTable, pCritSec, ppPin);

    if (SUCCEEDED(hr))
    {
        VfwPinImpl *pPinImpl = (VfwPinImpl*)*ppPin;
        pPinImpl->IKsPropertySet_iface.lpVtbl = &IKsPropertySet_VTable;
        ObjectRefCount(TRUE);
    }

    return hr;
}

static inline VfwPinImpl *impl_from_IPin(IPin *iface)
{
    return CONTAINING_RECORD(iface, VfwPinImpl, pin.pin.IPin_iface);
}

static HRESULT WINAPI VfwPin_QueryInterface(IPin * iface, REFIID riid, LPVOID * ppv)
{
    VfwPinImpl *This = impl_from_IPin(iface);

    TRACE("%s %p\n", debugstr_guid(riid), ppv);

    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPin))
        *ppv = This;
    else if (IsEqualIID(riid, &IID_IKsPropertySet))
        *ppv = &This->IKsPropertySet_iface;
    else if (IsEqualIID(riid, &IID_IAMStreamConfig))
        return IUnknown_QueryInterface((IUnknown *)This->parent, riid, ppv);

    if (*ppv)
    {
        IUnknown_AddRef((IUnknown *)(*ppv));
        return S_OK;
    }

    FIXME("No interface for %s!\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI
VfwPin_Release(IPin * iface)
{
   VfwPinImpl *This = impl_from_IPin(iface);
   ULONG refCount = InterlockedDecrement(&This->pin.pin.refCount);

   TRACE("() -> new refcount: %u\n", refCount);

   if (!refCount)
   {
      BaseOutputPin_Destroy(&This->pin);
      ObjectRefCount(FALSE);
   }
   return refCount;
}

static HRESULT WINAPI
VfwPin_EnumMediaTypes(IPin * iface, IEnumMediaTypes ** ppEnum)
{
    VfwPinImpl *This = impl_from_IPin(iface);
    AM_MEDIA_TYPE *pmt;
    HRESULT hr;

    hr = qcap_driver_get_format(This->parent->driver_info, &pmt);
    if (SUCCEEDED(hr)) {
        hr = BasePinImpl_EnumMediaTypes(iface, ppEnum);
        DeleteMediaType(pmt);
    }
    TRACE("%p -- %x\n", This, hr);
    return hr;
}

static HRESULT WINAPI
VfwPin_QueryInternalConnections(IPin * iface, IPin ** apPin, ULONG * cPin)
{
    TRACE("(%p)->(%p, %p)\n", iface, apPin, cPin);
    return E_NOTIMPL;
}

static const IPinVtbl VfwPin_Vtbl =
{
    VfwPin_QueryInterface,
    BasePinImpl_AddRef,
    VfwPin_Release,
    BaseOutputPinImpl_Connect,
    BaseOutputPinImpl_ReceiveConnection,
    BaseOutputPinImpl_Disconnect,
    BasePinImpl_ConnectedTo,
    BasePinImpl_ConnectionMediaType,
    BasePinImpl_QueryPinInfo,
    BasePinImpl_QueryDirection,
    BasePinImpl_QueryId,
    BasePinImpl_QueryAccept,
    VfwPin_EnumMediaTypes,
    VfwPin_QueryInternalConnections,
    BaseOutputPinImpl_EndOfStream,
    BaseOutputPinImpl_BeginFlush,
    BaseOutputPinImpl_EndFlush,
    BasePinImpl_NewSegment
};
