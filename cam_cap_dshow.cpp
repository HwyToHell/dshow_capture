// google-style order
#include "cam_cap_dshow.h"

#include <algorithm>
#include <cstring> // memcpy
#include <iostream>
//#include <string>:	"cam_cap_dshow.h"
//#include <vector>:	"cam_cap_dshow.h"
#include <Windows.h>
//#include <DShow.h>:	"cam_cap_dshow.h"



// encapsulate in classes
/*	vidCapDshow -> replaces opencvs VideoCapture
	videoDevice: webcams
	videoInput: mehrere devices, filtergraph
*/

// enumerate device capabilities

// select capability (frame size)

// create 

// add filter graph to running object table for visualization with graphedt
bool addToRot(IUnknown* pUnkGraph, DWORD* pdwRegister);

// remove filter graph from running object table
bool removeFromRot(DWORD dwRegister);


class SampleGrabberCallback : public ISampleGrabberCB {
private:
	ULONG m_cntRef;
	BYTE* m_bitmap; // TODO provide this bitmap array from cv application
	BYTE* m_bufferMediaSample;

	struct Sample {
		long long time;
		BYTE bitmap[640*480*3];
	};
	std::vector<Sample> m_sampleList;
	LARGE_INTEGER m_lastTime;
	
	int m_counter;
	long long m_sumTime;
	long long m_avgTime;


public:
	SampleGrabberCallback();
	~SampleGrabberCallback();
	ULONG STDMETHODCALLTYPE AddRef(); // virtual method in IUnknown, must be implemented
	ULONG STDMETHODCALLTYPE Release(); // virtual method in IUnknown, must be implemented
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv); // virtual

	STDMETHODIMP BufferCB(double SampleTime, BYTE* pBuffer, long BufferLen);
	STDMETHODIMP SampleCB(double SampleTime, IMediaSample* pSample);
	bool setBitmapSize(size_t bitmapSize);
};

SampleGrabberCallback::SampleGrabberCallback() {
	m_counter = 0;
	m_avgTime = m_sumTime = 0;
}

SampleGrabberCallback::~SampleGrabberCallback() {
	delete[] m_bitmap;
}

ULONG STDMETHODCALLTYPE SampleGrabberCallback::AddRef() {
	InterlockedIncrement(&m_cntRef);
	return m_cntRef;
}

ULONG STDMETHODCALLTYPE SampleGrabberCallback::Release() {
	ULONG ulCntRef = InterlockedDecrement(&m_cntRef);
	if (0 == m_cntRef) {
		delete this;
	}
	return ulCntRef;
}

HRESULT STDMETHODCALLTYPE SampleGrabberCallback::QueryInterface(REFIID riid, void** ppv) {
	if (ppv == nullptr)
		return E_POINTER;
	        
	if( riid == IID_ISampleGrabberCB || riid == IID_IUnknown ) {
		*ppv = (void *) static_cast<ISampleGrabberCB*> ( this );
		return NOERROR;
	}    
	else 
		return E_NOINTERFACE;
}


STDMETHODIMP SampleGrabberCallback::BufferCB(double SampleTime, BYTE* pBuffer, long BufferLen) {
	return E_NOTIMPL;
}

STDMETHODIMP SampleGrabberCallback::SampleCB(double SampleTime, IMediaSample* pSample) {
	using namespace std;

	// copy pixels
	long bufferSize = 0;
	HRESULT hr = pSample->GetPointer(&m_bufferMediaSample);
    if(FAILED(hr)) {
        cerr << "sample grabber callback: cannot get pointer to media sample buffer!" << endl;   
		throw "com error";
    } else {
		bufferSize = pSample->GetSize();
	}
	memcpy(m_bitmap, m_bufferMediaSample, bufferSize);

	// performance counting
	LARGE_INTEGER lNewTime, lMicroSecElapsed;
	LARGE_INTEGER lFrequency;

	// microsec since last callback
	BOOL success = QueryPerformanceFrequency(&lFrequency);
	success = QueryPerformanceCounter(&lNewTime);
	lMicroSecElapsed.QuadPart = lNewTime.QuadPart - m_lastTime.QuadPart;
	lMicroSecElapsed.QuadPart *= 1000000; 		// ticks per second
	lMicroSecElapsed.QuadPart /= lFrequency.QuadPart;
	m_lastTime = lNewTime;

	// DEBUG printout average sample time
	++m_counter;
	if (m_counter > 1) {
		m_sumTime += lMicroSecElapsed.QuadPart;
		m_avgTime = m_sumTime / m_counter;
	}
	if (m_counter % 100 == 0) {
		cout << "average time in ms: " << m_avgTime / 1000 << " presentation time: " << SampleTime << endl;
		cout << "bufferSize: " << bufferSize << endl;
	}



	return 0;
}

bool SampleGrabberCallback::setBitmapSize(size_t bitmapSize) {
	m_bitmap = new BYTE[bitmapSize];
	return true;
}



CamInput::CamInput() {
	using namespace std;
	// init com
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if(FAILED(hr)) {
        cerr << "CamInput: cannot initialize com!" << endl;   
		throw "com error";
    }

	// init direct show interface pointers
	m_captureBuilder = nullptr;
	m_graphBuilder = nullptr;
	m_mediaControl = nullptr;

	m_camSrcFilter = nullptr;
	m_grabFilter = nullptr;
	m_renderFilter = nullptr;

	// add sample grabber callback
	m_sGrabCallBack = new SampleGrabberCallback;
}


void releaseMoniker(CamDevice& camDevice) {
	camDevice.pMoniker->Release();
	return;
}


CamInput::~CamInput() {
	// unregister from running object table
	if (!removeFromRot(m_registerRot))
		throw "com error";

	// delete sample grabber callback

	// release moniker for each cam device
	std::for_each(m_deviceArray.begin(), m_deviceArray.end(), releaseMoniker);

	m_captureBuilder->Release();
	m_graphBuilder->Release();
	m_sampleGrabber->Release();

	m_mediaControl->Release();
	m_videoWindow->Release();
	
	m_camSrcFilter->Release();
	m_grabFilter->Release();
	m_renderFilter->Release();

	delete m_sGrabCallBack;

	CoUninitialize();
}


// create and add capture filter to graph
bool CamInput::addCamSrcFilter(int capDeviceNum, IGraphBuilder* pGraph) {
	using namespace std;
	IMoniker* pMoniker = nullptr;
	
	// deviceNumber out of range
	if (capDeviceNum >= (int)m_deviceArray.size())
		return false;
	else
		pMoniker = m_deviceArray.at(capDeviceNum).pMoniker;

	// create capture filter object
	HRESULT hr = pMoniker->BindToObject(0,0,IID_IBaseFilter, (void**)&m_camSrcFilter);
	if (FAILED(hr)) {
		cerr << "addCapFilter: cannot access video capture device "
			<< capDeviceNum << endl;  
		m_camSrcFilter->Release();
		throw "com error";
		return false;
	}

	// add capture filter to graph
	hr = pGraph->AddFilter(m_camSrcFilter, L"Capture Filter");
	if (FAILED(hr)) {
		cerr << "addCapFilter: cannot add capture filter to graph" << endl;   
		m_camSrcFilter->Release();
		throw "com error";
		return false;
	}

	return true;
}

// create and add sample grabber to graph
bool CamInput::addGrabFilter(IGraphBuilder* pGraph) {
	using namespace std;

	// create grabber filter instance
    HRESULT hr = CoCreateInstance (CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void **)&m_grabFilter);
	if (FAILED(hr)) {
		cerr << "addGrabFilter: cannot create grab filter instance" << endl;   
		m_grabFilter->Release();
		throw "com error";
		return false;
	}

	// add grabber filter to graph
	hr = pGraph->AddFilter(m_grabFilter, L"Sample Grabber");
	if (FAILED(hr)) {
		cerr << "addGrabFilter: cannot add grabber filter to graph" << endl;
		m_grabFilter->Release();
		throw "com error";
		return false;
	}

	// get sample grabber interface
	ISampleGrabber* pGrabber = NULL;
	hr = m_grabFilter->QueryInterface(IID_ISampleGrabber, (void**)&m_sampleGrabber);
	if (FAILED(hr)) {
		cerr << "addGrabFilter: cannot get sample grabber interface" << endl;   
		m_sampleGrabber->Release();
		throw "com error";
		return false;
	}

	return true;
}


bool CamInput::addNullRenderFilter(IGraphBuilder* pGraph) {
	using namespace std;

	// create null renderer filter instance
	HRESULT hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER, 
		IID_PPV_ARGS(&m_renderFilter));
	if (FAILED(hr)) {
		cerr << "addNullRender: cannot create null renderer filter instance" << endl;
		m_renderFilter->Release();
		throw "com error";
		return false;
	}

	// add null renderer filter to graph
	hr = pGraph->AddFilter(m_renderFilter, L"Null Renderer");
	if (FAILED(hr)) {
		cerr << "addNullRender: cannot add null renderer filter to graph" << endl;
		m_renderFilter->Release();
		throw "com error";
		return false;
	}

	return true;
}


// enumerate devices
int CamInput::enumerateDevices() {
	using namespace std;
	
	m_deviceArray.clear();
	int nDevices = 0;

	ICreateDevEnum* pDevEnum = nullptr; // released
    HRESULT hr = CoCreateInstance (CLSID_SystemDeviceEnum,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum,
		(void **) &pDevEnum);
	 if(FAILED(hr)) {
        cerr << "enumerateDevices: cannot create device enum!" << endl; 
		pDevEnum->Release();
		throw "com error";
		return 0;
    }

	IEnumMoniker *pClassEnum = nullptr; // released
	hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
		&pClassEnum, 0);
	if(FAILED(hr)) {
        cerr << "enumerateDevices: cannot create class enum!" << endl;   
		pDevEnum->Release();
		pClassEnum->Release();
		throw "com error";
		return 0;
	}

	IMoniker* pMoniker = nullptr; // released
	IPropertyBag* pPropBag = nullptr; // released
	CamDevice camDevice;
	while (S_OK == pClassEnum->Next(1, &pMoniker, nullptr) ) {
		
		// get friendly name by using use property bag
		hr = pMoniker->BindToStorage(nullptr, nullptr,
			IID_IPropertyBag, (void**)&pPropBag);
		if(FAILED(hr)) {
			cerr << "enumerateDevices: cannot bind moniker to storage!" << endl;   
			pDevEnum->Release();
			pClassEnum->Release();
			pMoniker->Release();
			pPropBag->Release();
			throw "com error";
			return 0;
		}

		VARIANT var;
		VariantInit(&var);
		hr = pPropBag->Read(L"FriendlyName", &var, 0);
		if(FAILED(hr)) {
			cerr << "enumerateDevices: cannot retrieve friendly name!" << endl;
			pDevEnum->Release();
			pClassEnum->Release();
			pMoniker->Release();
			pPropBag->Release();
			throw "com error";
			return 0;
		}

		wstring friendlyName(var.bstrVal, SysStringLen(var.bstrVal));
		camDevice.name = friendlyName;
		camDevice.pMoniker = pMoniker;
		pMoniker->AddRef(); // reference to moniker stored in deviceArray

		m_deviceArray.push_back(camDevice);
		++nDevices;
	}
	
	pDevEnum->Release();
	pClassEnum->Release();
	pMoniker->Release();
	pPropBag->Release();
	return nDevices;
}


bool isUncompressed(GUID mediaSubtype) {
	if (mediaSubtype == MEDIASUBTYPE_RGB24) 
		return true;
	if (mediaSubtype == MEDIASUBTYPE_RGB32)
		return true;
	if (mediaSubtype == MEDIASUBTYPE_RGB8)
		return true;
	if (mediaSubtype == MEDIASUBTYPE_RGB4)
		return true;
	if (mediaSubtype == MEDIASUBTYPE_RGB1)
		return true;
	return false;
}

int CamInput::enumerateStreamCaps() {
	using namespace std;
	
	IAMStreamConfig* pStreamCfg;
	HRESULT hr = m_captureBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
		&MEDIATYPE_Video, m_camSrcFilter, IID_IAMStreamConfig, (void**)&pStreamCfg);
	if(FAILED(hr)) {
		cerr << "enumerateStreamCaps: cannot find stream config interface!" << endl;   
		throw "com error";
	}

	int countCaps = 0, sizeCaps = 0, countUncompressed = 0;
	hr = pStreamCfg->GetNumberOfCapabilities(&countCaps, &sizeCaps);
	if(FAILED(hr)) {
		cerr << "enumerateStreamCaps: cannot access capabilities!" << endl;   
		throw "com error";
	}

	for (int i = 0; i < countCaps; ++i) {
		AM_MEDIA_TYPE* pMediaType;
		VIDEO_STREAM_CONFIG_CAPS videoConfigCaps;
		hr = pStreamCfg->GetStreamCaps(i, &pMediaType, (BYTE*)&videoConfigCaps);
		if(FAILED(hr)) {
			cerr << "enumerateStreamCaps: error getting capability #" << i << endl;   
			throw "com error";
		}	

		// check videoinfoheader
		if (pMediaType->formattype != FORMAT_VideoInfo) {
			cerr << "enumerateStreamCaps: wrong format type for videoinfoheader" << endl;
			return 0;
		}

		// list only uncompressed video formats
		if (isUncompressed(pMediaType->subtype)) {
			VIDEOINFOHEADER* pVideoInfo = (VIDEOINFOHEADER*)pMediaType->pbFormat;
			StreamCaps streamCaps;
			streamCaps.bmiHeader = pVideoInfo->bmiHeader;
			streamCaps.mediaType = *pMediaType;
				
			m_streamCapsArray.push_back(streamCaps);
			++countUncompressed;
		}
	} // end for

	return countUncompressed;
}


double CamInput::get(int propID) {
	using namespace std;	
	
	// get stream config interface
	IAMStreamConfig* pStreamCfg;
	HRESULT hr = m_captureBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
		&MEDIATYPE_Video, m_camSrcFilter, IID_IAMStreamConfig, (void**)&pStreamCfg);
	if(FAILED(hr)) {
		cerr << "get: cannot find stream config interface!" << endl;   
		throw "com error";
	}

	// get format 
	AM_MEDIA_TYPE mediaType;
	AM_MEDIA_TYPE* pMediaType = &mediaType;
	pStreamCfg->GetFormat(&pMediaType);
	VIDEOINFOHEADER* pVideoInfo = (VIDEOINFOHEADER*)pMediaType->pbFormat;
	BITMAPINFOHEADER bitmapInfo = (BITMAPINFOHEADER)pVideoInfo->bmiHeader;

	double retVal = 0;
	switch (propID) {
	case CV_CAP_PROP_FRAME_WIDTH:
		retVal = (double)bitmapInfo.biWidth;
		break;
	case CV_CAP_PROP_FRAME_HEIGHT:
		retVal = (double)bitmapInfo.biHeight;
		break;
	default: 
		break;
	}

	pStreamCfg->Release();
	return retVal;
}


bool CamInput::initGraph(int capDeviceNum) {
	using namespace std;

	// create capture graph builder
	HRESULT hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, 
		NULL, 
		CLSCTX_INPROC_SERVER,
		IID_ICaptureGraphBuilder2,
		(void**)&m_captureBuilder); // released in ~CamInput
	if (FAILED(hr)) {
		cerr << "initGraph: cannot instantiate capture graph builder" << endl;
		throw "com error";
		return false;
	}

	// create graph builder and set filter graph
	hr = CoCreateInstance(CLSID_FilterGraph,
			NULL,
			CLSCTX_INPROC_SERVER,
            IID_IGraphBuilder,
			(void**)&m_graphBuilder); // released in ~CamInput
	if (FAILED(hr)) {
		cerr << "initGraph: cannot instantiate filter graph" << endl;
		throw "com error";
		return false;
	}
	hr = m_captureBuilder->SetFiltergraph(m_graphBuilder);
	if (FAILED(hr)) {
		cerr << "initGraph: cannot set filter graph" << endl;
		throw "com error";
		return false;
	}

	// create and add filters to graph
	if ( !addCamSrcFilter(capDeviceNum, m_graphBuilder) ) {
		cerr << "initGraph: cannot add source filter" << endl;
		return false;
	}
	if ( !addGrabFilter(m_graphBuilder) ) {
		cerr << "initGraph: cannot add grab filter" << endl;
		return false;
	}
	if ( !addNullRenderFilter(m_graphBuilder) ) {
		cerr << "initGraph: cannot add sink filter" << endl;
		return false;
	}

	// set call back function
	m_sampleGrabber->SetCallback(m_sGrabCallBack, 0); // call IMediaSample method

	// get video size to set buffer
	double width = get(CV_CAP_PROP_FRAME_WIDTH);
	double height = get(CV_CAP_PROP_FRAME_HEIGHT);
	size_t videoSize = (size_t)width * (size_t)height * 3; // RGB24
	//m_sGrabCallBack->setBitmapSize(videoSize);


	// connect graph by using RenderStream
	hr = m_captureBuilder->RenderStream(&PIN_CATEGORY_CAPTURE,
		&MEDIATYPE_Video, m_camSrcFilter, m_grabFilter, nullptr);
		//&MEDIATYPE_Video, m_camSrcFilter, m_grabFilter, m_renderFilter);
	if (FAILED(hr)) {
		cerr << "initGraph: cannot connect filters" << endl;
		throw "com error";
		return false;
	}

	// media control to run / stop filter graph
	hr = m_graphBuilder->QueryInterface(IID_IMediaControl, (void**)&m_mediaControl);
	if (FAILED(hr)) {
		cerr << "initGraph: cannot query media interface" << endl;
		throw "com error";
		return false;
	}

	// video window
	hr =  m_graphBuilder->QueryInterface(IID_IVideoWindow, (void**) &m_videoWindow);
    if (FAILED(hr)) {
		cerr << "initGraph: cannot instantiate video window" << endl;
		throw "com error";
		return false;
	}

	m_videoWindow->put_Top(0);
	m_videoWindow->put_Left(0);
	//m_videoWindow->put_Right(0);
	//m_videoWindow->put_Bottom(0);
	m_videoWindow->put_Caption(L"Video Window");



	
	// TODO delete after debugging
	DWORD pdwRegister = 0;
	bool success = addToRot(m_graphBuilder, &m_registerRot);

	return true;
}

void CamInput::printStreamCaps() {
	using namespace std;
	for (size_t n = 0; n < m_streamCapsArray.size(); ++n) {
		int width = m_streamCapsArray.at(n).bmiHeader.biWidth;
		int height = m_streamCapsArray.at(n).bmiHeader.biHeight;
		cout << n << " resolution: " << width << "x" << height << endl;
	}
}

bool CamInput::runGraph() {
	using namespace std;
	HRESULT hr = m_mediaControl->Run();
	if (FAILED(hr)) {
		cerr << "runGraph: cannot run graph!" << endl;
		throw "com error";
		return false;
	} else {
		return true;
	}
}

bool CamInput::setResolution(int capabilityID) {
	using namespace std;
	
	// validate ID
	if ((unsigned int)capabilityID >= m_streamCapsArray.size() || capabilityID < 0) {
		cerr << "capability ID out of range" << endl;
		return false;
	}

	// get stream config interface
	IAMStreamConfig* pStreamCfg;
	HRESULT hr = m_captureBuilder->FindInterface(&PIN_CATEGORY_CAPTURE,
		&MEDIATYPE_Video, m_camSrcFilter, IID_IAMStreamConfig, (void**)&pStreamCfg);
	if(FAILED(hr)) {
		cerr << "setResolution: cannot find stream config interface!" << endl;   
		throw "com error";
	}

	// set 
	AM_MEDIA_TYPE* pMediaType = &(m_streamCapsArray.at(capabilityID).mediaType);
	pStreamCfg->SetFormat(pMediaType);

	// update bitmap buffer length in sample grabber callback
	long width = m_streamCapsArray.at(capabilityID).bmiHeader.biWidth;
	long height = m_streamCapsArray.at(capabilityID).bmiHeader.biHeight;
	size_t bitmapSize = width * height * 3;
	m_sGrabCallBack->setBitmapSize(bitmapSize);

	pStreamCfg->Release();
	return true;
}




bool CamInput::stopGraph() {
	using namespace std;
	HRESULT hr = m_mediaControl->Stop();
	if (FAILED(hr)) {
		cerr << "runGraph: cannot stop graph!" << endl;
		throw "com error";
		return false;
	} else {
		return true;
	}
}


bool addToRot(IUnknown* pUnkGraph, DWORD* pdwRegister) {
	using namespace std;

	IMoniker* pMoniker = nullptr;
	IRunningObjectTable* pRot = nullptr;
	HRESULT hr = GetRunningObjectTable(0, &pRot);
	if (FAILED(hr)) {
		cerr << "addToRot: cannot access running object table!" << endl;
		pRot->Release();
		throw "com error";
		return false;
	}

	const size_t strLen = 256;
	WCHAR wsz[strLen];
	swprintf_s(wsz, strLen, L"FilterGraph %08p pid %08x", (DWORD_PTR)pUnkGraph, GetCurrentProcessId() );
	hr = CreateItemMoniker(L"!", wsz, &pMoniker);
	if (FAILED(hr)) {
		cerr << "addToRot: cannot create item moniker!" << endl;
		pRot->Release();
		pMoniker->Release();
		throw "com error";
		return false;
	}

	hr = pRot->Register(ROTFLAGS_REGISTRATIONKEEPSALIVE, pUnkGraph, pMoniker, pdwRegister);
	if (FAILED(hr)) {
		cerr << "addToRot: cannot register graph!" << endl;
		pRot->Release();
		pMoniker->Release();
		throw "com error";
		return false;
	}

	pMoniker->Release();
	pRot->Release();
	return true;
}


bool removeFromRot(DWORD dwRegister) {
    IRunningObjectTable *pRot = nullptr;

    if (SUCCEEDED(GetRunningObjectTable(0, &pRot))) {
        pRot->Revoke(dwRegister);
        pRot->Release();
		return true;
    } else {
		return false;
	}
}

// TODO
// catch "com error" 