/// usage: instantiate CamCapture in heap
/// enumerate camDevices and streamCaps (containing Resolution)
/// setDevice(), setResolution(), startGraph()
/// read() for capturing frames


#include "cam_cap_dshow.h"

#include <algorithm>
#include <cstring>		// memcpy
#include <iomanip>		// setprecision
#include <iostream>
//#include <string>:	"cam_cap_dshow.h"
//#include <vector>:	"cam_cap_dshow.h"
#include <Windows.h>
//#include <DShow.h>:	"cam_cap_dshow.h"


///////////////////////////////////////////////////////////////////////////////
// Running Object Table helper fcns
// for visualization of filter graph configuration with graphedt tool
///////////////////////////////////////////////////////////////////////////////
// add filter graph to running object table 
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

// remove filter graph from running object table
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


///////////////////////////////////////////////////////////////////////////////
// CamInput
// implementation of callback fcn invoked from sample grabber filter
///////////////////////////////////////////////////////////////////////////////
class SampleGrabberCallback : public ISampleGrabberCB {
private:
	ULONG				m_cntRef;
	BYTE*				m_bitmap; // TODO provide this bitmap array from cv application
	long				m_height;
	long				m_width;
	int					m_depth;
	BYTE*				m_bufferMediaSample;
	CRITICAL_SECTION	m_lockBufferCopy;
	HANDLE				m_newFrameEvent;
	bool				m_timeMeasurement;

public:
	SampleGrabberCallback();
	~SampleGrabberCallback();
	ULONG STDMETHODCALLTYPE AddRef(); // virtual method in IUnknown, must be implemented
	ULONG STDMETHODCALLTYPE Release(); // virtual method in IUnknown, must be implemented
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv); // virtual

	STDMETHODIMP BufferCB(double SampleTime, BYTE* pBuffer, long BufferLen);
	STDMETHODIMP SampleCB(double SampleTime, IMediaSample* pSample);
	BYTE* getBitmap();
	HANDLE getNewFrameEventHandle();
	bool setBitmapSize(long height, long width, int nByteDepth);
};

SampleGrabberCallback::SampleGrabberCallback() {
	InitializeCriticalSection(&m_lockBufferCopy);
	m_newFrameEvent = CreateEvent(nullptr, true, false, L"new frame");
	m_timeMeasurement = true;
}

SampleGrabberCallback::~SampleGrabberCallback() {
	delete[] m_bitmap;
	DeleteCriticalSection(&m_lockBufferCopy);
	CloseHandle(m_newFrameEvent);
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

// method not used
STDMETHODIMP SampleGrabberCallback::BufferCB(double SampleTime, BYTE* pBuffer, long BufferLen) {
	return E_NOTIMPL;
}

// copy frame buffer with horizontal flip 
void cpyFlipHori(BYTE dst[], const BYTE src[], int height, int width, int nByteDepth) {
	int rowLen = width * nByteDepth;
	for (int i_row = 0; i_row < height; ++i_row) {
		memcpy(&dst[i_row * rowLen], &src[(height - i_row - 1) * rowLen], rowLen);
	}
	return;
}

// method is called each time a new media sample arrives
STDMETHODIMP SampleGrabberCallback::SampleCB(double SampleTime, IMediaSample* pSample) {
	using namespace std;
	
	// performance counting
	static bool init = false;
	static int counter = 0;
	static double sample_time_max_ms = 0, sample_time_min_ms = 0;
	static double copy_time_sum = 0, sample_time_sum = 0;
	static LARGE_INTEGER lPrevious = { 0 };
	LARGE_INTEGER lFreq, lActual, lAfterCpy; 


	// ticks since last callback
	QueryPerformanceFrequency(&lFreq);
	double freq = (double)lFreq.QuadPart;
	QueryPerformanceCounter(&lActual);

	// get pointer to pixel buffer to copy from
	long bufferSize = 0;
	HRESULT hr = pSample->GetPointer(&m_bufferMediaSample);
    if(FAILED(hr)) {
        cerr << "sample grabber callback: cannot get pointer to media sample buffer!" << endl;   
		throw "com error";
    } else {
		bufferSize = pSample->GetSize();
	}

	// copy to bitmap buffer, flip horizontally and raise new frame event
	EnterCriticalSection(&m_lockBufferCopy);
	// copy without flipping: memcpy(m_bitmap, m_bufferMediaSample, bufferSize);
	cpyFlipHori(m_bitmap, m_bufferMediaSample, (int)m_height, (int)m_width, m_depth);
	LeaveCriticalSection(&m_lockBufferCopy);
	SetEvent(m_newFrameEvent);

	// ticks after copying frame buffer
	QueryPerformanceCounter(&lAfterCpy);

	if (m_timeMeasurement) {
		// copy time
		double copy_time_ms = (double)(lAfterCpy.QuadPart - lActual.QuadPart) * 1000 / freq;
		// sample time
		double sample_time_ms = (double)(lActual.QuadPart - lPrevious.QuadPart) * 1000 / freq;

		if (!init) { // skip first calculation in order to populate previous time
			init = true;
		} else { // normal calculation after init
			copy_time_sum += copy_time_ms;
			sample_time_sum += sample_time_ms;

			sample_time_max_ms = (sample_time_max_ms == 0) ? sample_time_ms : sample_time_max_ms;  
			sample_time_min_ms = (sample_time_min_ms == 0) ? sample_time_ms : sample_time_min_ms;
			sample_time_max_ms = (sample_time_ms > sample_time_max_ms) ? sample_time_ms : sample_time_max_ms; 
			sample_time_min_ms = (sample_time_ms < sample_time_min_ms) ? sample_time_ms : sample_time_min_ms;
	
			++counter;
			if (counter >= 100) {
				double avg_sample = sample_time_sum / counter;
				cout << fixed << setprecision(1)
					<< "sample time in ms: avg: " << avg_sample 
					<< ", min: " << sample_time_min_ms 
					<< ", max: " << sample_time_max_ms << endl;
				double avg_cpy = copy_time_sum / counter;
				cout << "bufferSize: " << bufferSize;
				cout << fixed << setprecision(3) 
					<< ", time to copy frame buffer in ms: " <<  avg_cpy << endl;

				counter = 0;
				copy_time_sum = sample_time_sum = 0;
				sample_time_max_ms = 0, sample_time_min_ms = 0;
			}
		}
		lPrevious = lActual;
	} // end_if m_timeMeasurement

	return 0;
}

BYTE* SampleGrabberCallback::getBitmap() {
	return m_bitmap;
}

HANDLE SampleGrabberCallback::getNewFrameEventHandle() {
	return m_newFrameEvent;
}

bool SampleGrabberCallback::setBitmapSize(long height, long width, int nByteDepth) {
	if (height <= 0 || width <= 0 || nByteDepth <= 0)
		return false;
	else {
		m_height = height;
		m_width = width;
		m_depth = nByteDepth;
		int bitmapSize = height * width * nByteDepth;
		m_bitmap = new BYTE[bitmapSize];
		return true;
	}
}


///////////////////////////////////////////////////////////////////////////////
// CamInput
// replacement for cv::VideoCapture with ability to set camera frame size
///////////////////////////////////////////////////////////////////////////////
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


// helper fcn for releasing monikers for cam devices in D'tor
void releaseMoniker(CamDevice& camDevice) {
	camDevice.pMoniker->Release();
	return;
}

CamInput::~CamInput() {
	// stop graph, if still running
	if (&CamInput::isGraphRunning) {
		m_mediaControl->Stop();
	}

	// unregister from running object table
	if (!removeFromRot(m_registerRot))
		throw "com error";

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


// source filter (camera capture), add to filter graph
bool CamInput::addCamSrcFilter(int deviceID, IGraphBuilder* pGraph) {
	using namespace std;
	IMoniker* pMoniker = nullptr;
	
	// deviceNumber out of range
	if (deviceID >= (int)m_deviceArray.size())
		return false;
	else
		pMoniker = m_deviceArray.at(deviceID).pMoniker;

	// create capture filter object
	HRESULT hr = pMoniker->BindToObject(0,0,IID_IBaseFilter, (void**)&m_camSrcFilter);
	if (FAILED(hr)) {
		cerr << "addCapFilter: cannot access video capture device "
			<< deviceID << endl;  
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


// intermediate sample grabber filter (between capture and renderer)
// crate and add to filter graph
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


// sink filter for sample grabber filter, add to filter graph
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


// enumerate camera devices and get their friendly names
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


// helper fcn to filter stream capabilities for uncompressed media types
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


// stream capabilities contain possible frame sizes (VIDEOINFOHEADER -> BITMAPINFOHEADER)
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


// for cv::VideoCapture::get() compatibility
// properties CV_CAP_PROP_FRAME_WIDTH and CV_CAP_PROP_FRAME_HEIGHT implemented
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


// returns friendly name of selected camera device
std::wstring CamInput::getDevice(int deviceID) {
	if (deviceID < 0 || deviceID >= (int)m_deviceArray.size())
		return std::wstring(L"ID out of range");
	else // ID within range
		return m_deviceArray.at(deviceID).name;
}


// returns rect of frame size width x height
cv::Size CamInput::getResolution(int capabilityID) {
	cv::Size frameSize(0,0);
	if (capabilityID < 0 || capabilityID >= (int)m_streamCapsArray.size())
		frameSize.width = frameSize.height = 0;
	else { // ID within range
		frameSize.width = m_streamCapsArray.at(capabilityID).bmiHeader.biWidth;
		frameSize.height = m_streamCapsArray.at(capabilityID).bmiHeader.biHeight;
	}
	return frameSize;
}


// set camera device, build and initialize filter graph
bool CamInput::setDevice(int deviceID) {
	using namespace std;

	// create capture graph builder
	HRESULT hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, 
		NULL, 
		CLSCTX_INPROC_SERVER,
		IID_ICaptureGraphBuilder2,
		(void**)&m_captureBuilder); // released in ~CamInput
	if (FAILED(hr)) {
		cerr << "setDevice: cannot instantiate capture graph builder" << endl;
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
		cerr << "setDevice: cannot instantiate filter graph" << endl;
		throw "com error";
		return false;
	}
	hr = m_captureBuilder->SetFiltergraph(m_graphBuilder);
	if (FAILED(hr)) {
		cerr << "setDevice: cannot set filter graph" << endl;
		throw "com error";
		return false;
	}

	// create and add filters to graph
	if ( !addCamSrcFilter(deviceID, m_graphBuilder) ) {
		cerr << "setDevice: cannot add source filter" << endl;
		return false;
	}
	if ( !addGrabFilter(m_graphBuilder) ) {
		cerr << "setDevice: cannot add grab filter" << endl;
		return false;
	}
	if ( !addNullRenderFilter(m_graphBuilder) ) {
		cerr << "setDevice: cannot add sink filter" << endl;
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
		//&MEDIATYPE_Video, m_camSrcFilter, m_grabFilter, nullptr);
		&MEDIATYPE_Video, m_camSrcFilter, m_grabFilter, m_renderFilter);
	if (FAILED(hr)) {
		cerr << "setDevice: cannot connect filters" << endl;
		throw "com error";
		return false;
	}

	// media control to run / stop filter graph
	hr = m_graphBuilder->QueryInterface(IID_IMediaControl, (void**)&m_mediaControl);
	if (FAILED(hr)) {
		cerr << "setDevice: cannot query media interface" << endl;
		throw "com error";
		return false;
	}

	// video window
	hr =  m_graphBuilder->QueryInterface(IID_IVideoWindow, (void**) &m_videoWindow);
    if (FAILED(hr)) {
		cerr << "setDevice: cannot instantiate video window" << endl;
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


// test if filter graph is running and rendering data
bool CamInput::isGraphRunning() {
	using namespace std;
	FILTER_STATE filterState = State_Stopped;

	if (m_mediaControl) {
		HRESULT hr = m_mediaControl->GetState(100, (OAFilterState*)&filterState);
		if(FAILED(hr)) {
			cerr << "isGraphRunning: cannot get filter state!" << endl;   
			throw "com error";
		} else { // HRESULT == S_OK
			if (filterState == State_Running)
				return true;
			else
				return false;
		}
	} else { // media control member not initialized yet
		return false;
	}
}


// TODO delete, implement in application by calling getResolution(capabilityID)
void CamInput::printStreamCaps() {
	using namespace std;
	for (size_t n = 0; n < m_streamCapsArray.size(); ++n) {
		int width = m_streamCapsArray.at(n).bmiHeader.biWidth;
		int height = m_streamCapsArray.at(n).bmiHeader.biHeight;
		cout << n << " resolution: " << width << "x" << height << endl;
	}
}


// cv::VideoCapture::read() compatible
// check isGraphRunning before calling this function
// TODO implement dropped frames counter (if application is not fast enough)
bool CamInput::read(cv::Mat& bitmap) {
	using namespace cv;
	using namespace std;
	int width = (int)get(CV_CAP_PROP_FRAME_WIDTH);
	int height = (int)get(CV_CAP_PROP_FRAME_HEIGHT);

	// wait max 2000ms (= 0.5 fps) for new frame
	HANDLE hNewFrameEvent = m_sGrabCallBack->getNewFrameEventHandle();
	DWORD dwStatus = WaitForSingleObject(hNewFrameEvent, 2000); 

	// assign buffer to mat if new frame available
	if (dwStatus == WAIT_OBJECT_0) {
		BYTE* buffer = m_sGrabCallBack->getBitmap();
		Mat image(height, width, CV_8UC3, buffer);
		image.copyTo(bitmap);
		if (ResetEvent(hNewFrameEvent))
			return true;
		else {
			cerr << "read: cannot reset new frame event" << endl;
			return false;
		}

	// return false if timed out
	} else if (dwStatus == WAIT_TIMEOUT) {
		cout << "read: no new frames available anymore" << endl;
		return false;
	
	// error message otherwise
	} else {
		cerr << "read: error on waiting for event" << endl;
		return false;
	}
}


// start capturing frames
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


// set camera resolution
// filter graph must be stopped!
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
	int nByteDepth = 3; // RGB
	m_sGrabCallBack->setBitmapSize(height, width, nByteDepth);

	pStreamCfg->Release();
	return true;
}


// stop capturing frames
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

// TODO
// catch "com error" 