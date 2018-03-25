#pragma warning( disable : 4995 ) // disable #pragma deprecated warning
#include <windows.h>
#include <dshow.h>
#include <string>
#include <list>
#include <iostream>
#include <process.h>
#include "qedit.h"
#include "playcap.h"


HRESULT renderFile();
HRESULT renderCam();
void stopGraphViaCon(void* p);


int main (int argc, char* argv[]) {
	using namespace std;

	HRESULT hr = renderCam();
 
	cout << "Press <enter> to exit" << endl;
	string str;
	getline(cin, str);
	return 0;
}



HRESULT renderFile() {
	using namespace std;

    IGraphBuilder *pGraph = NULL;
    IMediaControl *pControl = NULL;
    IMediaEvent   *pEvent = NULL;

    // Initialize the COM library.
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr))
    {
        printf("ERROR - Could not initialize COM library");
        return hr;
    }

    // Create the filter graph manager and query for interfaces.
    hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, 
                        IID_IGraphBuilder, (void **)&pGraph);
    if (FAILED(hr))
    {
        printf("ERROR - Could not create the Filter Graph Manager.");
        return hr;
    }

    hr = pGraph->QueryInterface(IID_IMediaControl, (void **)&pControl);
    hr = pGraph->QueryInterface(IID_IMediaEvent, (void **)&pEvent);

    // Build the graph. IMPORTANT: Change this string to a file on your system.
    hr = pGraph->RenderFile(L"D:\\Users\\Holger\\count_traffic\\wildlife.wmv", NULL);
    if (SUCCEEDED(hr))
    {
        // Run the graph.
        hr = pControl->Run();
        if (SUCCEEDED(hr))
        {
            // Wait for completion.
            long evCode;
            pEvent->WaitForCompletion(INFINITE, &evCode);

            // Note: Do not use INFINITE in a real application, because it
            // can block indefinitely.
        }
    }
    pControl->Release();
    pEvent->Release();
    pGraph->Release();
    CoUninitialize();
	return hr;
}


HRESULT renderCam() {
	using namespace std;

	// Initialize COM
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if(FAILED(hr)) {
        cerr << "CoInitialize Failed!" << endl;   
		throw "com error";
    } 

	// create helper for capture filter graph
	ICaptureGraphBuilder2* pBuild = NULL; // released
    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, 
			NULL, 
			CLSCTX_INPROC_SERVER,
			IID_ICaptureGraphBuilder2,
			(void**)&pBuild );

	// create and set filter graph via graph builder
	IGraphBuilder* pGraph = NULL; // released
	hr = CoCreateInstance(CLSID_FilterGraph,
			NULL,
			CLSCTX_INPROC_SERVER,
            IID_IGraphBuilder,
			(void**)&pGraph);
	if (FAILED(hr)) {
		cerr << "cannot instantiate filter graph" << endl;
		throw "com error";
	}
	hr = pBuild->SetFiltergraph(pGraph);
	// use pGraph from now on

	// enumerate devices
	ICreateDevEnum* pDevEnum = NULL; // released
    hr = CoCreateInstance (CLSID_SystemDeviceEnum,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_ICreateDevEnum,
			(void **) &pDevEnum);

    IEnumMoniker *pClassEnum = NULL; // released
	hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
			&pClassEnum,
			0);

	// use first available capture device
	IMoniker* pMoniker = NULL; // released
	hr = pClassEnum->Next(1, &pMoniker, NULL);
	if (FAILED(hr)) {
		cerr << "no capture device found" << endl;
		throw "com error";
	}
	
	// print friendly name by using prop bag
	IPropertyBag* pPropBag; // released
	hr = pMoniker->BindToStorage(NULL, NULL, IID_IPropertyBag, (void**)&pPropBag);
	VARIANT var;
	VariantInit(&var);
	hr = pPropBag->Read(L"FriendlyName", &var, 0);
	wcout << "capture device: " << var.bstrVal << endl;
	pPropBag->Release();

	// bind moniker to filter object
    IBaseFilter *pCapSrc = NULL;  // released
	hr = pMoniker->BindToObject(0,0,IID_IBaseFilter, (void**)&pCapSrc);
	if (FAILED(hr)) {
		cerr << "unable to access video capture device" << endl;   
		throw "com error";
	}


	// add filter object to filter graph
	hr = pGraph->AddFilter(pCapSrc, L"Capture Filter");
	
	// enumerate pins to list
	IEnumPins* pPinEnum; // released
	hr = pCapSrc->EnumPins(&pPinEnum);

	IPin* pPin; // released
	LPWSTR szID = NULL;
	list<IPin*> pinList;
	PIN_INFO pinInfo;

	// pin ID (should be "0" for first cam)
	while (S_OK == pPinEnum->Next(1, &pPin, NULL)) {
		pinList.push_back(pPin);
		hr = pPin->QueryPinInfo(&pinInfo);

		hr = pPin->QueryId(&szID);
		wstring id(szID);
		CoTaskMemFree(szID);
	}

	// set format with stream info interface
	IAMStreamConfig* pStreamCfg; // released
	hr = pPin->QueryInterface(IID_IAMStreamConfig, (void**)&pStreamCfg);
	
	int nCaps;
	int nSize;
	hr = pStreamCfg->GetNumberOfCapabilities(&nCaps, &nSize);

	list<AM_MEDIA_TYPE> capsList;
	for (int i = 0; i < nCaps; ++i) {
		AM_MEDIA_TYPE* pMediaType;
		VIDEO_STREAM_CONFIG_CAPS streamCaps;
		hr = pStreamCfg->GetStreamCaps(i, &pMediaType, (BYTE*)&streamCaps);
		capsList.push_back(*pMediaType);

		GUID majorType;
		majorType = pMediaType->majortype;
		GUID subType;
		subType = pMediaType->subtype;
	}

	// set frame size via videoinfoheader
	AM_MEDIA_TYPE* pMediaType;
	hr = pStreamCfg->GetFormat(&pMediaType);

	GUID formatType(FORMAT_None);
	formatType = pMediaType->formattype;
	if (formatType != FORMAT_VideoInfo) {
		cout << "wrong video format for capture device" << endl;
	}
	else { 
		VIDEOINFOHEADER* pVideoInfo = (VIDEOINFOHEADER*)pMediaType->pbFormat;
		// frame size before change
		int width = pVideoInfo->bmiHeader.biWidth;
		int height = pVideoInfo->bmiHeader.biHeight;
		
		pVideoInfo->bmiHeader.biWidth = 320;
		pVideoInfo->bmiHeader.biHeight = 240;
		pStreamCfg->SetFormat(pMediaType);

	}

	// setup media control to prepare the graph to run
	IMediaControl* pControl = NULL;
	hr = pGraph->QueryInterface(IID_IMediaControl, (void**)&pControl);
	IMediaEvent* pEvent = NULL;
	hr = pGraph->QueryInterface(IID_IMediaEvent, (void**)&pEvent);

	// add sample grabber
	IBaseFilter* pGrabFilter = NULL;
    hr = CoCreateInstance (CLSID_SampleGrabber,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_IBaseFilter,
			(void **)&pGrabFilter);
	if (FAILED(hr)) {
		cerr << "unable to create grab filter instance" << endl;   
		throw "com error";
	}
	hr = pGraph->AddFilter(pGrabFilter, L"Sample Grabber");
	if (FAILED(hr)) {
		cerr << "unable to add grab filter to graph" << endl;   
		throw "com error";
	}

	ISampleGrabber* pGrabber = NULL;
	hr = pGrabFilter->QueryInterface(IID_ISampleGrabber, (void**)&pGrabber);
	if (FAILED(hr)) {
		cerr << "unable to get grabber interface" << endl;   
		throw "com error";
	}




	// render stream and run graph
	hr = pBuild->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pCapSrc, 
		NULL, NULL);
	hr = pControl->Run();

	// stop graph via console (second thread)
	pControl->AddRef();
	_beginthread(stopGraphViaCon, 0, (void*) pControl);



	// wait for graph to be stopped
	FILTER_STATE filterState = State_Running;
	bool done = false;
	while (!done) {
		hr = pControl->GetState(1000, (OAFilterState*)&filterState);
		done = (filterState == State_Stopped); 
	}

	//long evCode, param1, param2;
	//hr = pEvent->WaitForCompletion(INFINITE, &evCode);

	pEvent->Release();
	pControl->Release();

	pStreamCfg->Release();
	pPinEnum->Release();
	pPin->Release();
	pCapSrc->Release();
	pBuild->Release();
	pGraph->Release();

	CoUninitialize();
	return hr;
}

void stopGraphViaCon(void* p) {
	using namespace std;
	IMediaControl* pControl = (IMediaControl*)p;
	cout << "Press <enter> to stop graph";
	string str;
	getline(cin, str);

	cout << "graph stopped" << endl;

	pControl->Stop();
	pControl->Release();
	_endthread();
}


