/*
 * Generic Implementation of strmbase Base Renderer classes
 *
 * Copyright 2012 Aric Stewart, CodeWeavers
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
 */

#include "strmbase_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(strmbase);

static inline BaseInputPin *impl_BaseInputPin_from_IPin( IPin *iface )
{
    return CONTAINING_RECORD(iface, BaseInputPin, pin.IPin_iface);
}

static inline BaseRenderer *impl_from_IBaseFilter(IBaseFilter *iface)
{
    return CONTAINING_RECORD(iface, BaseRenderer, filter.IBaseFilter_iface);
}

static inline BaseRenderer *impl_from_BaseFilter(BaseFilter *iface)
{
    return CONTAINING_RECORD(iface, BaseRenderer, filter);
}

static const IQualityControlVtbl Renderer_QualityControl_Vtbl = {
    QualityControlImpl_QueryInterface,
    QualityControlImpl_AddRef,
    QualityControlImpl_Release,
    QualityControlImpl_Notify,
    QualityControlImpl_SetSink
};

static HRESULT WINAPI BaseRenderer_InputPin_ReceiveConnection(IPin * iface, IPin * pReceivePin, const AM_MEDIA_TYPE * pmt)
{
    BaseInputPin *This = impl_BaseInputPin_from_IPin(iface);
    BaseRenderer *renderer = impl_from_IBaseFilter(This->pin.pinInfo.pFilter);
    HRESULT hr;

    TRACE("(%p/%p)->(%p, %p)\n", This, renderer, pReceivePin, pmt);

    EnterCriticalSection(This->pin.pCritSec);
    hr = BaseInputPinImpl_ReceiveConnection(iface, pReceivePin, pmt);
    if (SUCCEEDED(hr))
    {
        if (renderer->pFuncsTable->pfnCompleteConnect)
            hr = renderer->pFuncsTable->pfnCompleteConnect(renderer, pReceivePin);
    }
    LeaveCriticalSection(This->pin.pCritSec);

    return hr;
}

static HRESULT WINAPI BaseRenderer_InputPin_Disconnect(IPin * iface)
{
    BaseInputPin *This = impl_BaseInputPin_from_IPin(iface);
    BaseRenderer *renderer = impl_from_IBaseFilter(This->pin.pinInfo.pFilter);
    HRESULT hr;

    TRACE("(%p/%p)\n", This, renderer);

    EnterCriticalSection(This->pin.pCritSec);
    hr = BasePinImpl_Disconnect(iface);
    if (SUCCEEDED(hr))
    {
        if (renderer->pFuncsTable->pfnBreakConnect)
            hr = renderer->pFuncsTable->pfnBreakConnect(renderer);
    }
    BaseRendererImpl_ClearPendingSample(renderer);
    LeaveCriticalSection(This->pin.pCritSec);

    return hr;
}

static HRESULT WINAPI BaseRenderer_InputPin_EndOfStream(IPin * iface)
{
    HRESULT hr;
    BaseInputPin* This = impl_BaseInputPin_from_IPin(iface);
    BaseRenderer *pFilter = impl_from_IBaseFilter(This->pin.pinInfo.pFilter);

    TRACE("(%p/%p)->()\n", This, pFilter);

    EnterCriticalSection(&pFilter->csRenderLock);
    EnterCriticalSection(&pFilter->filter.csFilter);
    hr = BaseInputPinImpl_EndOfStream(iface);
    EnterCriticalSection(This->pin.pCritSec);
    if (SUCCEEDED(hr))
    {
        if (pFilter->pFuncsTable->pfnEndOfStream)
            hr = pFilter->pFuncsTable->pfnEndOfStream(pFilter);
        else
            hr = BaseRendererImpl_EndOfStream(pFilter);
    }
    LeaveCriticalSection(This->pin.pCritSec);
    LeaveCriticalSection(&pFilter->filter.csFilter);
    LeaveCriticalSection(&pFilter->csRenderLock);
    return hr;
}

static HRESULT WINAPI BaseRenderer_InputPin_BeginFlush(IPin * iface)
{
    BaseInputPin* This = impl_BaseInputPin_from_IPin(iface);
    BaseRenderer *pFilter = impl_from_IBaseFilter(This->pin.pinInfo.pFilter);
    HRESULT hr;

    TRACE("(%p/%p)->()\n", This, iface);

    EnterCriticalSection(&pFilter->csRenderLock);
    EnterCriticalSection(&pFilter->filter.csFilter);
    EnterCriticalSection(This->pin.pCritSec);
    hr = BaseInputPinImpl_BeginFlush(iface);
    if (SUCCEEDED(hr))
    {
        if (pFilter->pFuncsTable->pfnBeginFlush)
            hr = pFilter->pFuncsTable->pfnBeginFlush(pFilter);
        else
            hr = BaseRendererImpl_BeginFlush(pFilter);
    }
    LeaveCriticalSection(This->pin.pCritSec);
    LeaveCriticalSection(&pFilter->filter.csFilter);
    LeaveCriticalSection(&pFilter->csRenderLock);
    return hr;
}

static HRESULT WINAPI BaseRenderer_InputPin_EndFlush(IPin * iface)
{
    BaseInputPin* This = impl_BaseInputPin_from_IPin(iface);
    BaseRenderer *pFilter = impl_from_IBaseFilter(This->pin.pinInfo.pFilter);
    HRESULT hr;

    TRACE("(%p/%p)->()\n", This, pFilter);

    EnterCriticalSection(&pFilter->csRenderLock);
    EnterCriticalSection(&pFilter->filter.csFilter);
    EnterCriticalSection(This->pin.pCritSec);
    hr = BaseInputPinImpl_EndFlush(iface);
    if (SUCCEEDED(hr))
    {
        if (pFilter->pFuncsTable->pfnEndFlush)
            hr = pFilter->pFuncsTable->pfnEndFlush(pFilter);
        else
            hr = BaseRendererImpl_EndFlush(pFilter);
    }
    LeaveCriticalSection(This->pin.pCritSec);
    LeaveCriticalSection(&pFilter->filter.csFilter);
    LeaveCriticalSection(&pFilter->csRenderLock);
    return hr;
}

static const IPinVtbl BaseRenderer_InputPin_Vtbl =
{
    BaseInputPinImpl_QueryInterface,
    BasePinImpl_AddRef,
    BaseInputPinImpl_Release,
    BaseInputPinImpl_Connect,
    BaseRenderer_InputPin_ReceiveConnection,
    BaseRenderer_InputPin_Disconnect,
    BasePinImpl_ConnectedTo,
    BasePinImpl_ConnectionMediaType,
    BasePinImpl_QueryPinInfo,
    BasePinImpl_QueryDirection,
    BasePinImpl_QueryId,
    BasePinImpl_QueryAccept,
    BasePinImpl_EnumMediaTypes,
    BasePinImpl_QueryInternalConnections,
    BaseRenderer_InputPin_EndOfStream,
    BaseRenderer_InputPin_BeginFlush,
    BaseRenderer_InputPin_EndFlush,
    BaseInputPinImpl_NewSegment
};

static IPin *renderer_get_pin(BaseFilter *iface, unsigned int index)
{
    BaseRenderer *This = impl_from_BaseFilter(iface);

    if (index >= 1)
        return NULL;

    return &This->pInputPin->pin.IPin_iface;
}

static void renderer_destroy(BaseFilter *iface)
{
    BaseRenderer *filter = impl_from_BaseFilter(iface);
    filter->pFuncsTable->renderer_destroy(filter);
}

static HRESULT renderer_query_interface(BaseFilter *iface, REFIID iid, void **out)
{
    BaseRenderer *filter = impl_from_BaseFilter(iface);
    HRESULT hr;

    if (filter->pFuncsTable->renderer_query_interface
            && SUCCEEDED(hr = filter->pFuncsTable->renderer_query_interface(filter, iid, out)))
    {
        return hr;
    }

    if (IsEqualIID(iid, &IID_IMediaSeeking) || IsEqualIID(iid, &IID_IMediaPosition))
        return IUnknown_QueryInterface(filter->pPosition, iid, out);
    else if (IsEqualIID(iid, &IID_IQualityControl))
    {
        *out = &filter->qcimpl->IQualityControl_iface;
        IUnknown_AddRef((IUnknown *)*out);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static const BaseFilterFuncTable RendererBaseFilterFuncTable = {
    .filter_get_pin = renderer_get_pin,
    .filter_destroy = renderer_destroy,
    .filter_query_interface = renderer_query_interface,
};

static HRESULT WINAPI BaseRenderer_Input_CheckMediaType(BasePin *pin, const AM_MEDIA_TYPE * pmt)
{
    BaseRenderer *This = impl_from_IBaseFilter(pin->pinInfo.pFilter);
    return This->pFuncsTable->pfnCheckMediaType(This, pmt);
}

static HRESULT WINAPI BaseRenderer_Receive(BaseInputPin *pin, IMediaSample * pSample)
{
    BaseRenderer *This = impl_from_IBaseFilter(pin->pin.pinInfo.pFilter);
    return BaseRendererImpl_Receive(This, pSample);
}

static const BaseInputPinFuncTable input_BaseInputFuncTable = {
    {
        BaseRenderer_Input_CheckMediaType,
        BasePinImpl_GetMediaType
    },
    BaseRenderer_Receive
};


HRESULT WINAPI strmbase_renderer_init(BaseRenderer *This, const IBaseFilterVtbl *vtbl,
        IUnknown *outer, const CLSID *clsid, const WCHAR *sink_name, DWORD_PTR debug_info,
        const BaseRendererFuncTable *pBaseFuncsTable)
{
    PIN_INFO piInput;
    HRESULT hr;

    strmbase_filter_init(&This->filter, vtbl, outer, clsid, debug_info, &RendererBaseFilterFuncTable);

    This->pFuncsTable = pBaseFuncsTable;

    /* construct input pin */
    piInput.dir = PINDIR_INPUT;
    piInput.pFilter = &This->filter.IBaseFilter_iface;
    lstrcpynW(piInput.achName, sink_name, ARRAY_SIZE(piInput.achName));

    hr = BaseInputPin_Construct(&BaseRenderer_InputPin_Vtbl, sizeof(BaseInputPin), &piInput,
            &input_BaseInputFuncTable, &This->filter.csFilter, NULL, (IPin **)&This->pInputPin);

    if (SUCCEEDED(hr))
    {
        hr = CreatePosPassThru(outer ? outer : (IUnknown *)&This->filter.IBaseFilter_iface, TRUE,
                &This->pInputPin->pin.IPin_iface, &This->pPosition);
        if (FAILED(hr))
            return hr;

        InitializeCriticalSection(&This->csRenderLock);
        This->csRenderLock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__": BaseRenderer.csRenderLock");
        This->evComplete = CreateEventW(NULL, TRUE, TRUE, NULL);
        This->ThreadSignal = CreateEventW(NULL, TRUE, TRUE, NULL);
        This->RenderEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        This->pMediaSample = NULL;

        QualityControlImpl_Create(&This->pInputPin->pin.IPin_iface, &This->filter.IBaseFilter_iface, &This->qcimpl);
        This->qcimpl->IQualityControl_iface.lpVtbl = &Renderer_QualityControl_Vtbl;
    }

    return hr;
}

void strmbase_renderer_cleanup(BaseRenderer *filter)
{
    IPin *peer;

    if (SUCCEEDED(IPin_ConnectedTo(&filter->pInputPin->pin.IPin_iface, &peer)))
    {
        IPin_Disconnect(peer);
        IPin_Release(peer);
    }
    IPin_Disconnect(&filter->pInputPin->pin.IPin_iface);
    IPin_Release(&filter->pInputPin->pin.IPin_iface);

    if (filter->pPosition)
        IUnknown_Release(filter->pPosition);

    filter->csRenderLock.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection(&filter->csRenderLock);

    BaseRendererImpl_ClearPendingSample(filter);
    CloseHandle(filter->evComplete);
    CloseHandle(filter->ThreadSignal);
    CloseHandle(filter->RenderEvent);
    QualityControlImpl_Destroy(filter->qcimpl);
    strmbase_filter_cleanup(&filter->filter);
}

HRESULT WINAPI BaseRendererImpl_Receive(BaseRenderer *This, IMediaSample * pSample)
{
    HRESULT hr = S_OK;
    REFERENCE_TIME start, stop;
    AM_MEDIA_TYPE *pmt;

    TRACE("(%p)->%p\n", This, pSample);

    if (This->pInputPin->end_of_stream || This->pInputPin->flushing)
        return S_FALSE;

    if (This->filter.state == State_Stopped)
        return VFW_E_WRONG_STATE;

    if (IMediaSample_GetMediaType(pSample, &pmt) == S_OK)
    {
        if (FAILED(This->pFuncsTable->pfnCheckMediaType(This, pmt)))
        {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        DeleteMediaType(pmt);
    }

    This->pMediaSample = pSample;
    IMediaSample_AddRef(pSample);

    if (This->pFuncsTable->pfnPrepareReceive)
        hr = This->pFuncsTable->pfnPrepareReceive(This, pSample);
    if (FAILED(hr))
    {
        if (hr == VFW_E_SAMPLE_REJECTED)
            return S_OK;
        else
            return hr;
    }

    if (This->pFuncsTable->pfnPrepareRender)
        This->pFuncsTable->pfnPrepareRender(This);

    EnterCriticalSection(&This->csRenderLock);
    if ( This->filter.state == State_Paused )
    {
        if (This->pFuncsTable->pfnOnReceiveFirstSample)
            This->pFuncsTable->pfnOnReceiveFirstSample(This, pSample);

        SetEvent(This->evComplete);
    }

    /* Wait for render Time */
    if (SUCCEEDED(IMediaSample_GetTime(pSample, &start, &stop)))
    {
        hr = S_FALSE;
        RendererPosPassThru_RegisterMediaTime(This->pPosition, start);
        if (This->pFuncsTable->pfnShouldDrawSampleNow)
            hr = This->pFuncsTable->pfnShouldDrawSampleNow(This, pSample, &start, &stop);

        if (hr == S_OK)
            ;/* Do not wait: drop through */
        else if (hr == S_FALSE)
        {
            if (This->pFuncsTable->pfnOnWaitStart)
                This->pFuncsTable->pfnOnWaitStart(This);

            LeaveCriticalSection(&This->csRenderLock);
            hr = QualityControlRender_WaitFor(This->qcimpl, pSample, This->RenderEvent);
            EnterCriticalSection(&This->csRenderLock);

            if (This->pFuncsTable->pfnOnWaitEnd)
                This->pFuncsTable->pfnOnWaitEnd(This);
        }
        else
        {
            LeaveCriticalSection(&This->csRenderLock);
            /* Drop Sample */
            return S_OK;
        }
    }

    if (SUCCEEDED(hr))
    {
        QualityControlRender_BeginRender(This->qcimpl);
        hr = This->pFuncsTable->pfnDoRenderSample(This, pSample);
        QualityControlRender_EndRender(This->qcimpl);
    }

    QualityControlRender_DoQOS(This->qcimpl);

    BaseRendererImpl_ClearPendingSample(This);
    LeaveCriticalSection(&This->csRenderLock);

    return hr;
}

HRESULT WINAPI BaseRendererImpl_Stop(IBaseFilter * iface)
{
    BaseRenderer *This = impl_from_IBaseFilter(iface);

    TRACE("(%p)->()\n", This);

    EnterCriticalSection(&This->csRenderLock);
    {
        RendererPosPassThru_ResetMediaTime(This->pPosition);
        if (This->pFuncsTable->pfnOnStopStreaming)
            This->pFuncsTable->pfnOnStopStreaming(This);
        This->filter.state = State_Stopped;
        SetEvent(This->evComplete);
        SetEvent(This->ThreadSignal);
        SetEvent(This->RenderEvent);
    }
    LeaveCriticalSection(&This->csRenderLock);

    return S_OK;
}

HRESULT WINAPI BaseRendererImpl_Run(IBaseFilter * iface, REFERENCE_TIME tStart)
{
    HRESULT hr = S_OK;
    BaseRenderer *This = impl_from_IBaseFilter(iface);
    TRACE("(%p)->(%s)\n", This, wine_dbgstr_longlong(tStart));

    EnterCriticalSection(&This->csRenderLock);
    This->filter.rtStreamStart = tStart;
    if (This->filter.state == State_Running)
        goto out;

    SetEvent(This->evComplete);
    ResetEvent(This->ThreadSignal);

    if (This->pInputPin->pin.pConnectedTo)
    {
        This->pInputPin->end_of_stream = FALSE;
    }
    else if (This->filter.filterInfo.pGraph)
    {
        IMediaEventSink *pEventSink;
        hr = IFilterGraph_QueryInterface(This->filter.filterInfo.pGraph, &IID_IMediaEventSink, (LPVOID*)&pEventSink);
        if (SUCCEEDED(hr))
        {
            hr = IMediaEventSink_Notify(pEventSink, EC_COMPLETE, S_OK, (LONG_PTR)This);
            IMediaEventSink_Release(pEventSink);
        }
        hr = S_OK;
    }
    if (SUCCEEDED(hr))
    {
        QualityControlRender_Start(This->qcimpl, This->filter.rtStreamStart);
        if (This->pFuncsTable->pfnOnStartStreaming)
            This->pFuncsTable->pfnOnStartStreaming(This);
        if (This->filter.state == State_Stopped)
            BaseRendererImpl_ClearPendingSample(This);
        SetEvent(This->RenderEvent);
        This->filter.state = State_Running;
    }
out:
    LeaveCriticalSection(&This->csRenderLock);

    return hr;
}

HRESULT WINAPI BaseRendererImpl_Pause(IBaseFilter * iface)
{
    BaseRenderer *This = impl_from_IBaseFilter(iface);

    TRACE("(%p)->()\n", This);

    EnterCriticalSection(&This->csRenderLock);
    {
     if (This->filter.state != State_Paused)
        {
            if (This->filter.state == State_Stopped)
            {
                if (This->pInputPin->pin.pConnectedTo)
                    ResetEvent(This->evComplete);
                This->pInputPin->end_of_stream = FALSE;
            }
            else if (This->pFuncsTable->pfnOnStopStreaming)
                This->pFuncsTable->pfnOnStopStreaming(This);

            if (This->filter.state == State_Stopped)
                BaseRendererImpl_ClearPendingSample(This);
            ResetEvent(This->RenderEvent);
            This->filter.state = State_Paused;
        }
    }
    ResetEvent(This->ThreadSignal);
    LeaveCriticalSection(&This->csRenderLock);

    return S_OK;
}

HRESULT WINAPI BaseRendererImpl_SetSyncSource(IBaseFilter *iface, IReferenceClock *clock)
{
    BaseRenderer *This = impl_from_IBaseFilter(iface);
    HRESULT hr;

    EnterCriticalSection(&This->filter.csFilter);
    QualityControlRender_SetClock(This->qcimpl, clock);
    hr = BaseFilterImpl_SetSyncSource(iface, clock);
    LeaveCriticalSection(&This->filter.csFilter);
    return hr;
}


HRESULT WINAPI BaseRendererImpl_GetState(IBaseFilter * iface, DWORD dwMilliSecsTimeout, FILTER_STATE *pState)
{
    HRESULT hr;
    BaseRenderer *This = impl_from_IBaseFilter(iface);

    TRACE("(%p)->(%d, %p)\n", This, dwMilliSecsTimeout, pState);

    if (WaitForSingleObject(This->evComplete, dwMilliSecsTimeout) == WAIT_TIMEOUT)
        hr = VFW_S_STATE_INTERMEDIATE;
    else
        hr = S_OK;

    BaseFilterImpl_GetState(iface, dwMilliSecsTimeout, pState);

    return hr;
}

HRESULT WINAPI BaseRendererImpl_EndOfStream(BaseRenderer* iface)
{
    IMediaEventSink* pEventSink;
    IFilterGraph *graph;
    HRESULT hr = S_OK;

    TRACE("(%p)\n", iface);

    graph = iface->filter.filterInfo.pGraph;
    if (graph)
    {        hr = IFilterGraph_QueryInterface(iface->filter.filterInfo.pGraph, &IID_IMediaEventSink, (LPVOID*)&pEventSink);
        if (SUCCEEDED(hr))
        {
            hr = IMediaEventSink_Notify(pEventSink, EC_COMPLETE, S_OK, (LONG_PTR)iface);
            IMediaEventSink_Release(pEventSink);
        }
    }
    RendererPosPassThru_EOS(iface->pPosition);
    SetEvent(iface->evComplete);

    return hr;
}

HRESULT WINAPI BaseRendererImpl_BeginFlush(BaseRenderer* iface)
{
    TRACE("(%p)\n", iface);
    BaseRendererImpl_ClearPendingSample(iface);
    SetEvent(iface->ThreadSignal);
    SetEvent(iface->RenderEvent);
    return S_OK;
}

HRESULT WINAPI BaseRendererImpl_EndFlush(BaseRenderer* iface)
{
    TRACE("(%p)\n", iface);
    QualityControlRender_Start(iface->qcimpl, iface->filter.rtStreamStart);
    RendererPosPassThru_ResetMediaTime(iface->pPosition);
    ResetEvent(iface->ThreadSignal);
    ResetEvent(iface->RenderEvent);
    return S_OK;
}

HRESULT WINAPI BaseRendererImpl_ClearPendingSample(BaseRenderer *iface)
{
    if (iface->pMediaSample)
    {
        IMediaSample_Release(iface->pMediaSample);
        iface->pMediaSample = NULL;
    }
    return S_OK;
}
